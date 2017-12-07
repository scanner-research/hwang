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

namespace hwang {

std::string type_to_string(uint32_t type) {
  std::string s;
  uint8_t *p = &type;
  for (int i = 0; i < 4; ++i) {
    s += static_cast<char>(p[i]);
  }
  return s;
}

uint32_t string_to_type(const std::string& type) {
  uint32_t type;
  uint8_t *p = &type;
  assert(type.size() == 4);
  for (int i = 0; i < 4; ++i) {
    p[i] = type.data()[i];
  }
  return type;
}

struct FullBox {
  uint64_t size;
  uint32_t type;
  uint8_t version;
  uint32_t flags;
};

FullBox parse_box(GetBitsState& bs) {
  FullBox b;
  b.size = get_bits(bs, 32);
  b.type = get_bits(bs, 32);
  if (b.size == 1) {
    b.size == get_bits(bs, 64);
  }
  if (b.type == string_to_type("uuid")) {
    // Skip 128 bits
    bs.get_bits(64);
    bs.get_bits(64);
  }
}

FullBox probe_box_type(GetBitsState& bs) {
  i64 total_size = 0;

  FullBox b;
  b.size = get_bits(bs, 32);
  b.type = get_bits(bs, 32);
  total_size += 8;

  if (b.size == 1) {
    b.size == get_bits(bs, 64);
    total_size += 8;
  }
  if (b.type == string_to_type("uuid")) {
    // Skip 128 bits
    bs.get_bits(64);
    bs.get_bits(64);
    total_size += 16;
  }
  bs.offset -= total_size * 8;

  return b;
}

FullBox parse_full_box(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  b.version = (uint8_t)get_bits(bs, 8);
  b.flags = get_bits(bs, 24);
}

struct FileTypeBox : public FullBox {
  uint32_t major_brand;
  uint32_t minor_brand;
  std::vector<uint32_t> compatible_brands;
}

FileTypeBox parse_ftyp(GetBitsState& bs) {
  FileTypeBox ftyp;
  *((FullBox*)&ftyp) = bs.parse_full_box(bs);
  // Determine size left for compatible brands
  ftyp.major_brand = get_bits(bs, 32);
  ftyp.minor_version = get_bits(bs, 32);
  int64_t num_brands = (ftyp.size - sizeof(FullBox) - 8) / 4;
  for (int64_t i = 0; i < num_brands; ++i) {
    ftyp.compatible_brands.push_back(get_bits(bs, 32));
  }
}

FullBox parse_moov(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("moov"));
  return b;
}

FullBox parse_trak(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("trak"));
  return b;
}

FullBox parse_mdia(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("mdia"));
  return b;
}

struct HandlerBox : public FullBox {
  uint32_t handler_type;
}

HandlerBox parse_hdlr(GetBitsState& bs) {
  HandlerBox h;
  *((FullBox*)&h) = parse_full_box(bs);
  assert(h.type == string_to_type("hdlr"));
  assert(h.version == 0);

  bs.get_bits(32); // pre_defined
  h.handler_type = bs.get_bits(32); // handler_type

  bs.get_bits(32);
  bs.get_bits(32);
  bs.get_bits(32);

  return h;
}

FullBox parse_dinf(GetBitsState& bs) {
  FullBox b = parse_box(bs);
  assert(b.type == string_to_type("dinf"));
  return b;
}

struct DataEntryBox : public FullBox {
  std::string name;
  std::string location;
}

struct DataReferenceBox : public FullBox {
  std::vector<DataEntryBox> data_entries;
}

DataEntryBox parse_urn(GetBitsState& bs) {
  DataEntryBox e;
  *((FullBox*)&e) = parse_full_box(bs);
  assert(r.type == string_to_type("urn "));
  assert(r.version == 0);

  if (e.flags == 0x00000001) {
    assert(false);
  }

  exit(-1);
}

DataEntryBox parse_url(GetBitsState& bs) {
  DataEntryBox e;
  *((FullBox*)&e) = parse_full_box(bs);
  assert(r.type == string_to_type("url "));
  assert(r.version == 0);

  if (e.flags == 0x00000001) {
    return e;
  }

  exit(-1);
}

DataReferenceBox parse_dref(GetBitsState& bs) {
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

FullBox parse_stbl(GetBitsState& bs) {
  FullBox b = parse_full_box(bs);
  assert(b.type == string_to_type("stbl"));
  return b;
}

struct SampleSizeBox : public FullBox {
  uint32_t sample_size;
  uint32_t sample_count;
  std::vector<uint32_t> entry_size;
};

SampleSizeBox parse_stsz(GetBitsState& bs) {
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

SampleSizeBox parse_stz2(GetBitsState& bs) {
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

SampleToChunkBox parse_stsc(GetBitsState& bs) {
  SampleToChunkBox sb;
  *((FullBox*)&sb) = parse_full_box(bs);
  assert(sb.type == string_to_type("stsc"));

  uint32_t entry_count = get_bits(bs, 32);

  uint32_t prev_first_chunk = 0;
  uint32_t prev_samples_per_chunk = 0;
  uint32_t prev_sample_description_index = 0;
  for (int i = 0; i < entry_count; ++i) {
    uint32_t first_chunk = get_bits(bs, 32);
    uint32_t samples_per_chunk = get_bits(bs, 32);
    uint32_t sample_description_index = get_bits(bs, 32);

    if (prev_first_chunk != 0) {
      ChunkEntry entry;
      entry.num_samples = prev_samples_per_chunk;
      entry.sample_description_index = prev_sample_description_index;
      for (int j = 0; j < (first_chunk - prev_first_chunk); ++j) {
        sb.chunk_entries.push_back(entry);
      }
    }

    prev_first_chunk = first_chunk;
    prev_samples_per_chunk = prev_samples_per_chunk;
    prev_sample_description_index = sample_description_index;
  }

  return sb;
}

struct ChunkOffsetBox : public FullBox {
  std::vector<uint64_t> chunk_offsets;
};

ChunkOffsetBox parse_stco(GetBitsState& bs) {
  SampleToChunkBox sc;
  *((FullBox*)&sc) = parse_full_box(bs);
  assert(sc.type == string_to_type("stco"));

  uint32_t entry_count = get_bits(bs, 32);

  for (int i = 0; i < entry_count; ++i) {
    sc.chunk_offsets.push_back(get_bits(bs, 32));
  }

  return sc;
}

ChunkOffsetBox parse_co64(GetBitsState& bs) {
  SampleToChunkBox sc;
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
}

SyncSampleBox parse_stss(GetBitsState& bs) {
  SyncSampleBox ss;
  *((FullBox*)&ss) = parse_full_box(bs);
  assert(ss.type == string_to_type("co64"));

  uint32_t entry_count = get_bits(bs, 32);

  for (int i = 0; i < entry_count; ++i) {
    ss.sample_number.push_back(get_bits(bs, 32));
  }

  return sc;
}

}
