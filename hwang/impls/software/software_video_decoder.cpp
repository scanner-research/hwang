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

#include "hwang/impls/software/software_video_decoder.h"
#include "hwang/util/h264.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libswscale/swscale.h"
}

#include <cassert>

namespace hwang {

namespace {
// Dummy until we add back the profiler
int32_t now() {
  return 0;
}

typedef struct BSFCompatContext {
    AVBSFContext *ctx;
    int extradata_updated;
} BSFCompatContext;

typedef struct H264BSFContext {
    int32_t  sps_offset;
    int32_t  pps_offset;
    uint8_t  length_size;
    uint8_t  new_idr;
    uint8_t  idr_sps_seen;
    uint8_t  idr_pps_seen;
    int      extradata_parsed;
} H264BSFContext;

}
///////////////////////////////////////////////////////////////////////////////
/// SoftwareVideoDecoder
SoftwareVideoDecoder::SoftwareVideoDecoder(int32_t device_id,
                                           DeviceType output_type,
                                           int32_t thread_count)
  : device_id_(device_id),
    output_type_(output_type),
    thread_count_(thread_count),
    codec_(nullptr),
    cc_(nullptr),
    reset_context_(true),
    sws_context_(nullptr),
    frame_pool_(1024),
    decoded_frame_queue_(1024) {

  // TODO(apoms): put a global file lock around this
  avcodec_register_all();

  av_init_packet(&packet_);

  annexb_ = nullptr;
}

SoftwareVideoDecoder::~SoftwareVideoDecoder() {
  if (annexb_) {
    av_bitstream_filter_close(annexb_);
  }

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55, 53, 0)
  avcodec_free_context(&cc_);
#else
  avcodec_close(cc_);
  av_freep(&cc_);
#endif
  while (frame_pool_.size() > 0) {
    AVFrame* frame;
    frame_pool_.pop(frame);
    av_frame_free(&frame);
  }
  while (decoded_frame_queue_.size() > 0) {
    AVFrame* frame;
    decoded_frame_queue_.pop(frame);
    av_frame_free(&frame);
  }

  sws_freeContext(sws_context_);
}

Result SoftwareVideoDecoder::configure(const FrameInfo &metadata,
                                       const std::vector<uint8_t> &extradata) {
  if (annexb_) {
    av_bitstream_filter_close(annexb_);
  }

  metadata_ = metadata;
  frame_width_ = metadata_.width;
  frame_height_ = metadata_.height;
  reset_context_ = true;

  int required_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, frame_width_,
                                               frame_height_, 1);

  conversion_buffer_.resize(required_size);

  extradata_ = extradata;
  extradata_.resize(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);

  AVCodecID codec_id = AV_CODEC_ID_H264;
  bitstream_filter_name_ = "h264_mp4toannexb";
  if (metadata.format == "h264" ||
      metadata.format == "avc1") {
    codec_id = AV_CODEC_ID_H264;
    bitstream_filter_name_ = "h264_mp4toannexb";
  } else if (metadata.format == "h265" ||
             metadata.format == "hev1" ||
             metadata.format == "hevc") {
    codec_id = AV_CODEC_ID_HEVC;
    bitstream_filter_name_ = "hevc_mp4toannexb";
  } else {
    return Result(false, "Unsupported video codec: " + metadata.format +
                             ". Supported codecs are: h264, hevc/h265");
  }
  codec_ = avcodec_find_decoder(codec_id);
  if (!codec_) {
    return Result(false,
                  "Could not find decoder for codec: " + metadata.format);
  }
  cc_ = avcodec_alloc_context3(codec_);
  if (!cc_) {
    return Result(false, "Could not alloc codec context for codec: " +
                             metadata.format);
  }

  cc_->thread_count = thread_count_;

  if (avcodec_open2(cc_, codec_, NULL) < 0) {
    return Result(false, "Could not open codec for format: " +
                             metadata.format);
  }

  if (cc_->extradata_size > 0 && cc_->extradata != nullptr) {
    free(cc_->extradata);
  }
  cc_->extradata = (uint8_t*)malloc(extradata_.size());
  memcpy(cc_->extradata, extradata_.data(), extradata_.size());
  cc_->extradata_size = extradata_.size();

  annexb_ = av_bitstream_filter_init(bitstream_filter_name_.c_str());

  return Result();
}

Result SoftwareVideoDecoder::feed(const uint8_t *encoded_buffer,
                                  size_t encoded_size, bool keyframe,
                                  bool discontinuity) {
  uint8_t *filtered_buffer = nullptr;
  int32_t filtered_size = 0;
  int err;
  if (encoded_size > 0) {
    err = av_bitstream_filter_filter(annexb_, cc_, NULL, &filtered_buffer,
                                     &filtered_size, encoded_buffer,
                                     encoded_size, keyframe);
    if (err < 0) {
      char err_msg[256];
      av_strerror(err, err_msg, 256);
      return Result(false, "Error while filtering frame (" +
                               std::to_string(err) +
                               "): " + std::string(err_msg));
    }
    if (keyframe) {
      BSFCompatContext *priv = (BSFCompatContext *)annexb_->priv_data;
      AVBSFContext *priv_ctx = (AVBSFContext *)priv->ctx;

      uint8_t *extra_buffer = priv_ctx->par_out->extradata;
      size_t extra_size = priv_ctx->par_out->extradata_size;

      int32_t temp_size = filtered_size + extra_size + 3;
      uint8_t *temp_buffer = (uint8_t *)malloc(temp_size);

      temp_buffer[0] = 0;
      temp_buffer[1] = 0;
      temp_buffer[2] = 1;
      memcpy(temp_buffer + 3, extra_buffer, extra_size);
      memcpy(temp_buffer + 3 + extra_size, filtered_buffer, filtered_size);

      free(filtered_buffer);
      filtered_buffer = temp_buffer;
      filtered_size = temp_size;
    }
  }

// Debug read packets
#if 0
  int32_t es = (int32_t)filtered_data_size;
  const uint8_t* b = filtered_buffer;
  while (es > 0) {
    const uint8_t* nal_start;
    int32_t nal_size;
    next_nal(b, es, nal_start, nal_size);
    int32_t nal_unit_type = get_nal_unit_type(nal_start);
    printf("nal unit type %d\n", nal_unit_type);

    if (nal_unit_type == 7) {
      int32_t offset = 32;
      int32_t sps_id = parse_exp_golomb(nal_start, nal_size, offset);
      printf("SPS NAL (%d)\n", sps_id);
    }
    if (nal_unit_type == 8) {
      int32_t offset = 8;
      int32_t pps_id = parse_exp_golomb(nal_start, nal_size, offset);
      int32_t sps_id = parse_exp_golomb(nal_start, nal_size, offset);
      printf("PPS id: %d, SPS id: %d\n", pps_id, sps_id);
    }
  }
#endif
  if (discontinuity) {
    while (decoded_frame_queue_.size() > 0) {
      AVFrame* frame;
      decoded_frame_queue_.pop(frame);
      av_frame_free(&frame);
    }
    while (frame_pool_.size() > 0) {
      AVFrame* frame;
      frame_pool_.pop(frame);
      av_frame_free(&frame);
    }

    packet_.data = NULL;
    packet_.size = 0;
    feed_packet(true);

    if (filtered_buffer) {
      free(filtered_buffer);
    }
    // Reinitialize filter
    if (annexb_) {
      av_bitstream_filter_close(annexb_);
    }

    if (cc_->extradata_size > 0 && cc_->extradata != nullptr) {
      free(cc_->extradata);
    }
    cc_->extradata = (uint8_t *)malloc(extradata_.size());
    memcpy(cc_->extradata, extradata_.data(), extradata_.size());
    cc_->extradata_size = extradata_.size();

    annexb_ = av_bitstream_filter_init(bitstream_filter_name_.c_str());
    return Result();
  }
  if (filtered_size > 0) {
    if (av_new_packet(&packet_, filtered_size) < 0) {
      return Result(false,
                    "could not allocate packet for feeding into decoder");
    }
    memcpy(packet_.data, filtered_buffer, filtered_size);
  } else {
    packet_.data = NULL;
    packet_.size = 0;
  }

  feed_packet(false);
  av_packet_unref(&packet_);

  if (filtered_buffer) {
    free(filtered_buffer);
  }

  return Result();
}

Result SoftwareVideoDecoder::discard_frame() {
  if (decoded_frame_queue_.size() > 0) {
    AVFrame* frame;
    decoded_frame_queue_.pop(frame);
    av_frame_unref(frame);
    frame_pool_.push(frame);
  }

  return Result();
}

Result SoftwareVideoDecoder::get_frame(uint8_t *decoded_buffer,
                                       size_t decoded_size) {
  int64_t size_left = decoded_size;

  AVFrame *frame;
  if (decoded_frame_queue_.size() > 0) {
    decoded_frame_queue_.pop(frame);
  } else {
    return Result();
  }

  if (reset_context_) {
    auto get_context_start = now();
    AVPixelFormat decoder_pixel_format = cc_->pix_fmt;
    sws_freeContext(sws_context_);
    sws_context_ = sws_getContext(
        frame_width_, frame_height_, decoder_pixel_format, frame_width_,
        frame_height_, AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);
    reset_context_ = false;
    auto get_context_end = now();
    // if (profiler_) {
    //   profiler_->add_interval("ffmpeg:get_sws_context", get_context_start,
    //                           get_context_end);
    // }
  }

  if (sws_context_ == NULL) {
    return Result(false, "Could not get sws_context for rgb conversion");
  }

  uint8_t* scale_buffer = decoded_buffer;

  uint8_t* out_slices[4];
  int out_linesizes[4];
  int required_size =
      av_image_fill_arrays(out_slices, out_linesizes, scale_buffer,
                           AV_PIX_FMT_RGB24, frame_width_, frame_height_, 1);
  if (required_size < 0) {
    return Result(false, "Error in av_image_fill_arrays");
  }
  if (required_size > decoded_size) {
    return Result(false, "Decode buffer not large enough for image");
  }
  auto scale_start = now();
  if (sws_scale(sws_context_, frame->data, frame->linesize, 0, frame->height,
                out_slices, out_linesizes) < 0) {
    return Result(false, "sws_scale failed");
  }
  auto scale_end = now();

  av_frame_unref(frame);
  frame_pool_.push(frame);

  // if (profiler_) {
  //   profiler_->add_interval("ffmpeg:scale_frame", scale_start, scale_end);
  // }

  return Result();
}

int SoftwareVideoDecoder::decoded_frames_buffered() {
  return decoded_frame_queue_.size();
}

Result SoftwareVideoDecoder::wait_until_frames_copied() {
  return Result();
}

void SoftwareVideoDecoder::feed_packet(bool flush) {
  int error;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 25, 0)
  auto send_start = now();
  error = avcodec_send_packet(cc_, &packet_);
  if (error != AVERROR_EOF) {
    if (error < 0) {
      char err_msg[256];
      av_strerror(error, err_msg, 256);
      fprintf(stderr, "Error while sending packet (%d): %s\n", error, err_msg);
      LOG(FATAL) << "Error while sending packet";
    }
  }
  auto send_end = now();

  auto received_start = now();
  bool done = false;
  while (!done) {
    AVFrame* frame;
    {
      if (frame_pool_.size() <= 0) {
        // Create a new frame if our pool is empty
        frame_pool_.push(av_frame_alloc());
      }
      frame_pool_.pop(frame);
    }

    error = avcodec_receive_frame(cc_, frame);
    if (error == AVERROR_EOF) {
      frame_pool_.push(frame);
      break;
    }
    if (error == 0) {
      if (!flush) {
        decoded_frame_queue_.push(frame);
      } else {
        av_frame_unref(frame);
        frame_pool_.push(frame);
      }
    } else if (error == AVERROR(EAGAIN)) {
      done = true;
      frame_pool_.push(frame);
    } else {
      char err_msg[256];
      av_strerror(error, err_msg, 256);
      fprintf(stderr, "Error while receiving frame (%d): %s\n", error, err_msg);
      LOG(FATAL) << "Error while receiving frame";
    }
  }
  auto received_end = now();
  // if (profiler_) {
  //   profiler_->add_interval("ffmpeg:send_packet", send_start, send_end);
  //   profiler_->add_interval("ffmpeg:receive_frame", received_start,
  //                           received_end);
  // }
#else
  uint8_t* orig_data = packet_.data;
  int orig_size = packet_.size;
  int got_picture = 0;
  do {
    // Get frame from pool of allocated frames to decode video into
    AVFrame* frame;
    {
      if (frame_pool_.size() <= 0) {
        // Create a new frame if our pool is empty
        frame_pool_.push(av_frame_alloc());
      }
      frame_pool_.pop(frame);
    }

    auto decode_start = now();
    int consumed_length =
        avcodec_decode_video2(cc_, frame, &got_picture, &packet_);
    // if (profiler_) {
    //   profiler_->add_interval("ffmpeg:decode_video", decode_start, now());
    // }
    if (consumed_length < 0) {
      char err_msg[256];
      av_strerror(consumed_length, err_msg, 256);
      fprintf(stderr, "Error while decoding frame (%d): %s\n", consumed_length,
              err_msg);
      LOG(FATAL) << "Error while decoding frame";
    }
    if (got_picture) {
      if (!flush) {
        if (frame->buf[0] == NULL) {
          // Must copy packet as data is stored statically
          AVFrame* cloned_frame = av_frame_clone(frame);
          if (cloned_frame == NULL) {
            fprintf(stderr, "could not clone frame\n");
            assert(false);
          }
          decoded_frame_queue_.push(cloned_frame);
          av_frame_free(&frame);
        } else {
          // Frame is reference counted so we can just take it directly
          decoded_frame_queue_.push(frame);
        }
      } else {
        av_frame_unref(frame);
        frame_pool_.push(frame);
      }
    } else {
      frame_pool_.push(frame);
    }
    packet_.data += consumed_length;
    packet_.size -= consumed_length;
  } while (packet_.size > 0 || (orig_size == 0 && got_picture));
  packet_.data = orig_data;
  packet_.size = orig_size;
#endif
  if (packet_.size == 0) {
    avcodec_flush_buffers(cc_);
  }
}

}
