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
#include "hwang/util/mp4.h"
#include "hwang/util/bits.h"

#include <thread>

#include <cassert>
#include <iostream>

namespace hwang {

MP4IndexCreator::MP4IndexCreator(uint64_t file_size)
    : file_size_(file_size), done_(false), error_(false) {
}

bool MP4IndexCreator::feed(uint8_t* data, size_t size,
                           uint64_t& next_offset,
                           uint64_t& next_size) {
  // 1.  Search for 'ftype' container to ensure this is a proper mp4 file
  // 2.  Search for the 'moov' container
  // 2a. Fail if there is a 'mvex' box, since we don't support movie fragments
  // 3.  Search for 'trak' containers to find the video track
  // 4.  Search for 'mdia' to find media information for track
  // 5.  Search for 'hdlr' to determine if track is a video track by checking
  //     'handler_type' == 'vide', otherwise go to step 3
  // 6.  Search for 'dinf'
  // 7.  Search for 'dref' to check data is in this file by checking entry_flags
  // 8.  Search for 'stbl' Sample Table Box

  // Note: 9 - 10 only needed if we want finer access than random access points
  // 9.  Search for 'stts' Time To Sample Box to use for determining decode
  //     order
  // 10.  Search for 'ctts' Composition Offset Box to use for precise ordering
  //      of frames
  // End note.

  // TODO(apoms): Make sure the data is in h264 format by parsing stsd

  // 11.  Search for 'stsz' or 'stz2' Sample Size Box to determine number and
  //     size of samples.
  // 12. Search for 'stsc' Sample To Chunk Box to determine which chunk a sample
  //     is in.
  // 13. Search for 'stco' or 'co64' Chunk Offset Box to determine the chunk
  //     byte offsets in the file.
  // 14. Search for 'stss' Sync Sample Box for location of random access points.
  //     If missing, then all samples are randoma access points

  GetBitsState bs;
  bs.buffer = data;
  bs.offset = 0;
  bs.size = size;

  auto type = [](const std::string &s) { return string_to_type(s); };
  auto size_left = [&]() { return bs.size - (bs.offset / 8); };

#define MORE_DATA(__offset, __size)                                            \
  if (__offset + __size > file_size_) {                                        \
    error_message_ = "EOF in middle of box";                                   \
    std::cerr << error_message_ << std::endl;                                  \
    done_ = true;                                                              \
    error_ = true;                                                             \
    return false;                                                              \
  }                                                                            \
  next_offset = __offset;                                                      \
  next_size = __size;                                                          \
  return true;

#define MORE_DATA_LIMIT(__offset, __size)                                      \
  {                                                                            \
    uint64_t __size2 = __size;                                                 \
    if (__offset + __size2 > file_size_) {                                     \
      __size2 = file_size_ - __offset;                                         \
      if (__size2 == 0) {                                                      \
        error_message_ = "Reached EOF without being done";                     \
        std::cerr << error_message_ << std::endl;                              \
        done_ = true;                                                          \
        error_ = true;                                                         \
        return false;                                                          \
      }                                                                        \
    }                                                                          \
    next_offset = __offset;                                                    \
    next_size = __size2;                                                       \
  }                                                                            \
  return true;

  auto search_for_box = [](GetBitsState& bs, uint32_t type,
                           std::function<void(GetBitsState &)> fn) {
    while ((bs.offset / 8) < bs.size) {
      FullBox b = probe_box_type(bs);
      printf("looking for %s, parsed box type: %s, parsed size: %ld, "
             "offset: %ld, size: %ld\n",
             type_to_string(type).c_str(),
             type_to_string(b.type).c_str(),
             b.size,
             bs.offset / 8,
             bs.size);
      if (b.type == type) {
        GetBitsState bs2 = bs;
        fn(bs2);
        bs.offset += b.size * 8;
        return true;
      } else {
        // Skip ahead by the size
        bs.offset += b.size * 8;
      }
    }
    return false;
  };

  while ((bs.offset / 8) < bs.size && !done_) {
    // Are we done?
    if (parsed_ftyp_ && parsed_moov_) {
      done_ = true;
      return false;
    }

    // Get type of next box
    FullBox b = probe_box_type(bs);
    printf("parsed box type: %s, size: %ld, bs off %ld, size %ld\n",
           type_to_string(b.type).c_str(),
           b.size,
           bs.offset / 8,
           bs.size
           );
    assert(b.size != 0);
    if (b.type == type("ftyp")) {
      if (size_left() < b.size) {
        // Get more data since we don't have this entire box
        MORE_DATA(offset_, b.size);
      }
      FileTypeBox ftyp = parse_ftyp(bs);
      bool supporting = false;
      for (auto &c : ftyp.compatible_brands) {
        if (c == string_to_type("isom") || c == string_to_type("iso2") ||
            c == string_to_type("avc1")) {
          supporting = true;
        }
      }
      if (!supporting) {
        std::string brands;
        for (auto &c : ftyp.compatible_brands) {
          brands += type_to_string(c) + ", ";
        }
        std::string error = "No supported mp4 brands: " + brands;
        std::cerr << error << std::endl;
        error_message_ = error;
        error_ = true;
        done_ = true;
        return false;
      }
      parsed_ftyp_ = true;
    } else if (b.type == type("moov")) {
      if (size_left() < b.size) {
        // Get more data since we don't have this entire box
        MORE_DATA(offset_, b.size);
      }
      //  Search for 'trak' containers to find the video track
      uint64_t before_moov_offset = bs.offset / 8;
      GetBitsState moov_bs = restrict_bits_to_box(bs);
      FullBox moov = parse_moov(moov_bs);
      bool found_valid_trak = false;
      uint64_t trak_offset = 0;
      while ((moov_bs.offset / 8) < moov_bs.size) {
        uint32_t trak_type = 0;
        auto trak_verify_fn = [&](GetBitsState &bs) {
          trak_offset = bs.offset;
          GetBitsState trak_bs = restrict_bits_to_box(bs);
          FullBox trak = parse_trak(trak_bs);
          // Search for mdia trak
          auto mdia_verify_fn = [&](GetBitsState &bs) {
            GetBitsState mdia_bs = restrict_bits_to_box(bs);
            FullBox mdia = parse_mdia(mdia_bs);
            // Search for hdlr trak
            auto hdlr_verify_fn = [&](GetBitsState &bs) {
              HandlerBox hdlr = parse_hdlr(bs);
              trak_type = hdlr.handler_type;
            };
            return search_for_box(mdia_bs, type("hdlr"), hdlr_verify_fn);
          };
          return search_for_box(trak_bs, type("mdia"), mdia_verify_fn);
        };
        bool found_trak = search_for_box(moov_bs, type("trak"), trak_verify_fn);
        if (!found_trak) {
          std::string error = "Could not find a trak box";
          std::cerr << error << std::endl;
          error_message_ = error;
          error_ = true;
          done_ = true;
          return false;
        }

        if (trak_type == type("vide")) {
          found_valid_trak = true;
          break;
        }
      }
      if (!found_valid_trak) {
        std::string error = "Could not find a video trak file";
        std::cerr << error << std::endl;
        error_message_ = error;
        error_ = true;
        done_ = true;
        return false;
      }

      // 11.  Search for 'stsz' or 'stz2' Sample Size Box to determine number
      // and
      //     size of samples.
      // 12. Search for 'stsc' Sample To Chunk Box to determine which chunk a
      // sample
      //     is in.
      // 13. Search for 'stco' or 'co64' Chunk Offset Box to determine the chunk
      //     byte offsets in the file.
      // 14. Search for 'stss' Sync Sample Box for location of random access
      // points.
      //     If missing, then all samples are randoma access points

      printf("old offset %ld, before moov %ld\n", bs.offset, before_moov_offset);
      bs.offset = (before_moov_offset + moov.size) * 8;
      printf("new offset %ld\n", bs.offset);
      parsed_moov_ = true;
    } else {
      // If not a box we are interested in, skip to next box
      // TODO(apoms): If we have enough data, just go to the next box using
      // the current bits. Right now we are assuming the data is too large

      // Jump to start of next box
      offset_ += b.size;

      MORE_DATA_LIMIT(offset_, 1024);
    }
    offset_ += b.size;
  }
  if (done_) {
    return false;
  }

  offset_ += bs.size;
  MORE_DATA_LIMIT(offset_, 1024);

  return true;
}

} // namespace hwang
