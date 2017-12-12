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

#pragma once

#include "hwang/video_decoder_interface.h"
#include "hwang/video_decoder_factory.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

namespace hwang {

class DecoderAutomata {
  DecoderAutomata() = delete;
  DecoderAutomata(const DecoderAutomata&) = delete;
  DecoderAutomata(const DecoderAutomata&& other) = delete;

 public:
  DecoderAutomata(DeviceHandle device_handle, int32_t num_devices,
                  VideoDecoderType decoder_type);
  ~DecoderAutomata();

  struct EncodedData {
    inline bool operator==(const EncodedData &other) const
    {
      return encoded_video == other.encoded_video && width == other.width &&
             height == other.height && start_keyframe == other.start_keyframe &&
             end_keyframe == other.end_keyframe &&
             sample_offsets == other.sample_offsets &&
             sample_sizes == other.sample_sizes &&
             keyframes == other.keyframes && valid_frames == other.valid_frames;
    }

    std::vector<uint8_t> encoded_video;
    uint32_t width;
    uint32_t height;
    uint64_t start_keyframe;
    uint64_t end_keyframe;
    std::vector<uint64_t> sample_offsets;
    std::vector<uint64_t> sample_sizes;
    std::vector<uint64_t> keyframes;
    std::vector<uint64_t> valid_frames;
  };
  void initialize(const std::vector<EncodedData> &encoded_data,
                  const std::vector<uint8_t> &extradata);

  void get_frames(uint8_t* buffer, int32_t num_frames);

  // void set_profiler(Profiler* profiler);

 private:
  void feeder();

  void set_feeder_idx(int32_t data_idx);

  const int32_t MAX_BUFFERED_FRAMES = 8;

  // Profiler* profiler_ = nullptr;

  DeviceHandle device_handle_;
  int32_t num_devices_;
  VideoDecoderType decoder_type_;
  std::unique_ptr<VideoDecoderInterface> decoder_;
  std::atomic<bool> feeder_waiting_;
  std::thread feeder_thread_;
  std::atomic<bool> not_done_;

  VideoDecoderInterface::FrameInfo info_{};
  size_t frame_size_;
  int32_t current_frame_;
  std::atomic<int32_t> reset_current_frame_;
  std::vector<EncodedData> encoded_data_;

  std::atomic<int64_t> next_frame_;
  std::atomic<int64_t> frames_retrieved_;
  std::atomic<int64_t> frames_to_get_;

  std::atomic<int32_t> retriever_data_idx_;
  std::atomic<int32_t> retriever_valid_idx_;

  std::atomic<bool> skip_frames_;
  std::atomic<bool> seeking_;
  std::atomic<int32_t> feeder_data_idx_;
  std::atomic<int64_t> feeder_valid_idx_;
  std::atomic<int64_t> feeder_current_frame_;
  std::atomic<int64_t> feeder_next_frame_;

  std::atomic<size_t> feeder_buffer_offset_;
  std::atomic<int64_t> feeder_next_keyframe_;
  std::atomic<int64_t> feeder_next_keyframe_idx_;
  std::mutex feeder_mutex_;
  std::condition_variable wake_feeder_;
};

}
