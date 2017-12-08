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

#include "hwang/util/bits.h"

#include <vector>
#include <string>
#include <cassert>

namespace hwang {

inline std::string type_to_string(uint32_t type) {
  std::string s;
  uint8_t *p = reinterpret_cast<uint8_t*>(&type);
  for (int i = 0; i < 4; ++i) {
    s += static_cast<char>(p[3 - i]);
  }
  return s;
}

inline uint32_t string_to_type(const std::string& type_str) {
  uint32_t type;
  uint8_t *p = reinterpret_cast<uint8_t*>(&type);
  assert(type_str.size() == 4);
  for (int i = 0; i < 4; ++i) {
    p[i] = type_str.data()[3 - i];
  }
  return type;
}

struct FullBox {
  uint64_t size;
  uint32_t type;
  uint8_t version;
  uint32_t flags;
};

inline FullBox parse_box(GetBitsState& bs) {
  align(bs, 8);

  FullBox b;
  b.size = get_bits(bs, 32);
  b.type = get_bits(bs, 32);
  if (b.size == 1) {
    b.size == get_bits(bs, 64);
  }
  if (b.type == string_to_type("uuid")) {
    // Skip 128 bits
    get_bits(bs, 64);
    get_bits(bs, 64);
  }
  return b;
}

inline FullBox probe_box_type(const GetBitsState& bs) {
  GetBitsState bs2 = bs;

  align(bs2, 8);

  FullBox b;
  b.size = get_bits(bs2, 32);
  b.type = get_bits(bs2, 32);

  if (b.size == 1) {
    b.size == get_bits(bs2, 64);
  }
  if (b.type == string_to_type("uuid")) {
    // Skip 128 bits
    get_bits(bs2, 64);
    get_bits(bs2, 64);
  }

  return b;
}

inline FullBox parse_full_box(GetBitsState& bs) {
  align(bs, 8);

  FullBox b = parse_box(bs);
  b.version = (uint8_t)get_bits(bs, 8);
  b.flags = get_bits(bs, 24);

  return b;
}

inline GetBitsState restrict_bits_to_box(const GetBitsState &bs) {
  GetBitsState bs2 = bs;
  FullBox b = probe_box_type(bs2);
  GetBitsState new_bs = bs;
  new_bs.size = new_bs.offset / 8 + b.size;
  return new_bs;
}

struct FileTypeBox : public FullBox {
  uint32_t major_brand;
  uint32_t minor_version;
  std::vector<uint32_t> compatible_brands;
};

inline FileTypeBox parse_ftyp(GetBitsState& bs) {
  int64_t start = bs.offset / 8;

  FileTypeBox ftyp;
  *((FullBox*)&ftyp) = parse_box(bs);

  // Determine size left for compatible brands
  ftyp.major_brand = get_bits(bs, 32);
  ftyp.minor_version = get_bits(bs, 32);

  int64_t size_left = (start + ftyp.size) - (bs.offset / 8);

  int64_t num_brands = size_left / 4;
  for (int64_t i = 0; i < num_brands; ++i) {
    ftyp.compatible_brands.push_back(get_bits(bs, 32));
  }

  return ftyp;
}

inline FullBox parse_moov(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("moov"));
  return b;
}

inline FullBox parse_mvex(GetBitsState &bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("mvex"));
  return b;
}

struct TrackExtendsBox : public FullBox {
  uint32_t track_ID;
  uint32_t default_sample_description_index;
  uint32_t default_sample_duration;
  uint32_t default_sample_size;
  uint32_t default_sample_flags;
};

inline TrackExtendsBox parse_trex(GetBitsState &bs) {
  TrackExtendsBox t;
  *((FullBox*)&t) = parse_full_box(bs);
  assert(t.type == string_to_type("trex"));

  t.track_ID = get_bits(bs, 32);
  t.default_sample_description_index = get_bits(bs, 32);
  t.default_sample_duration = get_bits(bs, 32);
  t.default_sample_size = get_bits(bs, 32);
  t.default_sample_flags = get_bits(bs, 32);
  return t;
}

inline FullBox parse_trak(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("trak"));
  return b;
}

struct TrackHeaderBox : public FullBox {
  uint64_t creation_time;
  uint64_t modification_time;
  uint32_t track_ID;
  uint64_t duration;
  int32_t rate;
  int16_t layer;
  int16_t alternate_group;
  int16_t volume;
  int32_t matrix[9];
  uint32_t width;
  uint32_t height;
};

inline FullBox parse_tkhd(GetBitsState& bs) {
  TrackHeaderBox h;
  *((FullBox*)&h) = parse_full_box(bs);
  assert(h.type == string_to_type("tkhd"));

  if (h.version == 1) {
    h.creation_time = get_bits(bs, 64);
    h.modification_time = get_bits(bs, 64);
    h.track_ID = get_bits(bs, 32);
    (void)get_bits(bs, 32);
    h.duration = get_bits(bs, 64);
  } else if (h.version == 0) {
    h.creation_time = get_bits(bs, 32);
    h.modification_time = get_bits(bs, 32);
    h.track_ID = get_bits(bs, 32);
    (void)get_bits(bs, 32);
    h.duration = get_bits(bs, 32);
  }
  (void)get_bits(bs, 32);
  (void)get_bits(bs, 32);

  h.layer = get_bits(bs, 16);
  h.alternate_group = get_bits(bs, 16);
  h.volume = get_bits(bs, 16);
  for (int i = 0; i < 9; ++i) {
    h.matrix[i] = get_bits(bs, 32);
  }
  h.width = get_bits(bs, 32);
  h.width = get_bits(bs, 32);
  return h;
}

inline FullBox parse_mdia(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("mdia"));
  return b;
}

struct HandlerBox : public FullBox {
  uint32_t handler_type;
};

inline HandlerBox parse_hdlr(GetBitsState& bs) {
  HandlerBox h;
  *((FullBox*)&h) = parse_full_box(bs);
  assert(h.type == string_to_type("hdlr"));
  assert(h.version == 0);

  get_bits(bs, 32); // pre_defined
  h.handler_type = get_bits(bs, 32); // handler_type

  get_bits(bs, 32);
  get_bits(bs, 32);
  get_bits(bs, 32);

  return h;
}

inline FullBox parse_minf(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("minf"));
  return b;
}

inline FullBox parse_dinf(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("dinf"));
  return b;
}

struct DataEntryBox : public FullBox {
  std::string name;
  std::string location;
};

struct DataReferenceBox : public FullBox {
  std::vector<DataEntryBox> data_entries;
};

inline DataEntryBox parse_urn(GetBitsState& bs) {
  DataEntryBox e;
  *((FullBox*)&e) = parse_full_box(bs);
  assert(e.type == string_to_type("urn "));
  assert(e.version == 0);

  if (e.flags == 0x00000001) {
    assert(false);
  }

  exit(-1);
}

inline DataEntryBox parse_url(GetBitsState& bs) {
  DataEntryBox e;
  *((FullBox*)&e) = parse_full_box(bs);
  assert(e.type == string_to_type("url "));
  assert(e.version == 0);

  if (e.flags == 0x00000001) {
    return e;
  }

  exit(-1);
}

inline DataReferenceBox parse_dref(GetBitsState& bs) {
  DataReferenceBox r;
  *((FullBox*)&r) = parse_full_box(bs);
  assert(r.type == string_to_type("dref"));
  assert(r.version == 0);

  uint32_t num_entries = get_bits(bs, 32);
  for (uint32_t i = 0; i < num_entries; ++i) {
    FullBox b = probe_box_type(bs);
    if (b.type == string_to_type("url ")) {
      r.data_entries.push_back(parse_url(bs));
    } else if (b.type == string_to_type("urn ")) {
      r.data_entries.push_back(parse_urn(bs));
    }
  }
  return r;
}

inline FullBox parse_stbl(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("stbl"));
  return b;
}

struct SampleSizeBox : public FullBox {
  uint32_t sample_size;
  uint32_t sample_count;
  std::vector<uint32_t> entry_size;
};

inline SampleSizeBox parse_stsz(GetBitsState& bs) {
  SampleSizeBox sb;
  *((FullBox*)&sb) = parse_full_box(bs);
  assert(sb.type == string_to_type("stsz"));

  sb.sample_size = get_bits(bs, 32);
  sb.sample_count = get_bits(bs, 32);

  if (sb.sample_size == 0) {
    for (int i = 0; i < sb.sample_count; ++i) {
      sb.entry_size.push_back(get_bits(bs, 32));
    }
  }

  return sb;
}

inline SampleSizeBox parse_stz2(GetBitsState& bs) {
  SampleSizeBox sb;
  *((FullBox*)&sb) = parse_full_box(bs);
  assert(sb.type == string_to_type("stz2"));

  get_bits(bs, 24); // reserved

  int field_size = get_bits(bs, 8);
  sb.sample_count = get_bits(bs, 32);
  for (int i = 0; i < sb.sample_count; ++i) {
    sb.entry_size.push_back(get_bits(bs, field_size));
  }
  // Pad to integral number of bytes
  if (field_size == 4 && sb.sample_count % 2 == 1) {
    get_bits(bs, field_size);
  }

  return sb;
}

struct SampleToChunkBox : public FullBox {
  struct ChunkEntry {
    uint32_t num_samples;
    uint32_t sample_description_index;
  };
  std::vector<ChunkEntry> chunk_entries;
};

inline SampleToChunkBox parse_stsc(GetBitsState& bs, uint64_t num_samples) {
  SampleToChunkBox sb;
  *((FullBox*)&sb) = parse_full_box(bs);
  assert(sb.type == string_to_type("stsc"));

  uint32_t entry_count = get_bits(bs, 32);

  uint64_t total_samples = 0;
  uint32_t prev_first_chunk = 0;
  uint32_t prev_samples_per_chunk = 0;
  uint32_t prev_sample_description_index = 0;
  for (int i = 0; i < entry_count; ++i) {
    uint32_t first_chunk = get_bits(bs, 32);
    uint32_t samples_per_chunk = get_bits(bs, 32);
    uint32_t sample_description_index = get_bits(bs, 32);

    printf("first chunk %u, per chunk %u\n",
           first_chunk,
           samples_per_chunk);
    if (prev_first_chunk != 0) {
      SampleToChunkBox::ChunkEntry entry;
      entry.num_samples = prev_samples_per_chunk;
      entry.sample_description_index = prev_sample_description_index;
      for (int j = 0; j < (first_chunk - prev_first_chunk); ++j) {
        sb.chunk_entries.push_back(entry);
        total_samples += entry.num_samples;
      }
    }

    prev_first_chunk = first_chunk;
    prev_samples_per_chunk = samples_per_chunk;
    prev_sample_description_index = sample_description_index;
  }
  // Handle last chunks
  SampleToChunkBox::ChunkEntry entry;
  entry.num_samples = prev_samples_per_chunk;
  entry.sample_description_index = prev_sample_description_index;
  while (total_samples < num_samples) {
    sb.chunk_entries.push_back(entry);
    total_samples += entry.num_samples;
  }

  return sb;
}

struct ChunkOffsetBox : public FullBox {
  std::vector<uint64_t> chunk_offsets;
};

inline ChunkOffsetBox parse_stco(GetBitsState& bs) {
  ChunkOffsetBox sc;
  *((FullBox*)&sc) = parse_full_box(bs);
  assert(sc.type == string_to_type("stco"));

  uint32_t entry_count = get_bits(bs, 32);

  for (int i = 0; i < entry_count; ++i) {
    sc.chunk_offsets.push_back(get_bits(bs, 32));
  }

  return sc;
}

inline ChunkOffsetBox parse_co64(GetBitsState& bs) {
  ChunkOffsetBox sc;
  *((FullBox*)&sc) = parse_full_box(bs);
  assert(sc.type == string_to_type("co64"));

  uint32_t entry_count = get_bits(bs, 32);

  for (int i = 0; i < entry_count; ++i) {
    sc.chunk_offsets.push_back(get_bits(bs, 64));
  }

  return sc;
}

struct SyncSampleBox : public FullBox {
  std::vector<uint32_t> sample_number;
};

inline SyncSampleBox parse_stss(GetBitsState& bs) {
  SyncSampleBox ss;
  *((FullBox*)&ss) = parse_full_box(bs);
  assert(ss.type == string_to_type("co64"));

  uint32_t entry_count = get_bits(bs, 32);

  for (int i = 0; i < entry_count; ++i) {
    ss.sample_number.push_back(get_bits(bs, 32));
  }

  return ss;
}

inline FullBox parse_moof(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("moof"));
  return b;
}

inline FullBox parse_traf(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("traf"));
  return b;
}

struct TrackFragmentHeaderBox : public FullBox {
  enum struct BaseOffsetType {
    PROVIDED,
    IS_RELATIVE,
    IS_MOOF,
  };
  uint32_t track_ID;
  BaseOffsetType base_offset_type;
  uint64_t base_data_offset;
  uint32_t sample_description_index;
  uint32_t default_sample_duration;
  uint32_t default_sample_size;
  uint32_t default_sample_flags;

  inline bool base_data_offset_present() {
    return flags & 0x000001;
  }
  inline bool sample_description_index_present() {
    return flags & 0x000002;
  }
  inline bool default_sample_duration_present() {
    return flags & 0x000008;
  }
  inline bool default_sample_size_present() {
    return flags & 0x000010;
  }
  inline bool default_sample_flags_present() {
    return flags & 0x000020;
  }
  inline bool duration_is_empty() {
    return flags & 0x010000;
  }
  inline bool default_base_is_moof() {
    return flags & 0x020000;
  }
};

inline TrackFragmentHeaderBox parse_tfhd(GetBitsState& bs) {
  TrackFragmentHeaderBox tf;
  *((FullBox*)&tf) = parse_full_box(bs);
  assert(tf.type == string_to_type("tfhd"));

  tf.track_ID = get_bits(bs, 32);

  // base-data-offset-present
  if (tf.base_data_offset_present()) {
    tf.base_data_offset = get_bits(bs, 64);
    tf.base_offset_type = TrackFragmentHeaderBox::BaseOffsetType::PROVIDED;
  }
  // sample-description-index-present
  if (tf.sample_description_index_present()) {
    tf.sample_description_index = get_bits(bs, 32);
  }
  // default-sample-duration-present
  if (tf.default_sample_duration_present()) {
    tf.default_sample_duration = get_bits(bs, 32);
  }
  // default-sample-size-present
  if (tf.default_sample_size_present()) {
    tf.default_sample_size = get_bits(bs, 32);
  }
  // default-sample-flags-present
  if (tf.default_sample_flags_present()) {
    tf.default_sample_flags = get_bits(bs, 32);
  }
  // duration-is-empty
  if (tf.duration_is_empty()) {
    // ??
  }
  // default-base-is-moof
  if (!tf.base_data_offset_present() && tf.default_base_is_moof()) {
    tf.base_offset_type = TrackFragmentHeaderBox::BaseOffsetType::IS_MOOF;
  }

  if (!(tf.base_data_offset_present() || tf.default_base_is_moof())) {
    tf.base_offset_type = TrackFragmentHeaderBox::BaseOffsetType::IS_RELATIVE;
  }

  return tf;
}

struct TrackRunBox : public FullBox {
  struct Sample {
    uint32_t sample_duration;
    uint32_t sample_size;
    uint32_t sample_flags;
    uint32_t sample_composition_time_offset;
  };

  int32_t data_offset;
  uint32_t first_sample_flags;
  std::vector<Sample> samples;

  inline bool data_offset_present() {
    return flags & 0x000001;
  }
  inline bool first_sample_flags_present() {
    return flags & 0x000004;
  }
  inline bool sample_duration_present() {
    return flags & 0x000100;
  }
  inline bool sample_size_present() {
    return flags & 0x000200;
  }
  inline bool sample_flags_present() {
    return flags & 0x000400;
  }
  inline bool sample_composition_time_offsets_present() {
    return flags & 0x000800;
  }
};

inline TrackRunBox parse_trun(GetBitsState& bs) {
  TrackRunBox tr;
  *((FullBox*)&tr) = parse_full_box(bs);
  assert(tr.type == string_to_type("trun"));

  uint32_t sample_count = get_bits(bs, 32);

  if (tr.data_offset_present()) {
    tr.data_offset = get_bits(bs, 32);
  }
  if (tr.first_sample_flags_present()) {
    tr.first_sample_flags = get_bits(bs, 32);
  }

  for (int i = 0; i < sample_count; ++i) {
    TrackRunBox::Sample sample;
    if (tr.sample_duration_present()) {
      sample.sample_duration = get_bits(bs, 32);
    }
    if (tr.sample_size_present()) {
      sample.sample_size = get_bits(bs, 32);
    }
    if (tr.sample_flags_present()) {
      sample.sample_flags = get_bits(bs, 32);
    }
    if (tr.sample_composition_time_offsets_present()) {
      sample.sample_composition_time_offset = get_bits(bs, 32);
    }
    tr.samples.push_back(sample);
  }

  return tr;
}

}
