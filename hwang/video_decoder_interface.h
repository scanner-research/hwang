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

#include <vector>
#include <cstdint>
#include <cstddef>

namespace hwang {

class VideoDecoderInterface {
 public:
  virtual ~VideoDecoderInterface(){};

  struct FrameInfo {
    uint32_t width;
    uint32_t height;
  };
  virtual void configure(const FrameInfo &metadata,
                         const std::vector<uint8_t> &extradata) = 0;

  virtual bool feed(const uint8_t* encoded_buffer, size_t encoded_size,
                    bool keyframe,
                    bool discontinuity = false) = 0;

  virtual bool discard_frame() = 0;

  virtual bool get_frame(uint8_t* decoded_buffer, size_t decoded_size) = 0;

  virtual int decoded_frames_buffered() = 0;

  virtual void wait_until_frames_copied() = 0;

  // void set_profiler(Profiler* profiler);

  // protected:
  // Profiler* profiler_ = nullptr;
};

}
