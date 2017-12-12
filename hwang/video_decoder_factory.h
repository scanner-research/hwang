/* Licensed under the Apache License, Version 2.0 (the "License");
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

#include <vector>

namespace hwang {

enum class VideoDecoderType {
  SOFTWARE,
  NVIDIA,
  INTEL,
};

class VideoDecoderFactory {
 public:
  static std::vector<VideoDecoderType> get_supported_decoder_types();

  static bool has_decoder_type(VideoDecoderType type);

  static VideoDecoderInterface *make_from_config(DeviceHandle device_handle,
                                                 uint32_t num_devices,
                                                 VideoDecoderType type);
};

}
