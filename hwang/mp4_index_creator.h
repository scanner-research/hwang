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

#include "hwang/video_index.h"
#include "hwang/util/mp4.h"

#include <string>

namespace hwang {

class MP4IndexCreator {
 public:
  MP4IndexCreator(uint64_t file_size);

  // Parse chunks of data from an mp4 file
  // @param[in] data A buffer of data from the mp4 file
  // @param[in] size The size of the data buffer
  // @param[out] next_offset The next offset in the file to read from
  // @param[out] next_offset The next size of data to read from the file
  // @return False if done or there was an error
  bool feed(uint8_t* data, size_t size,
            uint64_t& next_offset,
            uint64_t& next_size);

  const VideoIndex& get_video_index();

  bool is_done() {
    return done_ || (parsed_ftyp_ && parsed_moov_ && !fragments_present_);
  }

  bool is_error() { return error_; }

  const std::string& error_message() { return error_message_; }

 private:
  const uint64_t file_size_;
  bool done_;
  bool error_;
  std::string error_message_;

  uint64_t offset_ = 0;

  // Flags for if we have found all we need
  bool parsed_ftyp_ = false;
  bool parsed_moov_ = false;
  bool fragments_present_ = false;

  std::vector<TrackExtendsBox> track_extends_boxes_;

  std::vector<uint64_t> sample_offsets_;
  std::vector<uint64_t> sample_sizes_;
  std::vector<uint64_t> keyframe_indices_;
};

}

