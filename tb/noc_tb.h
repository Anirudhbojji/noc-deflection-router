// Shared testbench helpers: flit encode/decode mirroring noc_pkg::flit_t.
//
// Packed struct layout (53 bits, payload at LSB):
//   [52:49] dst_x  [48:45] dst_y  [44:41] src_x  [40:37] src_y
//   [36:29] pkt_id [28:25] seq    [24] last      [23:16] age
//   [15:0]  payload
#pragma once

#include <cstdint>

struct Flit {
  uint8_t  dst_x = 0, dst_y = 0, src_x = 0, src_y = 0;
  uint8_t  pkt_id = 0, seq = 0, last = 1, age = 0;
  uint16_t payload = 0;
};

inline uint64_t encode(const Flit& f) {
  uint64_t v = 0;
  v |= (uint64_t)(f.dst_x & 0xF)   << 49;
  v |= (uint64_t)(f.dst_y & 0xF)   << 45;
  v |= (uint64_t)(f.src_x & 0xF)   << 41;
  v |= (uint64_t)(f.src_y & 0xF)   << 37;
  v |= (uint64_t)(f.pkt_id & 0xFF) << 29;
  v |= (uint64_t)(f.seq & 0xF)     << 25;
  v |= (uint64_t)(f.last & 0x1)    << 24;
  v |= (uint64_t)(f.age & 0xFF)    << 16;
  v |= (uint64_t)(f.payload);
  return v;
}

inline Flit decode(uint64_t v) {
  Flit f;
  f.dst_x   = (v >> 49) & 0xF;
  f.dst_y   = (v >> 45) & 0xF;
  f.src_x   = (v >> 41) & 0xF;
  f.src_y   = (v >> 37) & 0xF;
  f.pkt_id  = (v >> 29) & 0xFF;
  f.seq     = (v >> 25) & 0xF;
  f.last    = (v >> 24) & 0x1;
  f.age     = (v >> 16) & 0xFF;
  f.payload = v & 0xFFFF;
  return f;
}

#define CHECK(cond, msg)                                                   \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL: %s (line %d)\n", msg, __LINE__);                       \
      return 1;                                                            \
    }                                                                      \
  } while (0)
