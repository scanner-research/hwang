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
#include <cstring>

namespace hwang {

MP4IndexCreator::MP4IndexCreator(uint64_t file_size)
    : file_size_(file_size), done_(false), error_(false) {
}

bool MP4IndexCreator::feed(const uint8_t* data, size_t size,
                           uint64_t& next_offset,
                           uint64_t& next_size) {
  const bool PRINT_DEBUG = false;
  // 1.  Search for 'ftype' container to ensure this is a proper mp4 file
  // 2.  Search for the 'moov' container
  // 2a. If there is a 'mvex' box, handle movie fragments
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


  // 11.  Search for 'stsz' or 'stz2' Sample Size Box to determine number and
  //     size of samples.
  // 12. Search for 'stsc' Sample To Chunk Box to determine which chunk a sample
  //     is in.
  // 13. Search for 'stco' or 'co64' Chunk Offset Box to determine the chunk
  //     byte offsets in the file.
  // 14. Search for 'stss' Sync Sample Box for location of random access points.
  //     If missing, then all samples are randoma access points

  // TODO(apoms): Make sure the data is in h264 format by parsing stsd
  // 16.  Search for 'stsd' to get avc metadata
  //
  // 17. If 'mvex' was specified, then search for all 'moofs'

  GetBitsState bs;
  bs.buffer = data;
  bs.offset = 0;
  bs.size = size;

  if (PRINT_DEBUG) {
    printf("first bs size: %lu\n", bs.size);
  }

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
        if (parsed_ftyp_ && parsed_moov_ && fragments_present_) {              \
          "We finished searching for moofs";                                   \
          done_ = true;                                                        \
          return false;                                                        \
        }                                                                      \
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

  auto search_for_box = [](GetBitsState &bs, uint32_t type,
                           std::function<bool(GetBitsState &)> fn) {
    while ((bs.offset / 8) < bs.size) {
      FullBox b = probe_box_type(bs);
      if (PRINT_DEBUG) {
        printf("looking for %s, parsed box type: %s, parsed size: %ld, "
               "offset: %ld, size: %ld\n",
               type_to_string(type).c_str(), type_to_string(b.type).c_str(),
               b.size, bs.offset / 8, bs.size);
      }
      if (b.type == type) {
        GetBitsState bs2 = bs;
        bool result = fn(bs2);
        bs.offset += b.size * 8;
        return result;
      } else {
        // Skip ahead by the size
        bs.offset += b.size * 8;
      }
    }
    return false;
  };

  while ((bs.offset / 8) < bs.size && !is_done()) {
    // Get type of next box
    FullBox b = probe_box_type(bs);
    assert(b.size != 0);
    if (PRINT_DEBUG) {
      printf("parsed box type: %s, size: %ld, bs off %ld, size %ld\n",
             type_to_string(b.type).c_str(), b.size, bs.offset / 8, bs.size);
      printf("box type %s, size %lu\n", type_to_string(b.type).c_str(), b.size);
    }
    if (!parsed_ftyp_ && b.type == type("ftyp")) {
      if (size_left() < b.size) {
        // Get more data since we don't have this entire box
        MORE_DATA(offset_, b.size);
      }
      FileTypeBox ftyp = parse_ftyp(bs);
      bool supporting = false;
      for (auto &c : ftyp.compatible_brands) {
        //printf("brand: %s\n", type_to_string(c).c_str());
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
    } else if (!parsed_moov_ && b.type == type("moov")) {
      if (size_left() < b.size) {
        // Get more data since we don't have this entire box
        MORE_DATA(offset_, b.size);
      }
      //  Search for 'trak' containers to find the video track
      uint64_t before_moov_offset = bs.offset / 8;
      GetBitsState orig_moov_bs = restrict_bits_to_box(bs);
      GetBitsState moov_bs = orig_moov_bs;
      FullBox moov = parse_moov(moov_bs);
      bool found_valid_trak = false;
      uint64_t trak_offset = 0;

      MediaHeaderBox mdhd;
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
            GetBitsState mdia_bs2 = mdia_bs;
            // Search for mdia trak
            auto mdhd_verify_fn = [&](GetBitsState &bs) {
              mdhd = parse_mdhd(bs);
              return true;
            };
            // Search for hdlr trak
            auto hdlr_verify_fn = [&](GetBitsState &bs) {
              HandlerBox hdlr = parse_hdlr(bs);
              trak_type = hdlr.handler_type;
              return true;
            };
            return
                search_for_box(mdia_bs, type("mdhd"), mdhd_verify_fn) &&
                search_for_box(mdia_bs2, type("hdlr"), hdlr_verify_fn);
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

      // Compute fps from timebase and duration
      timescale_ = mdhd.timescale;
      duration_ = mdhd.duration;

      // Excavate information from 'stbl' sample table box for the video trak
      {
        GetBitsState moov_bs = orig_moov_bs;
        moov_bs.offset = trak_offset;

        GetBitsState trak_bs = restrict_bits_to_box(moov_bs);
        FullBox trak = parse_trak(trak_bs);
        // Search for mdia
        bool parsed_stbl = search_for_box(trak_bs, type("mdia"), [&](GetBitsState &bs) {
          GetBitsState mdia_bs = restrict_bits_to_box(bs);
          FullBox mdia = parse_mdia(mdia_bs);
          // Search for minf
          return search_for_box(mdia_bs, type("minf"), [&](GetBitsState &bs) {
            GetBitsState minf_bs = restrict_bits_to_box(bs);
            FullBox minf = parse_minf(minf_bs);
            // Search for stbl
            return search_for_box(
                minf_bs, type("stbl"), [&](GetBitsState &stbl_bs) {
                  // Search for 'stsz' or 'stz2' Sample Size Box to determine
                  // number and size of samples.
                  FullBox stbl = parse_stbl(stbl_bs);

                  SampleSizeBox sample_size_box;

                  bool found_stsz;
                  {
                    GetBitsState bs = stbl_bs;
                    found_stsz =
                        search_for_box(bs, type("stsz"), [&](GetBitsState &bs) {
                          sample_size_box = parse_stsz(bs);
                          return true;
                        });
                  }
                  bool found_stz2;
                  {
                    GetBitsState bs = stbl_bs;
                    found_stz2 =
                        search_for_box(bs, type("stz2"), [&](GetBitsState &bs) {

                          sample_size_box = parse_stz2(bs);
                          return true;
                        });
                  }

                  if (!(found_stsz || found_stz2)) {
                    std::string error = "Could not find 'stsz' or 'stz2'";
                    std::cerr << error << std::endl;
                    error_message_ = error;
                    error_ = true;
                    done_ = true;
                    return false;
                  }

                  std::vector<uint64_t> sample_sizes;
                  for (int i = 0; i < sample_size_box.sample_count; ++i) {
                    if (sample_size_box.sample_size == 0) {
                      sample_sizes.push_back(sample_size_box.entry_size[i]);
                    } else {
                      sample_sizes.push_back(sample_size_box.sample_size);
                    }
                  }

                  // Search for 'stsc' Sample To Chunk Box to determine which
                  // chunk a sample is in.

                  std::vector<uint64_t> sample_chunk_assignment;
                  {
                    GetBitsState bs = stbl_bs;
                    bool found_stsc =
                        search_for_box(bs, type("stsc"), [&](GetBitsState &bs) {
                          SampleToChunkBox stsc =
                              parse_stsc(bs, sample_sizes.size());
                          for (size_t i = 0; i < stsc.chunk_entries.size();
                               ++i) {
                            uint64_t chunk_index = i + 1;
                            for (size_t j = 0;
                                 j < stsc.chunk_entries[i].num_samples; ++j) {
                              sample_chunk_assignment.push_back(chunk_index);
                            }
                          }
                          return true;
                        });

                    if (!found_stsc) {
                      std::string error = "Could not find 'stsc'";
                      std::cerr << error << std::endl;
                      error_message_ = error;
                      error_ = true;
                      done_ = true;
                      return false;
                    }
                  }

                  assert(sample_chunk_assignment.size() == sample_sizes.size());

                  // Search for 'stco' or 'co64' Chunk Offset Box to determine
                  // the chunk byte offsets in the file.

                  std::vector<uint64_t> sample_offsets;
                  {
                    ChunkOffsetBox chunk_offset_box;
                    bool found_stco;
                    {
                      GetBitsState bs = stbl_bs;
                      found_stco = search_for_box(
                          bs, type("stco"), [&](GetBitsState &bs) {
                            chunk_offset_box = parse_stco(bs);
                            return true;
                          });
                    }
                    bool found_co64;
                    {
                      GetBitsState bs = stbl_bs;
                      found_co64 = search_for_box(
                          bs, type("co64"), [&](GetBitsState &bs) {
                            chunk_offset_box = parse_co64(bs);
                            return true;
                          });
                    }

                    if (!(found_stco || found_co64)) {
                      std::string error = "Could not find 'stco' or 'co64'";
                      std::cerr << error << std::endl;
                      error_message_ = error;
                      error_ = true;
                      done_ = true;
                      return false;
                    }

                    uint64_t current_offset = 0;
                    uint64_t current_chunk_index = 1;
                    if (chunk_offset_box.chunk_offsets.size() > 0) {
                      current_offset = chunk_offset_box.chunk_offsets[0];
                    }
                    for (size_t i = 0; i < sample_sizes.size(); ++i) {
                      // Determine if this sample is in this chunk
                      if (sample_chunk_assignment[i] != current_chunk_index) {
                        assert(current_chunk_index <
                               chunk_offset_box.chunk_offsets.size());
                        current_offset =
                            chunk_offset_box
                                .chunk_offsets[++current_chunk_index - 1];
                      }
                      sample_offsets.push_back(current_offset);
                      // printf("chunk %ld (%lu), sample %lu, sample offset %lu\n",
                      //        current_chunk_index,
                      //        chunk_offset_box.chunk_offsets.size(),
                      //        i,
                      //        current_offset);
                      current_offset += sample_sizes[i];
                    }
                  }
                  assert(sample_offsets.size() == sample_sizes.size());

                  // Search for 'stss' Sync Sample Box for location of random
                  // access points. If missing, then all samples are randoma
                  // access points
                  std::vector<uint64_t> keyframe_indices;
                  {
                    GetBitsState bs = stbl_bs;
                    bool found_stss =
                        search_for_box(bs, type("stss"), [&](GetBitsState &bs) {
                          SyncSampleBox stss = parse_stss(bs);
                          for (size_t i = 0; i < stss.sample_number.size();
                               ++i) {
                            keyframe_indices.push_back(stss.sample_number[i] - 1);
                          }
                          return true;
                        });

                    if (!found_stss) {
                      for (size_t i = 0; i < sample_sizes.size(); ++i) {
                        keyframe_indices.push_back(sample_offsets_.size() + i);
                      }
                    }
                  }

                  int16_t width;
                  int16_t height;
                  std::string format;
                  std::vector<uint8_t> extradata;
                  {
                    GetBitsState bs = stbl_bs;
                    bool found_stsd =
                        search_for_box(bs, type("stsd"), [&](GetBitsState &bs) {
                          GetBitsState stsd_bs = restrict_bits_to_box(bs);
                          SampleDescriptionBox stsd = parse_stsd(stsd_bs);
                          for (size_t i = 0; i < stsd.entry_count; ++i) {
                            GetBitsState vs_bs = restrict_bits_to_box(stsd_bs);
                            VisualSampleEntry vs =
                                parse_visual_sample_entry(vs_bs);
                            width = vs.width;
                            height = vs.height;
                            format = type_to_string(vs.type);
                            if (PRINT_DEBUG) {
                              printf("visual sample entry type %s\n",
                                     format.c_str());
                            }
                            if (vs_bs.offset / 8 <
                                stsd_bs.offset / 8 + vs.size) {
                              if (vs.type == string_to_type("avc1")) {
                                GetBitsState vs_bs2 = vs_bs;
                                FullBox b2 = parse_box(vs_bs2);
                                if (b2.type == string_to_type("avcC")) {
                                  size_t size = b2.size - (vs_bs2.offset / 8 -
                                                           vs_bs.offset / 8);
                                  extradata.resize(size);
                                  memcpy(extradata.data(),
                                         vs_bs2.buffer + vs_bs2.offset / 8,
                                         size);
                                }
                              } else if (vs.type == string_to_type("hev1")) {
                                GetBitsState vs_bs2 = vs_bs;
                                FullBox b2 = parse_box(vs_bs2);
                                if (b2.type == string_to_type("hvcC")) {
                                  size_t size = b2.size - (vs_bs2.offset / 8 -
                                                           vs_bs.offset / 8);
                                  extradata.resize(size);
                                  memcpy(extradata.data(),
                                         vs_bs2.buffer + vs_bs2.offset / 8,
                                         size);
                                }
                                if (PRINT_DEBUG) {
                                  printf("extradata size %lu\n",
                                         extradata.size());
                                }
                              }
                            }
                            stsd_bs.offset += vs.size * 8;
                          }
                          return true;
                        });

                    if (!found_stsd) {
                      std::string error = "Could not find 'stsd'";
                      std::cerr << error << std::endl;
                      error_message_ = error;
                      error_ = true;
                      done_ = true;
                      return false;
                    }
                  }

                  width_ = width;
                  height_ = height;
                  format_ = format;

                  for (size_t i = 0; i < sample_sizes.size(); ++i) {
                    sample_offsets_.push_back(sample_offsets[i]);
                    sample_sizes_.push_back(sample_sizes[i]);
                  }
                  for (uint64_t ki : keyframe_indices) {
                    keyframe_indices_.push_back(ki);
                  }

                  extradata_ = extradata;

                  return true;
                });
          });
        });
        if (!parsed_stbl) {
          std::string error = "Could not parse 'stbl' correctly";
          std::cerr << error << std::endl;
          if (!error_) {
            error_message_ = error;
            error_ = true;
          }
          done_ = true;
          return false;
        }
      }

      // Search for mvex to know if fragments are present
      {
        GetBitsState moov_bs = orig_moov_bs;
        (void)parse_moov(moov_bs);
        fragments_present_ =
            search_for_box(moov_bs, type("mvex"), [&](GetBitsState &bs) {
              // Search for trex
              GetBitsState mvex_bs = restrict_bits_to_box(bs);
              FullBox mvex = parse_mvex(mvex_bs);
              while (mvex_bs.offset / 8 < mvex_bs.size) {
                (void)search_for_box(
                    mvex_bs, type("trex"), [&](GetBitsState &bs) {
                      track_extends_boxes_.push_back(parse_trex(bs));
                      return true;
                    });
              }
              // Search for leva and fail if found
              mvex_bs = restrict_bits_to_box(bs);
              mvex = parse_mvex(mvex_bs);
              while (mvex_bs.offset / 8 < mvex_bs.size) {
                (void)search_for_box(
                    mvex_bs, type("leva"), [&](GetBitsState &bs) {
                      std::cerr << "leva not supported" << std::endl;
                      exit(-1);
                      return false;
                    });
              }
              return true;
            });
      }

      bs.offset = (before_moov_offset + moov.size) * 8;
      parsed_moov_ = true;
    } else if (b.type == type("moof")) {
      if (size_left() < b.size) {
        // Get more data since we don't have this entire box
        MORE_DATA(offset_, b.size);
      }

      uint64_t before_moof_offset = bs.offset / 8;
      GetBitsState moof_bs = restrict_bits_to_box(bs);
      FullBox moof = parse_moof(moof_bs);

      std::vector<uint64_t> sample_offsets;
      std::vector<uint64_t> sample_sizes;
      std::vector<bool> keyframe_indicators;

      bool first_traf = true;
      uint64_t prev_traf_offset = 0;
      while ((moof_bs.offset / 8) < moof_bs.size) {
        //  Search for 'traf' containers
        bool traf_found =
            search_for_box(moof_bs, type("traf"), [&](GetBitsState &bs) {
              // Search for 'tfhd'
              GetBitsState orig_traf_bs = restrict_bits_to_box(bs);
              GetBitsState traf_bs = orig_traf_bs;
              FullBox traf = parse_traf(traf_bs);
              TrackFragmentHeaderBox tfhd;
              bool found_tfhd =
                  search_for_box(traf_bs, type("tfhd"), [&](GetBitsState &bs) {
                    tfhd = parse_tfhd(bs);
                    return true;
                  });
              if (!found_tfhd) {
                std::string error = "Could not find 'tfhd'";
                std::cerr << error << std::endl;
                if (!error_) {
                  error_message_ = error;
                  error_ = true;
                }
                done_ = true;
                return false;
              }

              uint64_t base_data_offset;
              switch (tfhd.base_offset_type) {
                case TrackFragmentHeaderBox::BaseOffsetType::PROVIDED: {
                  base_data_offset = tfhd.base_data_offset;
                  break;
                }
                case TrackFragmentHeaderBox::BaseOffsetType::IS_RELATIVE: {
                  if (first_traf) {
                    base_data_offset = offset_ + before_moof_offset;
                  } else {
                    base_data_offset = prev_traf_offset;
                  }
                  break;
                }
                case TrackFragmentHeaderBox::BaseOffsetType::IS_MOOF: {
                  base_data_offset = offset_ + before_moof_offset;
                  break;
                }
                default: {
                  exit(-1);
                  break;
                }
              }
              // Find trex from tfhd
              TrackExtendsBox trex;
              {
                bool found_trex = false;
                for (size_t i = 0; i < track_extends_boxes_.size(); ++i) {
                  if (track_extends_boxes_[i].track_ID == tfhd.track_ID) {
                    trex = track_extends_boxes_[i];
                    found_trex = true;
                  }
                }
                if (!found_trex) {
                  std::string error = "Could not find 'trex' for track id in 'tfhd'";
                  std::cerr << error << std::endl;
                  if (!error_) {
                    error_message_ = error;
                    error_ = true;
                  }
                  done_ = true;
                  return false;
                }
              }

              uint64_t prev_trun_offset = base_data_offset;

              // Search for 'trun' boxes
              traf_bs = orig_traf_bs;
              traf = parse_traf(traf_bs);
              while ((traf_bs.offset / 8) < traf_bs.size) {
                bool found_trun = search_for_box(
                    traf_bs, type("trun"), [&](GetBitsState &bs) {
                      TrackRunBox tr = parse_trun(bs);
                      // Use various defaults to determine the size and offset

                      // Determine data offset
                      uint64_t data_offset = base_data_offset;
                      // If data-offset-present use that
                      if (tr.data_offset_present()) {
                        // Data is relative to base-data-offset
                        data_offset = base_data_offset + tr.data_offset;
                      } else {
                        // Data starts at offset from previous run
                        data_offset = prev_trun_offset;
                      }

                      // Determine sample size
                      uint64_t base_size;
                      // If sample-size-present, use that
                      if (tr.sample_size_present()) {
                        size = 0;
                      }
                      // Try to grab default from tfhd
                      else if (tfhd.default_sample_size_present()) {
                        base_size = tfhd.default_sample_size;
                      }
                      // Grab default from trex
                      else {
                        base_size = trex.default_sample_size;
                      }

                      uint64_t current_offset = data_offset;
                      for (size_t i = 0; i < tr.samples.size(); ++i) {
                        const auto& sample = tr.samples[i];
                        uint64_t sample_size;
                        if (tr.sample_size_present()) {
                          sample_size = sample.sample_size;
                        } else {
                          sample_size = base_size;
                        }
                        uint32_t sample_flags;
                        if (tr.sample_flags_present()) {
                          // Sample flags in each sample
                          sample_flags = sample.sample_flags;
                        }
                        // First sample flags
                        else if (i == 0 && tr.first_sample_flags_present()) {
                          sample_flags = tr.first_sample_flags;
                        }
                        // Try to get values from tfhd
                        else if (tfhd.default_sample_flags_present()) {
                          sample_flags = tfhd.default_sample_flags;
                        }
                        // Get values from trex
                        else {
                          sample_flags = trex.default_sample_flags;
                        }
                        // keyframe is 15th bit == 0
                        bool is_keyframe = (sample_flags & 0x00010000) == 0;

                        sample_sizes.push_back(sample_size);
                        sample_offsets.push_back(current_offset);
                        keyframe_indicators.push_back(is_keyframe);

                        current_offset += sample_size;
                      }

                      prev_trun_offset = current_offset;

                      return true;
                    });
              }
              prev_traf_offset = prev_trun_offset;
              return found_tfhd;
            });
        first_traf = false;
        if (error_) {
          return false;
        }
      }
      assert(sample_offsets.size() == sample_sizes.size());
      // Append samples to sample list

      for (size_t i = 0; i < sample_sizes.size(); ++i) {
        if (keyframe_indicators[i]) {
          keyframe_indices_.push_back(sample_sizes_.size());
        }
        sample_offsets_.push_back(sample_offsets[i]);
        sample_sizes_.push_back(sample_sizes[i]);
      }


      bs.offset = (before_moof_offset + moof.size) * 8;
    } else {
      // If not a box we are interested in, skip to next box
      // TODO(apoms): If we have enough data, just go to the next box using
      // the current bits. Right now we are assuming the data is too large

      // Jump to start of next box
      offset_ += b.size;
      //printf("size %lu\n", b.size);

      MORE_DATA_LIMIT(offset_, 1024);
    }
    offset_ += b.size;
  }
  if (is_done()) {
    return false;
  }

  MORE_DATA_LIMIT(offset_, 1024);


  return true;
}


VideoIndex MP4IndexCreator::get_video_index() {
  return VideoIndex(timescale_, duration_, width_, height_, format_,
                    sample_offsets_, sample_sizes_, keyframe_indices_,
                    extradata_);
}

} // namespace hwang
