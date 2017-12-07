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

std::string type_to_string(uint32_t type) {
  std::string s;
  uint8_t *p = reinterpret_cast<uint8_t*>(&type);
  for (int i = 0; i < 4; ++i) {
    s += static_cast<char>(p[3 - i]);
  }
  return s;
}

uint32_t string_to_type(const std::string& type_str) {
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

FullBox parse_box(GetBitsState& bs) {
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

FullBox probe_box_type(const GetBitsState& bs) {
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

FullBox parse_full_box(GetBitsState& bs) {
  align(bs, 8);

  FullBox b = parse_box(bs);
  b.version = (uint8_t)get_bits(bs, 8);
  b.flags = get_bits(bs, 24);

  return b;
}

GetBitsState restrict_bits_to_box(const GetBitsState &bs) {
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

FileTypeBox parse_ftyp(GetBitsState& bs) {
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
};

HandlerBox parse_hdlr(GetBitsState& bs) {
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

FullBox parse_dinf(GetBitsState& bs) {
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

DataEntryBox parse_urn(GetBitsState& bs) {
  DataEntryBox e;
  *((FullBox*)&e) = parse_full_box(bs);
  assert(e.type == string_to_type("urn "));
  assert(e.version == 0);

  if (e.flags == 0x00000001) {
    assert(false);
  }

  exit(-1);
}

DataEntryBox parse_url(GetBitsState& bs) {
  DataEntryBox e;
  *((FullBox*)&e) = parse_full_box(bs);
  assert(e.type == string_to_type("url "));
  assert(e.version == 0);

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
      SampleToChunkBox::ChunkEntry entry;
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
  ChunkOffsetBox sc;
  *((FullBox*)&sc) = parse_full_box(bs);
  assert(sc.type == string_to_type("stco"));

  uint32_t entry_count = get_bits(bs, 32);

  for (int i = 0; i < entry_count; ++i) {
    sc.chunk_offsets.push_back(get_bits(bs, 32));
  }

  return sc;
}

ChunkOffsetBox parse_co64(GetBitsState& bs) {
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

SyncSampleBox parse_stss(GetBitsState& bs) {
  SyncSampleBox ss;
  *((FullBox*)&ss) = parse_full_box(bs);
  assert(ss.type == string_to_type("co64"));

  uint32_t entry_count = get_bits(bs, 32);

  for (int i = 0; i < entry_count; ++i) {
    ss.sample_number.push_back(get_bits(bs, 32));
  }

  return ss;
}

}
