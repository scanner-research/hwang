/* Copyright 2016 Carnegie Mellon University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hwang/decoder_automata.h"

#include "hwang/util/h264.h"

#include <thread>
#include <cassert>

namespace hwang {
namespace {
// Dummy until we add back the profiler
int32_t now() {
  return 0;
}
}

DecoderAutomata::DecoderAutomata(DeviceHandle device_handle,
                                 int32_t num_devices,
                                 VideoDecoderType decoder_type,
                                 VideoDecoderInterface* decoder)
    : device_handle_(device_handle), num_devices_(num_devices),
      decoder_type_(decoder_type),
      decoder_(decoder),
      feeder_waiting_(false), not_done_(true), frames_retrieved_(0),
      skip_frames_(false) {
  feeder_thread_ = std::thread(&DecoderAutomata::feeder, this);
  result_set_ = false;
}

DecoderAutomata* DecoderAutomata::make_instance(
    DeviceHandle device_handle, int32_t num_devices,
    VideoDecoderType decoder_type) {
  auto decoder = VideoDecoderFactory::make_from_config(
      device_handle, num_devices, decoder_type);
  if (decoder == nullptr) {
    return nullptr;
  }
  return new DecoderAutomata(device_handle, num_devices, decoder_type, decoder);
}

DecoderAutomata::~DecoderAutomata() {
  {
    frames_to_get_ = 0;
    frames_retrieved_ = 0;
    while (decoder_->decoded_frames_buffered() > 0) {
      decoder_->discard_frame();
    }

    std::unique_lock<std::mutex> lk(feeder_mutex_);
    wake_feeder_.wait(lk, [this] { return feeder_waiting_.load(); });

    if (frames_retrieved_ > 0) {
      decoder_->flush();
      while (decoder_->decoded_frames_buffered() > 0) {
        decoder_->discard_frame();
      }
    }
    not_done_ = false;
    feeder_waiting_ = false;
  }

  wake_feeder_.notify_one();
  feeder_thread_.join();
}

Result DecoderAutomata::initialize(const std::vector<EncodedData> &encoded_data,
                                 const std::vector<uint8_t> &extradata) {
  assert(!encoded_data.empty());
  while (decoder_->decoded_frames_buffered() > 0) {
    HWANG_RETURN_ON_ERROR(decoder_->discard_frame());
  }

  std::unique_lock<std::mutex> lk(feeder_mutex_);
  wake_feeder_.wait(lk, [this] { return feeder_waiting_.load(); });

  encoded_data_ = encoded_data;
  frame_size_ = encoded_data[0].width * encoded_data[0].height * 3;
  current_frame_ = encoded_data[0].start_keyframe;
  next_frame_.store(encoded_data[0].valid_frames[0], std::memory_order_release);
  retriever_data_idx_.store(0, std::memory_order_release);
  retriever_valid_idx_ = 0;

  VideoDecoderInterface::FrameInfo info;
  info.height = encoded_data[0].height;
  info.width = encoded_data[0].width;
  info.format = encoded_data[0].format;

  // printf("extradata size %lu\n", extradata.size());
  HWANG_RETURN_ON_ERROR(decoder_->configure(info, extradata))

  if (frames_retrieved_ > 0) {
    HWANG_RETURN_ON_ERROR(decoder_->flush());
    while (decoder_->decoded_frames_buffered() > 0) {
      HWANG_RETURN_ON_ERROR(decoder_->discard_frame());
    }
  }

  set_feeder_idx(0);
  info_ = info;
  std::atomic_thread_fence(std::memory_order_release);
  seeking_ = false;

  return Result();
}

Result DecoderAutomata::get_frames(uint8_t *buffer, int32_t num_frames) {
  int64_t total_frames_decoded = 0;
  int64_t total_frames_used = 0;

  auto start = now();

  // Wait until feeder is waiting
  {
    // Wait until frames are being requested
    std::unique_lock<std::mutex> lk(feeder_mutex_);
    wake_feeder_.wait(lk, [this] { return feeder_waiting_.load(); });
  }

  frames_retrieved_ = 0;
  frames_to_get_ = num_frames;
  // We don't want to send discontinuity packet and flush until we know
  // we have exhausted this decode args group
  if (encoded_data_.size() > retriever_data_idx_) {
    const auto &valid_frames = encoded_data_[retriever_data_idx_].valid_frames;
    // If we are at the end of a segment or if the retriever and the feeder
    // are working on the same segment
    if (retriever_valid_idx_ == valid_frames.size() ||
        retriever_data_idx_ == feeder_data_idx_) {
      // Make sure to not feed seek packet if we reached end of stream
      if (encoded_data_.size() > feeder_data_idx_) {
        if (seeking_) {
          while (decoder_->decoded_frames_buffered() > 0) {
            decoder_->discard_frame();
            total_frames_decoded++;
          }
          seeking_ = false;
        }
      }

      // Start up feeder thread
      {
        std::unique_lock<std::mutex> lk(feeder_mutex_);
        feeder_waiting_ = false;
      }
      wake_feeder_.notify_one();
    }
  }

  // if (profiler_) {
  //   profiler_->add_interval("get_frames_wait", start, now());
  // }

  while (frames_retrieved_ < frames_to_get_) {
    if (result_set_.load()) {
      HWANG_RETURN_ON_ERROR(feeder_result_);
    }
    if (decoder_->decoded_frames_buffered() > 0) {
      auto iter = now();
      // New frames
      bool more_frames = true;
      while (more_frames && frames_retrieved_ < frames_to_get_) {
        const auto &valid_frames =
            encoded_data_[retriever_data_idx_].valid_frames;
        assert(valid_frames.size() > retriever_valid_idx_.load());
        assert(current_frame_ <= valid_frames.at(retriever_valid_idx_));
        if (current_frame_ == valid_frames.at(retriever_valid_idx_)) {
          uint8_t *decoded_buffer = buffer + frames_retrieved_ * frame_size_;
          HWANG_RETURN_ON_ERROR(decoder_->get_frame(decoded_buffer, frame_size_));
          more_frames = (decoder_->decoded_frames_buffered() > 0);
          retriever_valid_idx_++;
          if (retriever_valid_idx_ == valid_frames.size()) {
            // Move to next decode args
            retriever_data_idx_ += 1;
            retriever_valid_idx_ = 0;

            // Trigger feeder to start again and set ourselves to the
            // start of that keyframe
            if (retriever_data_idx_ < encoded_data_.size()) {
              {
                // Wait until feeder is waiting
                // skip_frames_ = true;
                std::unique_lock<std::mutex> lk(feeder_mutex_);
                wake_feeder_.wait(lk, [this, &total_frames_decoded] {
                  while (decoder_->decoded_frames_buffered() > 0) {
                    decoder_->discard_frame();
                    total_frames_decoded++;
                  }
                  return feeder_waiting_.load();
                });
                // skip_frames_ = false;
              }

              if (seeking_) {
                while (decoder_->decoded_frames_buffered() > 0) {
                  decoder_->discard_frame();
                  total_frames_decoded++;
                }
                seeking_ = false;
              }

              {
                std::unique_lock<std::mutex> lk(feeder_mutex_);
                feeder_waiting_ = false;
                current_frame_ =
                    encoded_data_[retriever_data_idx_].keyframes[0] - 1;
              }
              wake_feeder_.notify_one();
              more_frames = false;
            } else {
              assert(frames_retrieved_ + 1 == frames_to_get_);
            }
          }
          if (retriever_data_idx_ < encoded_data_.size()) {
            next_frame_.store(encoded_data_[retriever_data_idx_]
                                  .valid_frames[retriever_valid_idx_],
                              std::memory_order_release);
          }
          total_frames_used++;
          frames_retrieved_++;
        } else {
          HWANG_RETURN_ON_ERROR(decoder_->discard_frame());
          more_frames = (decoder_->decoded_frames_buffered() > 0);
        }
        current_frame_++;
        total_frames_decoded++;
      }
    } 
    std::this_thread::yield();
  }
  HWANG_RETURN_ON_ERROR(decoder_->wait_until_frames_copied());
  // if (profiler_) {
  //   profiler_->add_interval("get_frames", start, now());
  //   profiler_->increment("frames_used", total_frames_used);
  //   profiler_->increment("frames_decoded", total_frames_decoded);
  // }

  return Result();
}

// void DecoderAutomata::set_profiler(Profiler *profiler) {
//   profiler_ = profiler;
//   decoder_->set_profiler(profiler);
// }

void DecoderAutomata::feeder() {
  int64_t total_frames_fed = 0;
  int32_t frames_fed = 0;
  seeking_ = false;
  while (not_done_) {
    {
      // Wait until frames are being requested
      std::unique_lock<std::mutex> lk(feeder_mutex_);
      feeder_waiting_ = true;
    }
    wake_feeder_.notify_one();

    {
      std::unique_lock<std::mutex> lk(feeder_mutex_);
      wake_feeder_.wait(lk, [this] { return !feeder_waiting_; });
    }
    std::atomic_thread_fence(std::memory_order_acquire);

    // Ignore requests to feed if we have alredy fed all data
    if (encoded_data_.size() <= feeder_data_idx_) {
      continue;
    }

    // if (profiler_) {
    //   profiler_->increment("frames_fed", frames_fed);
    // }
    frames_fed = 0;
    bool seen_metadata = false;
    while (frames_retrieved_ < frames_to_get_) {
      int32_t frames_to_wait = 8;
      while (frames_retrieved_ < frames_to_get_ &&
             decoder_->decoded_frames_buffered() > frames_to_wait) {
        wake_feeder_.notify_one();
        std::this_thread::yield();
      }
      if (skip_frames_) {
        seen_metadata = false;
        seeking_ = true;
        set_feeder_idx(feeder_data_idx_ + 1);
        break;
      }
      frames_fed++;

      int32_t fdi = feeder_data_idx_.load(std::memory_order_acquire);
      const uint8_t *encoded_buffer =
          (const uint8_t *)encoded_data_[fdi].encoded_video.data();
      size_t encoded_buffer_size = encoded_data_[fdi].encoded_video.size();
      int32_t encoded_packet_size = 0;
      const uint8_t *encoded_packet = NULL;
      bool is_keyframe = false;
      if (feeder_buffer_offset_ < encoded_buffer_size &&
          feeder_current_frame_ < encoded_data_[fdi].end_keyframe) {
        uint64_t start_keyframe = encoded_data_[fdi].start_keyframe;
        encoded_packet_size =
            encoded_data_[fdi]
                .sample_sizes[feeder_current_frame_ - start_keyframe];
        feeder_buffer_offset_ =
            encoded_data_[fdi]
                .sample_offsets[feeder_current_frame_ - start_keyframe];
        encoded_packet = encoded_buffer + feeder_buffer_offset_;
        assert(0 <= encoded_packet_size &&
               encoded_packet_size < encoded_buffer_size);

        if (feeder_current_frame_ == feeder_next_keyframe_) {
          feeder_next_keyframe_idx_++;
          if (feeder_next_keyframe_idx_ <
              encoded_data_[feeder_data_idx_].keyframes.size()) {
            feeder_next_keyframe_ =
                encoded_data_[feeder_data_idx_].keyframes.at(
                    feeder_next_keyframe_idx_);
          }
          is_keyframe = true;
        }

        feeder_buffer_offset_ += encoded_packet_size;
        // printf("encoded packet size %d, ptr %p\n", encoded_packet_size,
        //        encoded_packet);
      }

      // if (seen_metadata && encoded_packet_size > 0) {
      //   const uint8_t *start_buffer = encoded_packet;
      //   int32_t original_size = encoded_packet_size;

      //   while (encoded_packet_size > 0) {
      //     const uint8_t *nal_start;
      //     int32_t nal_size;
      //     next_nal(encoded_packet, encoded_packet_size, nal_start, nal_size);
      //     if (encoded_packet_size == 0) {
      //       break;
      //     }
      //     int32_t nal_type = get_nal_unit_type(nal_start);
      //     int32_t nal_ref = get_nal_ref_idc(nal_start);
      //     if (is_vcl_nal(nal_type)) {
      //       encoded_packet = nal_start -= 3;
      //       encoded_packet_size = nal_size + encoded_packet_size + 3;
      //       break;
      //     }
      //   }
      // }

      Result result = decoder_->feed(encoded_packet, encoded_packet_size,
                                     is_keyframe);
      if (!result.ok) {
        result_set_ = true;
        feeder_result_ = result;
        continue;
      }
      fprintf(stderr, "feeder current frame %d\n", feeder_current_frame_.load());

      if (feeder_current_frame_ == feeder_next_frame_) {
        feeder_valid_idx_++;
        if (feeder_valid_idx_ <
            encoded_data_[feeder_data_idx_].valid_frames.size()) {
          feeder_next_frame_ = encoded_data_[feeder_data_idx_].valid_frames.at(
              feeder_valid_idx_);
        } else {
          // Done
          feeder_next_frame_ = -1;
        }
      }
      feeder_current_frame_++;

      // Set a discontinuity if we sent an empty packet to reset
      // the stream next time
      if (encoded_packet_size == 0) {
        //assert(feeder_buffer_offset_ >= encoded_buffer_size);
        // Reached the end of a decoded segment so flush the internal buffers
        // of the decoder and wait before moving onto the next segment
        Result result = decoder_->flush();
        if (!result.ok) {
          result_set_ = true;
          feeder_result_ = result;
          continue;
        }

        seen_metadata = false;
        seeking_ = true;
        set_feeder_idx(feeder_data_idx_ + 1);
        break;
      } else {
        seen_metadata = true;
      }
      std::this_thread::yield();
    }
  }
}

void DecoderAutomata::set_feeder_idx(int32_t data_idx) {
  feeder_data_idx_ = data_idx;
  feeder_valid_idx_ = 0;
  feeder_buffer_offset_ = 0;
  if (feeder_data_idx_ < encoded_data_.size()) {
    feeder_buffer_offset_ =
        encoded_data_[feeder_data_idx_].sample_offsets.at(0);
    feeder_current_frame_ = encoded_data_[feeder_data_idx_].keyframes.at(0);
    feeder_next_frame_ = encoded_data_[feeder_data_idx_].valid_frames.at(0);
    feeder_next_keyframe_idx_ = 0;
    feeder_next_keyframe_ =
        encoded_data_[feeder_data_idx_].keyframes.at(feeder_next_keyframe_idx_);
  }
}
} // namespace hwang
