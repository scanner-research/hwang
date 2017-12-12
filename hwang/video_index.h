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
#include <vector>

namespace hwang {

class VideoIndex {
 public:
  VideoIndex() {};

  VideoIndex(uint32_t width, uint32_t height,
             const std::vector<uint64_t> &sample_offsets,
             const std::vector<uint64_t> &sample_sizes,
             const std::vector<uint64_t> &keyframe_indices,
             const std::vector<uint8_t> &metadata)
      : frame_width_(width), frame_height_(height), num_frames_(sample_sizes.size()),
        sample_offsets_(sample_offsets), sample_sizes_(sample_sizes),
        keyframe_indices_(keyframe_indices), metadata_bytes_(metadata){};

  static VideoIndex deserialize(const std::vector<uint8_t>& data);

  std::vector<uint8_t> serialize() const;

  const std::vector<uint64_t> &sample_sizes() const {
    return sample_sizes_;
  }

  const std::vector<uint64_t> &sample_offsets() const {
    return sample_offsets_;
  }

  const std::vector<uint64_t> &keyframe_indices() const {
    return keyframe_indices_;
  }

  const std::vector<uint8_t>& metadata_bytes() const { return metadata_bytes_; }

  uint32_t frame_width() const { return frame_width_; }
  uint32_t frame_height() const { return frame_height_; }
  uint64_t frames() const { return num_frames_; }
  uint64_t num_non_ref_frames() const { return num_non_ref_frames_; }

private:
  uint32_t frame_width_;
  uint32_t frame_height_;
  uint64_t num_frames_;
  uint64_t num_non_ref_frames_ = 0;
  std::vector<uint64_t> sample_offsets_;
  std::vector<uint64_t> sample_sizes_;
  std::vector<uint64_t> keyframe_indices_;
  std::vector<uint8_t> metadata_bytes_;

};

struct VideoIntervals {
  std::vector<std::tuple<size_t, size_t>> sample_index_intervals;
  std::vector<std::vector<uint64_t>> valid_frames;
};

VideoIntervals slice_into_video_intervals(const VideoIndex &index,
                                          const std::vector<uint64_t> &rows);
}
