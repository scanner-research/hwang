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
#include "hwang/common.h"
#include "hwang/util/queue.h"

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

#include <deque>
#include <mutex>
#include <vector>

namespace hwang {

///////////////////////////////////////////////////////////////////////////////
/// SoftwareVideoDecoder
class SoftwareVideoDecoder : public VideoDecoderInterface {
public:
  SoftwareVideoDecoder(int32_t device_id, DeviceType output_type,
                       int32_t thread_count);

  ~SoftwareVideoDecoder();

  Result configure(const FrameInfo &metadata,
                   const std::vector<uint8_t>& extradata) override;

  Result feed(const uint8_t *encoded_buffer, size_t encoded_size,
              bool keyframe) override;

  Result flush() override;

  Result discard_frame() override;

  Result get_frame(uint8_t *decoded_buffer, size_t decoded_size) override;

  int decoded_frames_buffered() override;

  Result wait_until_frames_copied() override;

private:
  void feed_packet(bool flush);

  int device_id_;
  DeviceType output_type_;
  int thread_count_;
  AVPacket packet_;
  AVCodec* codec_;
  AVCodecContext* cc_;
  std::string bitstream_filter_name_;
  AVBitStreamFilterContext* annexb_;
  std::vector<uint8_t> extradata_;

  FrameInfo metadata_;
  int32_t frame_width_;
  int32_t frame_height_;
  std::vector<uint8_t> conversion_buffer_;
  bool reset_context_;
  SwsContext* sws_context_;

  Queue<AVFrame*> frame_pool_;
  Queue<AVFrame*> decoded_frame_queue_;
};

} // namespace hwang
