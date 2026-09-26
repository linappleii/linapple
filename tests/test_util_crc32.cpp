// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>
#include <cstring>
#include <string>

#include "core/Util_Crc32.h"
#include "doctest.h"

TEST_CASE("Util_Crc32: empty and null inputs") {
  CHECK(crc32_compute(nullptr, 0) == 0);
  CHECK(crc32_compute("", 0) == 0);

  const uint32_t state = crc32_init();
  // Nullptr or zero length should preserve state unmodified
  CHECK(crc32_update(state, nullptr, 10) == state);

  const uint8_t byte = 0x42;
  CHECK(crc32_update(state, &byte, 0) == state);
}

TEST_CASE("Util_Crc32: standard golden test vectors") {
  // Standard ITU-T / ISO 3309 test vector: "123456789" -> 0xCBF43926
  const char* check_input = "123456789";
  CHECK(crc32_compute(check_input, std::strlen(check_input)) == 0xCBF43926U);

  // Single character 'a' (0x61) -> 0xE8B7BE43
  const char* single_char = "a";
  CHECK(crc32_compute(single_char, 1) == 0xE8B7BE43U);
}

TEST_CASE("Util_Crc32: streaming equivalence matches one-shot compute") {
  const std::string text = "The quick brown fox jumps over the lazy dog";
  const uint32_t golden = crc32_compute(text.data(), text.size());
  CHECK(golden == 0x414FA339U);

  // Incremental chunked updates
  uint32_t state = crc32_init();
  state = crc32_update(state, text.data(), 10);
  state = crc32_update(state, text.data() + 10, 15);
  state = crc32_update(state, text.data() + 25, text.size() - 25);
  CHECK(crc32_final(state) == golden);
}
