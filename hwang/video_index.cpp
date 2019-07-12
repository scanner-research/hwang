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
#include "hwang/hwang_descriptors.pb.h"

#include <string>
#include <vector>
#include <cassert>
#include <tuple>

namespace hwang {

VideoIndex VideoIndex::deserialize(const std::vector<uint8_t> &data) {
  proto::VideoIndex desc;
  desc.ParseFromArray(data.data(), data.size());
  return VideoIndex(desc.timescale(), desc.duration(),
                    desc.frame_width(), desc.frame_height(),
                    desc.format(),
                    std::vector<uint64_t>(desc.sample_offsets().begin(),
                                          desc.sample_offsets().end()),
                    std::vector<uint64_t>(desc.sample_sizes().begin(),
                                          desc.sample_sizes().end()),
                    std::vector<uint64_t>(desc.keyframe_indices().begin(),
                                          desc.keyframe_indices().end()),
                    std::vector<uint8_t>(desc.metadata_bytes().begin(),
                                         desc.metadata_bytes().end()));
}

std::vector<uint8_t> VideoIndex::serialize() const {
  proto::VideoIndex desc;
  desc.set_timescale(timescale_);
  desc.set_duration(duration_);
  desc.set_frame_width(frame_width_);
  desc.set_frame_height(frame_height_);
  desc.set_format(format_);
  for (uint64_t s : sample_offsets_) {
    desc.add_sample_offsets(s);
  }
  for (uint64_t s : sample_sizes_) {
    desc.add_sample_sizes(s);
  }
  for (uint64_t k : keyframe_indices_) {
    desc.add_keyframe_indices(k);
  }
  desc.set_metadata_bytes(metadata_bytes_.data(), metadata_bytes_.size());
  std::vector<uint8_t> data(desc.ByteSizeLong());
  desc.SerializeToArray(data.data(), data.size());
  return data;
}

VideoIntervals slice_into_video_intervals(const VideoIndex &index,
                                          const std::vector<uint64_t> &rows) {
  auto keyframe_positions = index.keyframe_indices();
  keyframe_positions.push_back(index.frames());
  VideoIntervals info;
  assert(keyframe_positions.size() >= 2);
  size_t start_keyframe_index = 0;
  size_t end_keyframe_index = 1;
  uint64_t next_keyframe = keyframe_positions[end_keyframe_index];
  std::vector<uint64_t> valid_frames;

  for (uint64_t row : rows) {
    if (row >= next_keyframe) {
      // Check if this keyframe is adjacent
      uint64_t last_endpoint = index.sample_offsets().at(next_keyframe - 1) +
                               index.sample_sizes().at(next_keyframe - 1);
      bool is_adjacent =
          (last_endpoint == index.sample_offsets().at(next_keyframe));

      assert(end_keyframe_index < keyframe_positions.size() - 1);
      next_keyframe = keyframe_positions[++end_keyframe_index];

      if (row >= next_keyframe || !is_adjacent) {
        // Skipped a keyframe or keyframe is not adjacent, make a new interval
        if (!valid_frames.empty()) {
          info.sample_index_intervals.push_back(
              std::make_tuple(keyframe_positions[start_keyframe_index],
                              keyframe_positions[end_keyframe_index - 1]));
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
      std::make_tuple(keyframe_positions[start_keyframe_index],
                      keyframe_positions[end_keyframe_index]));
  info.valid_frames.push_back(valid_frames);
  return info;
}

}
