#include "frontends/common/Util_Hash.h"

// Manual memory management and pointer decay checks are disabled here
// as they are required for this specific C-compatible architectural boundary.
// Also disabling magic number and member initialization checks as they
// are inherent to this standard cryptographic algorithm implementation.
// NOLINTBEGIN(cppcoreguidelines-owning-memory, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-pro-type-member-init, cppcoreguidelines-pro-type-reinterpret-cast)

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <new>

using UINT4 = uint32_t;

// --- Constants ---

static constexpr int MD5_BLOCK_SIZE = 64;
static constexpr int MD5_STATE_SIZE = 4;
static constexpr int MD5_DIGEST_SIZE = 16;
static constexpr int MD5_HEX_BUFFER_SIZE = 33;

static constexpr uint32_t MD5_INIT_0 = 0x67452301U;
static constexpr uint32_t MD5_INIT_1 = 0xefcdab89U;
static constexpr uint32_t MD5_INIT_2 = 0x98badcfeU;
static constexpr uint32_t MD5_INIT_3 = 0x10325476U;

// --- Internal State ---

static UINT4 state[MD5_STATE_SIZE];
static uint64_t total_length; // Total length in bytes (64-bit to prevent overflow)
static uint8_t buffer[MD5_BLOCK_SIZE];

// --- Algorithmic Helpers ---

static inline auto F(UINT4 x, UINT4 y, UINT4 z) -> UINT4 {
  return ((x & y) | ((~x) & z));
}

static inline auto G(UINT4 x, UINT4 y, UINT4 z) -> UINT4 {
  return ((x & z) | (y & (~z)));
}

static inline auto H(UINT4 x, UINT4 y, UINT4 z) -> UINT4 {
  return (x ^ y ^ z);
}

static inline auto I(UINT4 x, UINT4 y, UINT4 z) -> UINT4 {
  return (y ^ (x | (~z)));
}

static inline auto ROTATE_LEFT(UINT4 x, int n) -> UINT4 {
  return ((x << n) | (x >> (32 - n)));
}

static const UINT4 md5_initstate[MD5_STATE_SIZE] = {
    MD5_INIT_0, MD5_INIT_1, MD5_INIT_2, MD5_INIT_3};

static const char s1[4] = {7, 12, 17, 22};
static const char s2[4] = {5, 9, 14, 20};
static const char s3[4] = {4, 11, 16, 23};
static const char s4[4] = {6, 10, 15, 21};

static const UINT4 T[MD5_BLOCK_SIZE] = {
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU,
    0xa8304613U, 0xfd469501U, 0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
    0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U, 0xf61e2562U, 0xc040b340U,
    0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
    0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U,
    0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
    0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U, 0x289b7ec6U, 0xeaa127faU,
    0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
    0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U,
    0xffeff47dU, 0x85845dd1U, 0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
    0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U};

static void md5_transform(const uint8_t block[MD5_BLOCK_SIZE])
{
  int i = 0, j = 0;
  UINT4 a = 0, b = 0, c = 0, d = 0, tmp = 0;
  auto *x = reinterpret_cast<const UINT4 *>(block);

  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];

  for (i = 0; i < 16; i++) {
    tmp = a + F(b, c, d) + x[i] + T[i];
    tmp = ROTATE_LEFT(tmp, s1[i & 3]);
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }
  for (i = 0, j = 1; i < 16; i++, j += 5) {
    tmp = a + G(b, c, d) + x[j & 15] + T[i + 16];
    tmp = ROTATE_LEFT(tmp, s2[i & 3]);
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }
  for (i = 0, j = 5; i < 16; i++, j += 3) {
    tmp = a + H(b, c, d) + x[j & 15] + T[i + 32];
    tmp = ROTATE_LEFT(tmp, s3[i & 3]);
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }
  for (i = 0, j = 0; i < 16; i++, j += 7) {
    tmp = a + I(b, c, d) + x[j & 15] + T[i + 48];
    tmp = ROTATE_LEFT(tmp, s4[i & 3]);
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

static void md5_init() {
  memcpy(state, md5_initstate, sizeof(md5_initstate));
  total_length = 0;
}

static void md5_update(const char *input, size_t inputlen) {
  auto buflen = static_cast<size_t>(total_length & 63U);
  total_length += static_cast<uint64_t>(inputlen);
  
  if (buflen + inputlen < MD5_BLOCK_SIZE) {
    memcpy(buffer + buflen, input, inputlen);
    return;
  }

  size_t first_part = MD5_BLOCK_SIZE - buflen;
  memcpy(buffer + buflen, input, first_part);
  md5_transform(buffer);
  
  size_t i = first_part;
  for (; i + MD5_BLOCK_SIZE <= inputlen; i += MD5_BLOCK_SIZE) {
    md5_transform(reinterpret_cast<const uint8_t *>(input + i));
  }
  
  memcpy(buffer, input + i, inputlen - i);
}

static auto md5_final() -> uint8_t * {
  auto buflen = static_cast<size_t>(total_length & 63U);

  buffer[buflen++] = 0x80U;
  if (buflen > 56) {
    memset(buffer + buflen, 0, MD5_BLOCK_SIZE - buflen);
    md5_transform(buffer);
    memset(buffer, 0, 56);
  } else {
    memset(buffer + buflen, 0, 56 - buflen);
  }

  // Append length in bits as 64-bit little-endian
  uint64_t bits = total_length * 8;
  memcpy(buffer + 56, &bits, sizeof(bits));
  md5_transform(buffer);

  return reinterpret_cast<uint8_t *>(state);
}

static auto md5(const char *input) -> char * {
  if (input == nullptr) {
    return nullptr;
  }
  md5_init();
  md5_update(input, strlen(input));
  return reinterpret_cast<char *>(md5_final());
}

extern "C" {

auto md5str(const char *input) -> char * {
  static char result[MD5_HEX_BUFFER_SIZE];
  if (input == nullptr) {
      result[0] = '\0';
      return result;
  }
  
  auto *digest = reinterpret_cast<uint8_t *>(md5(input));
  if (digest == nullptr) {
      result[0] = '\0';
      return result;
  }

  for (size_t i = 0; i < MD5_DIGEST_SIZE; i++) {
    sprintf(result + (2 * i), "%02X", digest[i]);
  }
  result[MD5_HEX_BUFFER_SIZE - 1] = '\0';
  return result;
}

}

// NOLINTEND(cppcoreguidelines-owning-memory, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-pro-type-member-init, cppcoreguidelines-pro-type-reinterpret-cast)
