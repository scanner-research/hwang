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

namespace hwang {

struct GetBitsState {
  const uint8_t* buffer;
  int64_t offset;
  int64_t size;
};

inline uint8_t get_bit(GetBitsState& gb) {
  uint8_t v =
      ((*(gb.buffer + (gb.offset >> 0x3))) >> (0x7 - (gb.offset & 0x7))) & 0x1;
  //((*(gb.buffer + (gb.offset >> 0x3))) >> ((gb.offset & 0x7))) & 0x1;
  gb.offset++;
  return v;
}

inline uint64_t get_bits(GetBitsState& gb, int32_t bits) {
  uint64_t v = 0;
  for (int i = bits - 1; i >= 0; i--) {
    v |= (uint64_t)(get_bit(gb)) << i;
  }
  return v;
}

inline void align(GetBitsState& gb, int32_t alignment) {
  int64_t diff = gb.offset % alignment;
  if (diff != 0) {
    gb.offset += alignment - diff;
  }
}

inline uint64_t get_ue_golomb(GetBitsState& gb) {
  // calculate zero bits. Will be optimized.
  int32_t zeros = 0;
  while (0 == get_bit(gb)) {
    zeros++;
  }

  // insert first 1 bit
  uint32_t info = 1 << zeros;

  for (int32_t i = zeros - 1; i >= 0; i--) {
    info |= get_bit(gb) << i;
  }

  return (info - 1);
}

inline uint32_t get_se_golomb(GetBitsState& gb) {
  // calculate zero bits. Will be optimized.
  int32_t zeros = 0;
  while (0 == get_bit(gb)) {
    zeros++;
  }

  // insert first 1 bit
  int32_t info = 1 << zeros;

  for (int32_t i = zeros - 1; i >= 0; i--) {
    info |= get_bit(gb) << i;
  }

  return (info - 1);
}

}
