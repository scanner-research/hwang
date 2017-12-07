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
#include <fstream>

namespace hwang {

MP4IndexCreator::MP4IndexCreator(uint64_t file_size)
    : file_size_(file_size), done_(false), error_(false) {
}

bool MP4IndexCreator::feed(uint8_t* data, size_t size,
                           uint64_t& next_offset) {
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
  next_offset = __offset;                                                      \
  next_size = __size;                                                          \
  return true;

  while ((bs.offset / 8) < bs.size) {
    // Get type of next box
    FullBox b = probe_box_type(bs);
    printf("parsed box type: %s\n", type_to_string(b.type).c_str());
    switch (b.type) {
      case type("ftyp"): {
        if (size_left() < b.size) {
          // Get more data since we don't have this entire box
          MORE_DATA(offset_, b.size);
        }
        FileTypeBox ftyp = parse_ftyp(bs);
        if (!(ftyp.major_brand == string_to_type("isom") ||
              ftyp.major_brand == string_to_type("iso2") ||
              ftyp.major_brand == string_to_type("avc1"))) {

          std::string error = "Unsupported major brand in mp4: " +
                              type_to_string(ftyp.major_brand);
          std::cerr << error << std::endl;
          error_message_ = error;
          error_ = true;
          done_ = true;

          return false;
        }
        parse_ftyp_ = true;
        break;
      }
      case type("moov"): {
        if (size_left() < b.size) {
          // Get more data since we don't have this entire box
          MORE_DATA(offset_, b.size);
        }

        parse_moov_ = true;
        break;
      }
      default: {
        // If not a box we are interested in, skip to next box
        // TODO(apoms): If we have enough data, just go to the next box using
        // the current bits. Right now we are assuming the data is too large

        // Jump to start of next box
        offset_ += b.size;

        MORE_DATA(offset_, 1024);
        break;
      }
    }
  }
  // Are we done?
  if (parse_ftyp_ && parsed_moov_) {
    done_ = true;
    return false;
  }

  offset_ += bs.size;
  MORE_DATA(offset_, 1024);

  return true;
}

}
