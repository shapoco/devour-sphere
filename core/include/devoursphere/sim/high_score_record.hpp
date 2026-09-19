// The high score as the handhelds keep it in flash: 16 bytes that say which
// game they belong to and prove they came through whole. Blank flash (all
// 0xFF), a record of another major version and a damaged one all decode as
// "no record". The WASM build keeps the same three values in localStorage as
// JSON instead (impl/wasm/SPEC.md); the platforms decide when to write
// (impl/xiamocon/devoursphere/include/high_score_store.hpp).
#pragma once

#include <cstddef>
#include <cstdint>

#include "devoursphere/sim/config.hpp"

namespace devoursphere::sim {

constexpr size_t HIGH_SCORE_RECORD_BYTES = 16;

// CRC-32 as zlib computes it (IEEE polynomial, reflected). Bit by bit: the
// record is 12 bytes, a table would be a waste of flash
inline uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

// Layout, little endian:
//   0  "DSHS"            magic
//   4  u16 major         VERSION_MAJOR when written
//   6  u16 sphere        the sphere reached in that run
//   8  u32 score
//  12  u32 crc32         of the 12 bytes before it
inline void encodeHighScoreRecord(uint8_t *out, uint32_t score, int sphere) {
  out[0] = 'D';
  out[1] = 'S';
  out[2] = 'H';
  out[3] = 'S';
  out[4] = (uint8_t)VERSION_MAJOR;
  out[5] = (uint8_t)(VERSION_MAJOR >> 8);
  out[6] = (uint8_t)sphere;
  out[7] = (uint8_t)(sphere >> 8);
  for (int i = 0; i < 4; i++) out[8 + i] = (uint8_t)(score >> (8 * i));
  const uint32_t c = crc32(out, 12);
  for (int i = 0; i < 4; i++) out[12 + i] = (uint8_t)(c >> (8 * i));
}

// False when there is no record to take: blank flash, another major version
// (the scores do not compare; see SPEC.md "バージョン") or a damaged one
inline bool decodeHighScoreRecord(const uint8_t *in, uint32_t *score,
                                  int *sphere) {
  if (in[0] != 'D' || in[1] != 'S' || in[2] != 'H' || in[3] != 'S') return false;
  uint32_t c = 0;
  for (int i = 0; i < 4; i++) c |= (uint32_t)in[12 + i] << (8 * i);
  if (c != crc32(in, 12)) return false;
  const int major = in[4] | (in[5] << 8);
  if (major != VERSION_MAJOR) return false;
  *sphere = in[6] | (in[7] << 8);
  uint32_t s = 0;
  for (int i = 0; i < 4; i++) s |= (uint32_t)in[8 + i] << (8 * i);
  *score = s;
  return true;
}

}  // namespace devoursphere::sim
