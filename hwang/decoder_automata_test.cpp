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

namespace {

std::vector<DecoderAutomata::EncodedData> get_all_frames(const VideoIndex& video_index, const std::vector<uint8_t>& video_bytes) {
  std::vector<DecoderAutomata::EncodedData> args;
  uint64_t last_endpoint = 1000000;
  // Walk by keyframes
  auto kf_indices = video_index.keyframe_indices();
  kf_indices.push_back(video_index.frames());
  for (uint64_t kf = 0; kf < kf_indices.size() - 1; ++kf) {
    uint64_t kfi = kf_indices.at(kf);
    uint64_t kfi_end = kf_indices.at(kf + 1);
    // printf("last endpoint %lu, offset %lu\n", last_endpoint,
    //        video_index.sample_offsets().at(kfi));
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
      // printf("new args %d\n", kfi);
    }
    DecoderAutomata::EncodedData& decode_args = args.back();
    for (uint64_t r = kfi; r < kfi_end; r++) {
      decode_args.valid_frames.push_back(r);
      decode_args.sample_offsets.push_back(video_index.sample_offsets().at(r));
      decode_args.sample_sizes.push_back(video_index.sample_sizes().at(r));
    }
    // printf("valid frames %d-%d\n", kfi, kfi_end);
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
  return args;
}

std::vector<DecoderAutomata::EncodedData> get_strided_range_frames(const VideoIndex& video_index,
                                                                   const std::vector<uint8_t>& video_bytes,
                                                                   const std::vector<uint64_t>& desired_frames) {
  std::vector<DecoderAutomata::EncodedData> args;
  // Walk by keyframes
  auto keyframe_indices = video_index.keyframe_indices();
  auto sample_offsets = video_index.sample_offsets();
  auto sample_sizes = video_index.sample_sizes();
  keyframe_indices.push_back(video_index.frames());

  VideoIntervals intervals =
      slice_into_video_intervals(video_index, desired_frames);
  size_t num_intervals = intervals.valid_frames.size();
  for (size_t i = 0; i < num_intervals; ++i) {
    size_t start_index;
    size_t end_index;
    std::tie(start_index, end_index) =
        intervals.sample_index_intervals[i];

    uint64_t start_keyframe = start_index;
    uint64_t end_keyframe = end_index;

    uint64_t start_keyframe_byte_offset =
        static_cast<uint64_t>(sample_offsets[start_keyframe]);
    uint64_t end_keyframe_byte_offset =
        static_cast<uint64_t>(sample_offsets[end_keyframe]);

    uint64_t start_keyframe_index;
    for (size_t i = 0; i <= keyframe_indices.size(); ++i) {
      if (keyframe_indices[i] == start_keyframe) {
        start_keyframe_index = i;
        break;
      }
    }
    uint64_t end_keyframe_index;
    for (size_t i = start_keyframe_index; i <= keyframe_indices.size(); ++i) {
      if (keyframe_indices[i] == end_keyframe) {
        end_keyframe_index = i;
        break;
      }
    }

    std::vector<uint64_t> all_keyframes;
    std::vector<uint64_t> all_keyframe_indices;
    for (size_t i = start_keyframe_index; i <= end_keyframe_index; ++i) {
      all_keyframes.push_back(keyframe_indices[i]);
      all_keyframe_indices.push_back(keyframe_indices[i] - keyframe_indices[0]);
    }

    std::vector<uint64_t> all_offsets;
    std::vector<uint64_t> all_sizes;
    for (size_t i = start_keyframe; i <= end_keyframe; ++i) {
      all_offsets.push_back(sample_offsets[i]);
      all_sizes.push_back(sample_sizes[i]);
    }

    args.emplace_back();
    DecoderAutomata::EncodedData& decode_args = args.back();
    decode_args.width = video_index.frame_width();
    decode_args.height = video_index.frame_height();
    // We add the start frame of this item to all frames since the decoder
    // works in terms of absolute frame numbers, instead of item relative
    // frame numbers
    decode_args.start_keyframe = keyframe_indices[start_keyframe_index];
    decode_args.end_keyframe = keyframe_indices[end_keyframe_index];
    decode_args.keyframes = all_keyframes;
    decode_args.sample_offsets = all_offsets;
    decode_args.sample_sizes = all_sizes;
    decode_args.valid_frames = intervals.valid_frames[i];
    decode_args.encoded_video = video_bytes;
  }
  return args;
}
}

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
  std::vector<DecoderAutomata::EncodedData> args = get_all_frames(video_index, video_bytes);
  decoder->initialize(args, video_index.metadata_bytes());

  for (auto& arg : args) {
    std::vector<uint8_t> frame_buffer(video_index.frame_width() *
                                      video_index.frame_height() * 3 *
                                      arg.valid_frames.size());
    decoder->get_frames(frame_buffer.data(), arg.valid_frames.size());
  }

  delete decoder;
}

TEST(DecoderAutomata, GetStridedRangesFrames) {
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
  std::vector<uint64_t> desired_frames;
  for (uint64_t i = 0; i < 10; ++i) {
    desired_frames.push_back(i);
  }
  for (uint64_t i = 100; i < 115; ++i) {
    desired_frames.push_back(i);
  }
  for (uint64_t i = 300; i < 450; ++i) {
    desired_frames.push_back(i);
  }
  for (uint64_t i = 700; i < 900; ++i) {
    desired_frames.push_back(i);
  }

  std::vector<DecoderAutomata::EncodedData> args = get_strided_range_frames(video_index, video_bytes, desired_frames);
  decoder->initialize(args, video_index.metadata_bytes());

  for (auto& arg : args) {
    std::vector<uint8_t> frame_buffer(video_index.frame_width() *
                                      video_index.frame_height() * 3 *
                                      arg.valid_frames.size());
    decoder->get_frames(frame_buffer.data(), arg.valid_frames.size());
  }

  delete decoder;
}


TEST(DecoderAutomata, GatherFramesComparison) {
  av_log_set_level(AV_LOG_TRACE);

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
  int target_frame = 500;
  // Get 500th frame from direct decoding
  std::vector<uint8_t> all_frame_buffer(video_index.frame_width() *
                                        video_index.frame_height() * 3);
  {
    DecoderAutomata* decoder = new DecoderAutomata(device, 1, decoder_type);
    std::vector<DecoderAutomata::EncodedData> args =
        get_all_frames(video_index, video_bytes);
    decoder->initialize(args, video_index.metadata_bytes());

    int frame = 0;
    while (frame <= target_frame) {
      for (auto &arg : args) {
        for (int i = 0; i < arg.valid_frames.size(); i++) {
          int num_frames = 1;
          decoder->get_frames(all_frame_buffer.data(), num_frames);
          frame++;
          if (frame - 1 == target_frame) {
            break;
          };
        }
        if (frame - 1 == target_frame) {
          break;
        };
      }
    }
    delete decoder;
  }

  // Grab frames
  std::vector<uint8_t> gather_frame_buffer(video_index.frame_width() *
                                           video_index.frame_height() * 3);
  {
    DecoderAutomata* decoder = new DecoderAutomata(device, 1, decoder_type);
    std::vector<uint64_t> desired_frames = {target_frame};
    std::vector<DecoderAutomata::EncodedData> args =
        get_strided_range_frames(video_index, video_bytes, desired_frames);
    decoder->initialize(args, video_index.metadata_bytes());

    for (auto &arg : args) {
      for (int i = 0; i < arg.valid_frames.size(); i += 1) {
        int num_frames = 1;
        decoder->get_frames(gather_frame_buffer.data(), num_frames);
      }
    }
    delete decoder;
  }

  for (int i = 0; i < all_frame_buffer.size(); ++i) {
    ASSERT_TRUE(all_frame_buffer[i] == gather_frame_buffer[i]);
  }

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
  std::vector<DecoderAutomata::EncodedData> args = get_all_frames(video_index, video_bytes);
  decoder->initialize(args, video_index.metadata_bytes());

  for (auto& arg : args) {
    std::vector<uint8_t> frame_buffer(video_index.frame_width() *
                                      video_index.frame_height() * 3 *
                                      arg.valid_frames.size());
    decoder->get_frames(frame_buffer.data(), arg.valid_frames.size());
  }

  delete decoder;
}

TEST(DecoderAutomata, GetStridedRangesFramesGPU) {
  av_log_set_level(AV_LOG_TRACE);

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
  std::vector<uint64_t> desired_frames;
  for (uint64_t i = 0; i < 10; ++i) {
    desired_frames.push_back(i);
  }
  for (uint64_t i = 100; i < 115; ++i) {
    desired_frames.push_back(i);
  }
  for (uint64_t i = 300; i < 450; ++i) {
    desired_frames.push_back(i);
  }
  for (uint64_t i = 700; i < 900; ++i) {
    desired_frames.push_back(i);
  }

  std::vector<DecoderAutomata::EncodedData> args = get_strided_range_frames(
      video_index, video_bytes, desired_frames);
  decoder->initialize(args, video_index.metadata_bytes());

  for (auto& arg : args) {
    std::vector<uint8_t> frame_buffer(video_index.frame_width() *
                                      video_index.frame_height() * 3 *
                                      arg.valid_frames.size());
    for (int i = 0; i < arg.valid_frames.size(); i += 8) {
      int num_frames = std::min((size_t)8, arg.valid_frames.size() - i);
      decoder->get_frames(frame_buffer.data(), num_frames);
    }
  }

  delete decoder;
}

TEST(DecoderAutomata, GatherFramesComparisonGPU) {
  av_log_set_level(AV_LOG_TRACE);

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

  int target_frame = 500;
  // Get 500th frame from direct decoding
  std::vector<uint8_t> all_frame_buffer(video_index.frame_width() *
                                        video_index.frame_height() * 3);
  {
    DecoderAutomata* decoder = new DecoderAutomata(device, 1, decoder_type);
    std::vector<DecoderAutomata::EncodedData> args =
        get_all_frames(video_index, video_bytes);
    decoder->initialize(args, video_index.metadata_bytes());

    int frame = 0;
    while (frame <= target_frame) {
      for (auto &arg : args) {
        for (int i = 0; i < arg.valid_frames.size(); i++) {
          int num_frames = 1;
          decoder->get_frames(all_frame_buffer.data(), num_frames);
          frame++;
          if (frame - 1 == target_frame) {
            break;
          };
        }
        if (frame - 1 == target_frame) {
          break;
        };
      }
    }
    delete decoder;
  }

  // Grab frames
  std::vector<uint8_t> gather_frame_buffer(video_index.frame_width() *
                                           video_index.frame_height() * 3);
  {
    DecoderAutomata* decoder = new DecoderAutomata(device, 1, decoder_type);
    std::vector<uint64_t> desired_frames = {target_frame};
    std::vector<DecoderAutomata::EncodedData> args =
        get_strided_range_frames(video_index, video_bytes, desired_frames);
    decoder->initialize(args, video_index.metadata_bytes());

    for (auto &arg : args) {
      for (int i = 0; i < arg.valid_frames.size(); i += 1) {
        int num_frames = 1;
        decoder->get_frames(gather_frame_buffer.data(), num_frames);
      }
    }
    delete decoder;
  }

  for (int i = 0; i < all_frame_buffer.size(); ++i) {
    ASSERT_TRUE(all_frame_buffer[i] == gather_frame_buffer[i]);
  }

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
