// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/Util_Hash.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using Uint4_t = uint32_t;

namespace {

constexpr int k_md5_block_size = 64;
constexpr int k_md5_state_size = 4;
constexpr int k_md5_digest_size = 16;
constexpr int k_md5_hex_buffer_size = 33;

constexpr uint32_t k_md5_init_0 = 0x67452301U;
constexpr uint32_t k_md5_init_1 = 0xefcdab89U;
constexpr uint32_t k_md5_init_2 = 0x98badcfeU;
constexpr uint32_t k_md5_init_3 = 0x10325476U;

constexpr std::array<Uint4_t, k_md5_state_size> k_md5_initstate = {
    {k_md5_init_0, k_md5_init_1, k_md5_init_2, k_md5_init_3},
};

struct Md5Context_t {
  std::array<Uint4_t, k_md5_state_size> state = k_md5_initstate;
  uint64_t total_length = 0;
  std::array<uint8_t, k_md5_block_size> buffer{};
};

static inline auto F(Uint4_t x, Uint4_t y, Uint4_t z) noexcept -> Uint4_t {
  return ((x & y) | ((~x) & z));
}

static inline auto G(Uint4_t x, Uint4_t y, Uint4_t z) noexcept -> Uint4_t {
  return ((x & z) | (y & (~z)));
}

static inline auto H(Uint4_t x, Uint4_t y, Uint4_t z) noexcept -> Uint4_t {
  return (x ^ y ^ z);
}

static inline auto I(Uint4_t x, Uint4_t y, Uint4_t z) noexcept -> Uint4_t {
  return (y ^ (x | (~z)));
}

static inline auto rotate_left(Uint4_t x, int n) noexcept -> Uint4_t {
  constexpr int bits_in_uint4 = 32;
  return ((x << n) | (x >> (bits_in_uint4 - n)));
}

constexpr std::array<char, 4> k_s1 = {{7, 12, 17, 22}};
constexpr std::array<char, 4> k_s2 = {{5, 9, 14, 20}};
constexpr std::array<char, 4> k_s3 = {{4, 11, 16, 23}};
constexpr std::array<char, 4> k_s4 = {{6, 10, 15, 21}};

constexpr std::array<Uint4_t, k_md5_block_size> k_t = {
    {
        0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU,
        0x4787c62aU, 0xa8304613U, 0xfd469501U, 0x698098d8U, 0x8b44f7afU,
        0xffff5bb1U, 0x895cd7beU, 0x6b901122U, 0xfd987193U, 0xa679438eU,
        0x49b40821U, 0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
        0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U, 0x21e1cde6U,
        0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U,
        0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U,
        0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
        0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U, 0xd9d4d039U,
        0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U, 0xf4292244U, 0x432aff97U,
        0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU,
        0x85845dd1U, 0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
        0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U,
    },
};

static auto md5_transform(Md5Context_t* ctx,
                          const uint8_t block[k_md5_block_size]) -> void {
  Uint4_t a = ctx->state.at(0);
  Uint4_t b = ctx->state.at(1);
  Uint4_t c = ctx->state.at(2);
  Uint4_t d = ctx->state.at(3);
  Uint4_t tmp = 0;

  const auto* x = reinterpret_cast<const Uint4_t*>(block);

  for (int i = 0; i < 16; i++) {
    tmp = a + F(b, c, d) + x[i] + k_t.at(static_cast<size_t>(i));
    tmp = rotate_left(tmp, k_s1.at(static_cast<size_t>(i & 3)));
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }

  for (int i = 0, j = 1; i < 16; i++, j += 5) {
    tmp = a + G(b, c, d) + x[j & 15] + k_t.at(static_cast<size_t>(i) + 16);
    tmp = rotate_left(tmp, k_s2.at(static_cast<size_t>(i & 3)));
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }

  for (int i = 0, j = 5; i < 16; i++, j += 3) {
    tmp = a + H(b, c, d) + x[j & 15] + k_t.at(static_cast<size_t>(i) + 32);
    tmp = rotate_left(tmp, k_s3.at(static_cast<size_t>(i & 3)));
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }

  for (int i = 0, j = 0; i < 16; i++, j += 7) {
    tmp = a + I(b, c, d) + x[j & 15] + k_t.at(static_cast<size_t>(i) + 48);
    tmp = rotate_left(tmp, k_s4.at(static_cast<size_t>(i & 3)));
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }

  ctx->state.at(0) += a;
  ctx->state.at(1) += b;
  ctx->state.at(2) += c;
  ctx->state.at(3) += d;
}

static auto md5_update(Md5Context_t* ctx, const char* input, size_t inputlen)
    -> void {
  const auto buflen = static_cast<size_t>(ctx->total_length & 63U);
  ctx->total_length += inputlen;

  if (buflen + inputlen < k_md5_block_size) {
    memcpy(ctx->buffer.data() + buflen, input, inputlen);
    return;
  }

  const size_t first_part = k_md5_block_size - buflen;
  memcpy(ctx->buffer.data() + buflen, input, first_part);
  md5_transform(ctx, ctx->buffer.data());

  size_t i = first_part;
  for (; i + k_md5_block_size <= inputlen; i += k_md5_block_size) {
    md5_transform(ctx, reinterpret_cast<const uint8_t*>(input + i));
  }

  memcpy(ctx->buffer.data(), input + i, inputlen - i);
}

static auto md5_final(Md5Context_t* ctx,
                      std::array<uint8_t, k_md5_digest_size>& digest_out)
    -> void {
  auto buflen = static_cast<size_t>(ctx->total_length & 63U);

  ctx->buffer.at(buflen++) = 0x80U;
  if (buflen > 56) {
    memset(ctx->buffer.data() + buflen, 0, k_md5_block_size - buflen);
    md5_transform(ctx, ctx->buffer.data());
    memset(ctx->buffer.data(), 0, 56);
  } else {
    memset(ctx->buffer.data() + buflen, 0, 56 - buflen);
  }

  const uint64_t bits = ctx->total_length * 8;
  memcpy(ctx->buffer.data() + 56, &bits, sizeof(bits));
  md5_transform(ctx, ctx->buffer.data());

  memcpy(digest_out.data(), ctx->state.data(), digest_out.size());
}

}  // namespace

auto md5str(const char* input) -> std::string {
  if (input == nullptr) {
    return {};
  }

  Md5Context_t ctx{};
  md5_update(&ctx, input, strlen(input));

  std::array<uint8_t, k_md5_digest_size> digest{};
  md5_final(&ctx, digest);

  std::array<char, k_md5_hex_buffer_size> hex_str{};
  for (size_t i = 0; i < k_md5_digest_size; i++) {
    snprintf(hex_str.data() + (2 * i), 3, "%02X", digest.at(i));
  }
  return std::string(hex_str.data(), k_md5_hex_buffer_size - 1);
}
