#include "frontends/common/Util_Hash.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

using Uint4_t = uint32_t;

// MD5 implementation follows the standard RSA Data Security, Inc. MD5
// Message-Digest Algorithm. It inherently uses magic numbers from the
// specification and bit-level operations.

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-type-reinterpret-cast,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

// --- Constants ---

static constexpr int md5_block_size = 64;
static constexpr int md5_state_size = 4;
static constexpr int md5_digest_size = 16;
static constexpr int md5_hex_buffer_size = 33;

static constexpr uint32_t md5_init_0 = 0x67452301U;
static constexpr uint32_t md5_init_1 = 0xefcdab89U;
static constexpr uint32_t md5_init_2 = 0x98badcfeU;
static constexpr uint32_t md5_init_3 = 0x10325476U;

// --- Internal State ---

static std::array<Uint4_t, md5_state_size> state;
static uint64_t
    total_length;  // Total length in bytes (64-bit to prevent overflow)
static std::array<uint8_t, md5_block_size> buffer;

// --- Algorithmic Helpers ---

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

static constexpr std::array<Uint4_t, md5_state_size> md5_initstate = {
    {md5_init_0, md5_init_1, md5_init_2, md5_init_3}};

static constexpr std::array<char, 4> s1 = {{7, 12, 17, 22}};
static constexpr std::array<char, 4> s2 = {{5, 9, 14, 20}};
static constexpr std::array<char, 4> s3 = {{4, 11, 16, 23}};
static constexpr std::array<char, 4> s4 = {{6, 10, 15, 21}};

static constexpr std::array<Uint4_t, md5_block_size> T = {
    {0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU,
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
     0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U}};

static void md5_transform(const uint8_t block[md5_block_size]) {
  int i = 0;
  int j = 0;
  Uint4_t a = 0;
  Uint4_t b = 0;
  Uint4_t c = 0;
  Uint4_t d = 0;
  Uint4_t tmp = 0;

  const auto* x = reinterpret_cast<const Uint4_t*>(block);

  a = state.at(0);
  b = state.at(1);
  c = state.at(2);
  d = state.at(3);

  for (i = 0; i < 16; i++) {
    tmp = a + F(b, c, d) + x[i] + T.at(static_cast<size_t>(i));
    tmp = rotate_left(tmp, s1.at(static_cast<size_t>(i & 3)));
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }
  for (i = 0, j = 1; i < 16; i++, j += 5) {
    tmp = a + G(b, c, d) + x[j & 15] + T.at(static_cast<size_t>(i) + 16);
    tmp = rotate_left(tmp, s2.at(static_cast<size_t>(i & 3)));
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }
  for (i = 0, j = 5; i < 16; i++, j += 3) {
    tmp = a + H(b, c, d) + x[j & 15] + T.at(static_cast<size_t>(i) + 32);
    tmp = rotate_left(tmp, s3.at(static_cast<size_t>(i & 3)));
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }
  for (i = 0, j = 0; i < 16; i++, j += 7) {
    tmp = a + I(b, c, d) + x[j & 15] + T.at(static_cast<size_t>(i) + 48);
    tmp = rotate_left(tmp, s4.at(static_cast<size_t>(i & 3)));
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }

  state.at(0) += a;
  state.at(1) += b;
  state.at(2) += c;
  state.at(3) += d;
}

static void md5_init() {
  memcpy(state.data(), md5_initstate.data(), sizeof(md5_initstate));
  total_length = 0;
}

static void md5_update(const char* input, size_t inputlen) {
  auto buflen = static_cast<size_t>(total_length & 63U);
  total_length += static_cast<uint64_t>(inputlen);

  if (buflen + inputlen < md5_block_size) {
    memcpy(buffer.data() + buflen, input, inputlen);
    return;
  }

  size_t first_part = md5_block_size - buflen;
  memcpy(buffer.data() + buflen, input, first_part);
  md5_transform(buffer.data());

  size_t i = first_part;
  for (; i + md5_block_size <= inputlen; i += md5_block_size) {
    md5_transform(reinterpret_cast<const uint8_t*>(input + i));
  }

  memcpy(buffer.data(), input + i, inputlen - i);
}

static auto md5_final() -> uint8_t* {
  auto buflen = static_cast<size_t>(total_length & 63U);

  buffer.at(buflen++) = 0x80U;
  if (buflen > 56) {
    memset(buffer.data() + buflen, 0, md5_block_size - buflen);
    md5_transform(buffer.data());
    memset(buffer.data(), 0, 56);
  } else {
    memset(buffer.data() + buflen, 0, 56 - buflen);
  }

  // Append length in bits as 64-bit little-endian
  uint64_t bits = total_length * 8;
  memcpy(buffer.data() + 56, &bits, sizeof(bits));
  md5_transform(buffer.data());

  return reinterpret_cast<uint8_t*>(state.data());
}

static auto md5(const char* input) -> char* {
  if (input == nullptr) {
    return nullptr;
  }
  md5_init();
  md5_update(input, strlen(input));
  return reinterpret_cast<char*>(md5_final());
}

extern "C" {

auto md5str(const char* input) -> char* {
  static std::array<char, md5_hex_buffer_size> result;
  if (input == nullptr) {
    result.at(0) = '\0';
    return result.data();
  }

  auto* digest = reinterpret_cast<uint8_t*>(md5(input));
  if (digest == nullptr) {
    result.at(0) = '\0';
    return result.data();
  }

  for (size_t i = 0; i < md5_digest_size; i++) {
    snprintf(result.data() + (2 * i), 3, "%02X", digest[i]);
  }
  result.at(md5_hex_buffer_size - 1) = '\0';
  return result.data();
}
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-type-reinterpret-cast,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
