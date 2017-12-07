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

#include <string>

namespace hwang {

class VideoIndex {
 public:
  VideoIndex();

  const std::vector<uint8_t>& metadata_bytes() { return metadata_bytes_; }
  const std::vector<int64_t> &keyframe_positions() {
    return keyframe_positions_;
  }
  const std::vector<int64_t> &keyframe_timestamps() {
    return keyframe_timestamps_;
  }
  const std::vector<int64_t> &keyframe_byte_offsets() {
    return keyframe_byte_offsets_;
  };

  int64_t frames() { return num_frames_; };
  int64_t num_non_ref_frames() { return num_non_ref_frames_; };

private:
  int64_t num_frames_;
  int64_t num_non_ref_frames_ = 0;
  std::vector<uint8_t> metadata_bytes_;
  std::vector<int64_t> keyframe_positions_;
  std::vector<int64_t> keyframe_timestamps_;
  std::vector<int64_t> keyframe_byte_offsets_;
};

}
