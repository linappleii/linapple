// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstring>

#include "core/Util_Text.h"
#include "doctest.h"

TEST_CASE(
    "Util_Text: util_safe_strcpy copies correctly and handles boundaries") {
  std::array<char, 8> dest{};

  // Normal copy
  util_safe_strcpy(dest.data(), "hello", dest.size());
  CHECK(std::strcmp(dest.data(), "hello") == 0);

  // Exact fit (7 chars + null terminator)
  util_safe_strcpy(dest.data(), "1234567", dest.size());
  CHECK(std::strcmp(dest.data(), "1234567") == 0);

  // Truncation (8 chars -> 7 copied + null terminator)
  util_safe_strcpy(dest.data(), "12345678", dest.size());
  CHECK(std::strcmp(dest.data(), "1234567") == 0);
  CHECK(dest[7] == '\0');

  // Empty string
  util_safe_strcpy(dest.data(), "", dest.size());
  CHECK(dest[0] == '\0');

  // Defensive null / zero-size checks
  dest[0] = 'X';
  util_safe_strcpy(dest.data(), "abc", 0);
  CHECK(dest[0] == 'X');

  util_safe_strcpy(nullptr, "abc", dest.size());

  util_safe_strcpy(dest.data(), nullptr, dest.size());
  CHECK(dest[0] == 'X');

  // Self-copy is a safe no-op
  util_safe_strcpy(dest.data(), "hello", dest.size());
  util_safe_strcpy(dest.data(), dest.data(), dest.size());
  CHECK(std::strcmp(dest.data(), "hello") == 0);
}

TEST_CASE("Util_Text: util_safe_strncat concatenates and handles boundaries") {
  std::array<char, 10> dest{};
  dest[0] = '\0';

  util_safe_strncat(dest.data(), "foo", dest.size());
  CHECK(std::strcmp(dest.data(), "foo") == 0);

  util_safe_strncat(dest.data(), "bar", dest.size());
  CHECK(std::strcmp(dest.data(), "foobar") == 0);

  // Truncation on append
  util_safe_strncat(dest.data(), "12345", dest.size());
  CHECK(std::strcmp(dest.data(), "foobar123") == 0);
  CHECK(dest[9] == '\0');

  // Appending when already full
  util_safe_strncat(dest.data(), "extra", dest.size());
  CHECK(std::strcmp(dest.data(), "foobar123") == 0);

  // Defensive null / zero-size checks
  util_safe_strncat(nullptr, "test", dest.size());
  util_safe_strncat(dest.data(), nullptr, dest.size());
  util_safe_strncat(dest.data(), "test", 0);
}

TEST_CASE("Util_Text: hex conversion and validation") {
  CHECK(hex_char_to_val('0') == 0);
  CHECK(hex_char_to_val('9') == 9);
  CHECK(hex_char_to_val('A') == 10);
  CHECK(hex_char_to_val('F') == 15);
  CHECK(hex_char_to_val('a') == 10);
  CHECK(hex_char_to_val('f') == 15);
  CHECK(hex_char_to_val('g') == 0);

  CHECK(text_convert_2_chars_to_byte("00") == 0x00);
  CHECK(text_convert_2_chars_to_byte("A5") == 0xA5);
  CHECK(text_convert_2_chars_to_byte("ff") == 0xFF);
  CHECK(text_convert_2_chars_to_byte(nullptr) == 0);

  CHECK(text_is_hex_char('0'));
  CHECK(text_is_hex_char('F'));
  CHECK(text_is_hex_char('a'));
  CHECK(!text_is_hex_char('G'));
  CHECK(!text_is_hex_char(' '));

  CHECK(text_is_hex_byte("1A"));
  CHECK(!text_is_hex_byte("1G"));
  CHECK(!text_is_hex_byte(nullptr));

  CHECK(text_is_hex_string("0123456789ABCDEFabcdef"));
  CHECK(!text_is_hex_string("1234Z"));
  CHECK(!text_is_hex_string(""));
  CHECK(!text_is_hex_string(nullptr));
}
