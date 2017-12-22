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
#include "hwang/decoder_automata.h"
#include "hwang/util/fs.h"
#include "hwang/tests/videos.h"
#include "hwang/util/fs.h"
#include "hwang/util/cuda.h"

#include <gtest/gtest.h>

#include <thread>

extern "C" {
#include "libavcodec/avcodec.h"
}

namespace hwang {

TEST(DecoderAutomata, GetAllFrames) {
  std::vector<TestVideoInfo> videos = {test_video_fragmented,
                                       test_video_unfragmented};

  avcodec_register_all();

  // Load test data
  std::vector<uint8_t> video_bytes =
      read_entire_file(download_video(videos[0]));

  // Create video index
  MP4IndexCreator indexer(video_bytes.size());
  uint64_t current_offset = 0;
  uint64_t size_to_read = std::min((size_t)1024, video_bytes.size());
  while (!indexer.is_done()) {
    indexer.feed(video_bytes.data() + current_offset, size_to_read,
                 current_offset, size_to_read);
  }
  ASSERT_FALSE(indexer.is_error());
  VideoIndex video_index = indexer.get_video_index();

  // Create decoder
  VideoDecoderType decoder_type = VideoDecoderType::SOFTWARE;
  DeviceHandle device = CPU_DEVICE;
  DecoderAutomata* decoder = new DecoderAutomata(device, 1, decoder_type);

  // Grab frames
  std::vector<DecoderAutomata::EncodedData> args;
  uint64_t last_endpoint = 1000000;
  // Walk by keyframes
  auto kf_indices = video_index.keyframe_indices();
  kf_indices.push_back(video_index.frames());
  for (uint64_t kf = 0; kf < kf_indices.size() - 1; ++kf) {
    uint64_t kfi = kf_indices.at(kf);
    uint64_t kfi_end = kf_indices.at(kf + 1);
    printf("last endpoint %lu, offset %lu\n", last_endpoint,
           video_index.sample_offsets().at(kfi));
    if (video_index.sample_offsets().at(kfi) != last_endpoint) {
      if (args.size() > 0) {
        DecoderAutomata::EncodedData& decode_args = args.back();
        decode_args.end_keyframe = kfi;
        decode_args.keyframes.push_back(kfi);
      }
      args.emplace_back();
      {
        DecoderAutomata::EncodedData &decode_args = args.back();
        decode_args.width = video_index.frame_width();
        decode_args.height = video_index.frame_height();
        decode_args.start_keyframe = kfi;
      }
      printf("new args %d\n", kfi);
    }
    DecoderAutomata::EncodedData& decode_args = args.back();
    for (uint64_t r = kfi; r < kfi_end; r++) {
      decode_args.valid_frames.push_back(r);
      decode_args.sample_offsets.push_back(video_index.sample_offsets().at(r));
      decode_args.sample_sizes.push_back(video_index.sample_sizes().at(r));
    }
    printf("valid frames %d-%d\n", kfi, kfi_end);
    decode_args.keyframes.push_back(kfi);
    decode_args.encoded_video = video_bytes;

    last_endpoint = video_index.sample_offsets().at(kfi_end - 1) +
                    video_index.sample_sizes().at(kfi_end - 1);
  }
  {
    DecoderAutomata::EncodedData& decode_args = args.back();
    decode_args.end_keyframe = video_index.frames();
    decode_args.keyframes.push_back(video_index.frames());
  }

  decoder->initialize(args, video_index.metadata_bytes());

  for (auto& arg : args) {
    std::vector<uint8_t> frame_buffer(video_index.frame_width() *
                                      video_index.frame_height() * 3 *
                                      arg.valid_frames.size());
    decoder->get_frames(frame_buffer.data(), arg.valid_frames.size());
  }

  delete decoder;
}

#ifdef HAVE_CUDA
TEST(DecoderAutomata, GetAllFramesGPU) {
  std::vector<TestVideoInfo> videos = {test_video_fragmented,
                                       test_video_unfragmented};

  avcodec_register_all();

  // Load test data
  std::vector<uint8_t> video_bytes =
      read_entire_file(download_video(videos[0]));

  // Create video index
  MP4IndexCreator indexer(video_bytes.size());
  uint64_t current_offset = 0;
  uint64_t size_to_read = std::min((size_t)1024, video_bytes.size());
  while (!indexer.is_done()) {
    indexer.feed(video_bytes.data() + current_offset, size_to_read,
                 current_offset, size_to_read);
  }
  ASSERT_FALSE(indexer.is_error());
  VideoIndex video_index = indexer.get_video_index();

  // Create decoder
  VideoDecoderType decoder_type = VideoDecoderType::NVIDIA;
  DeviceHandle device;
  device.type = DeviceType::GPU;
  device.id = 0;
  DecoderAutomata* decoder = new DecoderAutomata(device, 1, decoder_type);

  // Grab frames
  std::vector<DecoderAutomata::EncodedData> args;
  uint64_t last_endpoint = 1000000;
  // Walk by keyframes
  auto kf_indices = video_index.keyframe_indices();
  kf_indices.push_back(video_index.frames());
  for (uint64_t kf = 0; kf < kf_indices.size() - 1; ++kf) {
    uint64_t kfi = kf_indices.at(kf);
    uint64_t kfi_end = kf_indices.at(kf + 1);
    printf("last endpoint %lu, offset %lu\n", last_endpoint,
           video_index.sample_offsets().at(kfi));
    if (video_index.sample_offsets().at(kfi) != last_endpoint) {
      if (args.size() > 0) {
        DecoderAutomata::EncodedData& decode_args = args.back();
        decode_args.end_keyframe = kfi;
        decode_args.keyframes.push_back(kfi);
      }
      args.emplace_back();
      {
        DecoderAutomata::EncodedData &decode_args = args.back();
        decode_args.width = video_index.frame_width();
        decode_args.height = video_index.frame_height();
        decode_args.start_keyframe = kfi;
      }
      printf("new args %d\n", kfi);
    }
    DecoderAutomata::EncodedData& decode_args = args.back();
    for (uint64_t r = kfi; r < kfi_end; r++) {
      decode_args.valid_frames.push_back(r);
      decode_args.sample_offsets.push_back(video_index.sample_offsets().at(r));
      decode_args.sample_sizes.push_back(video_index.sample_sizes().at(r));
    }
    printf("valid frames %d-%d\n", kfi, kfi_end);
    decode_args.keyframes.push_back(kfi);
    decode_args.encoded_video = video_bytes;

    last_endpoint = video_index.sample_offsets().at(kfi_end - 1) +
                    video_index.sample_sizes().at(kfi_end - 1);
  }
  {
    DecoderAutomata::EncodedData& decode_args = args.back();
    decode_args.end_keyframe = video_index.frames();
    decode_args.keyframes.push_back(video_index.frames());
  }

  decoder->initialize(args, video_index.metadata_bytes());

  for (auto& arg : args) {
    std::vector<uint8_t> frame_buffer(video_index.frame_width() *
                                      video_index.frame_height() * 3 *
                                      arg.valid_frames.size());
    decoder->get_frames(frame_buffer.data(), arg.valid_frames.size());
  }

  delete decoder;
}
#endif

// TEST(DecoderAutomata, GetStridedFrames) {
//   std::vector<TestVideoInfo> videos = {test_video_fragmented,
//                                        test_video_unfragmented};

//   avcodec_register_all();

//   // Load test data
//   std::vector<uint8_t> video_bytes =
//       read_entire_file(download_video(videos[0]));

//   // Create video index
//   MP4IndexCreator indexer(video_bytes.size());
//   uint64_t current_offset = 0;
//   uint64_t size_to_read = std::min((size_t)1024, video_bytes.size());
//   while (!indexer.is_done()) {
//     indexer.feed(video_bytes.data() + current_offset, size_to_read,
//                  current_offset, size_to_read);
//   }
//   ASSERT_FALSE(indexer.is_error());
//   VideoIndex video_index = indexer.get_video_index();

//   // Create decoder
//   VideoDecoderType decoder_type = VideoDecoderType::SOFTWARE;
//   DeviceHandle device = CPU_DEVICE;
//   DecoderAutomata* decoder = new DecoderAutomata(device, 1, decoder_type);

//   // Grab frames
//   std::vector<DecoderAutomata::EncodedData> args;
//   args.emplace_back();
//   DecoderAutomata::EncodedData& decode_args = args.back();
//   decode_args.width = video_index.frame_width();
//   decode_args.height = video_index.frame_height();
//   decode_args.start_keyframe = 0;
//   decode_args.end_keyframe = video_index.frames();
//   for (uint64_t r = 0; r < video_index.frames(); r += 2) {
//     decode_args.valid_frames.push_back(r);
//   }
//   for (uint64_t k : video_index.keyframe_indices()) {
//     decode_args.keyframes.push_back(k);
//   }
//   for (uint64_t k : video_index.sample_offsets()) {
//     decode_args.sample_offsets.push_back(k);
//   }
//   decode_args.encoded_video = video_bytes;
//   decoder->initialize(args);

//   std::vector<uint8_t> frame_buffer(video_index.frame_width() *
//                                     video_index.frame_height() * 3);
//   for (int64_t i = 0; i < video_index.frames() / 2; ++i) {
//     decoder->get_frames(frame_buffer.data(), 1);
//   }

//   delete decoder;
// }

}
