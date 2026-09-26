// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstdint>

#include "core/Util_Endian.h"
#include "doctest.h"

TEST_CASE("Util_Endian: nullptr safety") {
  CHECK(read_u16_le(nullptr) == 0);
  CHECK(read_u32_le(nullptr) == 0);
  CHECK(read_u16_unaligned(nullptr) == 0);

  // Write calls with nullptr must be no-ops and not crash
  write_u16_le(nullptr, 0x1234);
  write_u32_le(nullptr, 0x12345678);
}

TEST_CASE("Util_Endian: read_u16_le decodes little-endian bytes") {
  const std::array<uint8_t, 4> bytes = {0x34, 0x12, 0xCD, 0xAB};

  CHECK(read_u16_le(bytes.data()) == 0x1234);
  CHECK(read_u16_le(bytes.data() + 2) == 0xABCD);
  CHECK(read_u16_unaligned(bytes.data()) == 0x1234);
  CHECK(read_u16_unaligned(bytes.data() + 1) == 0xCD12);
}

TEST_CASE("Util_Endian: read_u32_le decodes little-endian bytes") {
  const std::array<uint8_t, 5> bytes = {0x78, 0x56, 0x34, 0x12, 0x99};

  CHECK(read_u32_le(bytes.data()) == 0x12345678U);
  CHECK(read_u32_le(bytes.data() + 1) == 0x99123456U);
}

TEST_CASE("Util_Endian: boundary and sign-bit values") {
  const std::array<uint8_t, 4> zeros = {0x00, 0x00, 0x00, 0x00};
  CHECK(read_u16_le(zeros.data()) == 0x0000);
  CHECK(read_u32_le(zeros.data()) == 0x00000000U);

  const std::array<uint8_t, 4> max_vals = {0xFF, 0xFF, 0xFF, 0xFF};
  CHECK(read_u16_le(max_vals.data()) == 0xFFFF);
  CHECK(read_u32_le(max_vals.data()) == 0xFFFFFFFFU);

  const std::array<uint8_t, 4> sign_bit = {0x00, 0x00, 0x00, 0x80};
  CHECK(read_u32_le(sign_bit.data()) == 0x80000000U);
}

TEST_CASE("Util_Endian: write_u16_le encodes and round-trips correctly") {
  std::array<uint8_t, 4> buf = {0, 0, 0, 0};

  write_u16_le(buf.data(), 0x1234);
  CHECK(buf[0] == 0x34);
  CHECK(buf[1] == 0x12);
  CHECK(read_u16_le(buf.data()) == 0x1234);

  write_u16_le(buf.data() + 2, 0xFEDC);
  CHECK(buf[2] == 0xDC);
  CHECK(buf[3] == 0xFE);
  CHECK(read_u16_le(buf.data() + 2) == 0xFEDC);
}

TEST_CASE("Util_Endian: write_u32_le encodes and round-trips correctly") {
  std::array<uint8_t, 6> buf = {0, 0, 0, 0, 0, 0};

  write_u32_le(buf.data(), 0x12345678U);
  CHECK(buf[0] == 0x78);
  CHECK(buf[1] == 0x56);
  CHECK(buf[2] == 0x34);
  CHECK(buf[3] == 0x12);
  CHECK(read_u32_le(buf.data()) == 0x12345678U);

  // Unaligned offset write and read round-trip
  write_u32_le(buf.data() + 1, 0xDEADBEEFU);
  CHECK(read_u32_le(buf.data() + 1) == 0xDEADBEEFU);
}
