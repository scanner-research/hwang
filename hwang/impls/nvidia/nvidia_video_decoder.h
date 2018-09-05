/* Copyright 2016 Carnegie Mellon University, NVIDIA Corporation
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

#include "hwang/common.h"
#include "hwang/util/queue.h"
#include "hwang/video_decoder_interface.h"

#include <cuda.h>
#include <cuda_runtime.h>

#if CUDA_VERSION >= 9000
#include "hwang/impls/nvidia/nvcuvid.h"
#else
#include <nvcuvid.h>
#endif


extern "C" {
#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"
}

namespace hwang {

///////////////////////////////////////////////////////////////////////////////
/// NVIDIAVideoDecoder
class NVIDIAVideoDecoder : public VideoDecoderInterface {
public:
  NVIDIAVideoDecoder(int device_id, DeviceType output_type,
                     CUcontext cuda_context);

  ~NVIDIAVideoDecoder();

  Result configure(const FrameInfo &metadata,
                   const std::vector<uint8_t> &extradata) override;

  Result feed(const uint8_t *encoded_buffer, size_t encoded_size, bool keyframe,
              bool discontinuity = false) override;

  Result discard_frame() override;

  Result get_frame(uint8_t *decoded_buffer, size_t decoded_size) override;

  int decoded_frames_buffered() override;

  Result wait_until_frames_copied() override;

private:
  static int cuvid_handle_video_sequence(void* opaque, CUVIDEOFORMAT* format);

  static int cuvid_handle_picture_decode(void* opaque,
                                         CUVIDPICPARAMS* picparams);

  static int cuvid_handle_picture_display(void* opaque,
                                          CUVIDPARSERDISPINFO* dispinfo);

  int device_id_;
  DeviceType output_type_;
  CUcontext cuda_context_;
  static const int max_output_frames_ = 32;
  static const int max_mapped_frames_ = 8;
  std::vector<cudaStream_t> streams_;
  AVCodec* codec_;
  AVCodecContext* cc_;
  AVBitStreamFilterContext* annexb_;

  int32_t frame_width_;
  int32_t frame_height_;
  std::vector<uint8_t> metadata_packets_;
  CUvideoparser parser_;
  CUvideodecoder decoder_;

  int32_t last_displayed_frame_;
  volatile int32_t frame_in_use_[max_output_frames_];
  volatile int32_t undisplayed_frames_[max_output_frames_];
  volatile int32_t invalid_frames_[max_output_frames_];

  std::mutex frame_queue_mutex_;
  CUVIDPARSERDISPINFO frame_queue_[max_output_frames_];
  int32_t frame_queue_read_pos_;
  int32_t frame_queue_elements_;

  CUdeviceptr mapped_frames_[max_mapped_frames_];
  uint8_t* convert_frame_ = nullptr;
};
}
