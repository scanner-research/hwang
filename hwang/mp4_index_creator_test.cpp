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

#include "hwang/mp4_index_creator.h"
#include "tests/videos.h"

#include <gtest/gtest.h>

#include <thread>

namespace hwang {

TEST(MP4IndexCreator, SimpleTest) {
  // Download video file
  std::vector<u8> video_bytes = read_entire_file(download_video(short_video));

  MP4IndexCreator indexer(video_bytes.size());

  // Load test data
  VideoMetadata video_meta =
      read_video_metadata(storage, download_video_meta(short_video));
  std::vector<u8> video_bytes = read_entire_file(download_video(short_video));
  u8* video_buffer = new_buffer(CPU_DEVICE, video_bytes.size());
  memcpy_buffer(video_buffer, CPU_DEVICE, video_bytes.data(), CPU_DEVICE,
                video_bytes.size());

  std::vector<proto::DecodeArgs> args;
  args.emplace_back();
  proto::DecodeArgs& decode_args = args.back();
  decode_args.set_width(video_meta.width());
  decode_args.set_height(video_meta.height());
  decode_args.set_start_keyframe(0);
  decode_args.set_end_keyframe(video_meta.frames());
  for (i64 r = 0; r < video_meta.frames(); ++r) {
    decode_args.add_valid_frames(r);
  }
  for (i64 k : video_meta.keyframe_positions()) {
    decode_args.add_keyframes(k);
  }
  for (i64 k : video_meta.keyframe_byte_offsets()) {
    decode_args.add_keyframe_byte_offsets(k);
  }
  decode_args.set_encoded_video((i64)video_buffer);
  decode_args.set_encoded_video_size(video_bytes.size());

  decoder->initialize(args);

  std::vector<u8> frame_buffer(short_video.width * short_video.height * 3);
  for (i64 i = 0; i < video_meta.frames(); ++i) {
    decoder->get_frames(frame_buffer.data(), 1);
  }

  delete decoder;
  delete storage;
}

}
