// SymphonyStation5
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compact MD5 (RFC 1321).

#include "util/md5.h"

#include <cstdint>

namespace {

constexpr uint32_t kK[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

constexpr int kShift[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7,
                            12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,
                            14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16,
                            23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21,
                            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

uint32_t Rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

}  // namespace

std::string Md5Hex(const std::string& data) {
  uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};

  // Pad: 0x80, zeros to 56 mod 64, then the bit length (little endian).
  std::string msg = data;
  uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8;
  msg.push_back(static_cast<char>(0x80));
  while (msg.size() % 64 != 56) {
    msg.push_back('\0');
  }
  for (int i = 0; i < 8; i++) {
    msg.push_back(static_cast<char>((bit_len >> (8 * i)) & 0xff));
  }

  for (size_t off = 0; off < msg.size(); off += 64) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++) {
      const auto* p =
          reinterpret_cast<const unsigned char*>(msg.data() + off + i * 4);
      m[i] = p[0] | (p[1] << 8) | (p[2] << 16) |
             (static_cast<uint32_t>(p[3]) << 24);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; i++) {
      uint32_t f;
      int g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      uint32_t tmp = d;
      d = c;
      c = b;
      b = b + Rotl(a + f + kK[i] + m[g], kShift[i]);
      a = tmp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
  }

  static const char kHex[] = "0123456789abcdef";
  std::string out;
  for (uint32_t v : h) {
    for (int i = 0; i < 4; i++) {
      unsigned byte = (v >> (8 * i)) & 0xff;
      out.push_back(kHex[byte >> 4]);
      out.push_back(kHex[byte & 0xf]);
    }
  }
  return out;
}
