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

#include "hwang/video_index.h"
#include "hwang/descriptors.pb.h"

#include <string>
#include <vector>

namespace hwang {

VideoIndex VideoIndex::deserialize(const std::vector<uint8_t> &data) {
  proto::VideoIndex desc;
  desc.ParseFromArray(data.data(), data.size());
  return VideoIndex();
}

std::vector<uint8_t> VideoIndex::serialize() const {
  proto::VideoIndex desc;
  std::vector<uint8_t> data(desc.ByteSizeLong());
  desc.SerializeToArray(data.data(), data.size());
  return data;
}

VideoIntervals slice_into_video_intervals(const VideoIndex &index,
                                          const std::vector<uint64_t> &rows) {
  const auto& keyframe_positions = index.keyframe_indices();
  VideoIntervals info;
  assert(keyframe_positions.size() >= 2);
  size_t start_keyframe_index = 0;
  size_t end_keyframe_index = 1;
  uint64_t next_keyframe = keyframe_positions[end_keyframe_index];
  std::vector<uint64_t> valid_frames;
  for (uint64_t row : rows) {
    if (row >= next_keyframe) {
      assert(end_keyframe_index < keyframe_positions.size() - 1);
      next_keyframe = keyframe_positions[++end_keyframe_index];
      if (row >= next_keyframe) {
        // Skipped a keyframe, so make a new interval
        if (!valid_frames.empty()) {
          info.sample_index_intervals.push_back(
              std::make_tuple(start_keyframe_index, end_keyframe_index - 1));
          info.valid_frames.push_back(valid_frames);
        }

        while (row >= keyframe_positions[end_keyframe_index]) {
          end_keyframe_index++;
          assert(end_keyframe_index < keyframe_positions.size());
        }
        valid_frames.clear();
        start_keyframe_index = end_keyframe_index - 1;
        next_keyframe = keyframe_positions[end_keyframe_index];
      }
    }
    valid_frames.push_back(row);
  }
  info.sample_index_intervals.push_back(
      std::make_tuple(start_keyframe_index, end_keyframe_index));
  info.valid_frames.push_back(valid_frames);
  return info;
}

}
