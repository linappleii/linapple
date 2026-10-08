// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
constexpr uint8_t hex_alpha_base = 10;

inline auto hex_char_to_val(char c) noexcept -> uint8_t {
  if (c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0');
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<uint8_t>(c - 'A' + hex_alpha_base);
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<uint8_t>(c - 'a' + hex_alpha_base);
  }
  return 0;
}

inline auto text_convert_2_chars_to_byte(const char* text) noexcept -> uint8_t {
  if (text == nullptr || text[0] == '\0' || text[1] == '\0') {
    return 0;
  }
  return static_cast<uint8_t>((hex_char_to_val(text[0]) << 4) |
                              hex_char_to_val(text[1]));
}

inline auto text_is_hex_char(char ch) noexcept -> bool {
  return ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') ||
          (ch >= 'a' && ch <= 'f'));
}

inline auto text_is_hex_byte(const char* text) noexcept -> bool {
  if (text == nullptr) {
    return false;
  }
  return text_is_hex_char(text[0]) && text_is_hex_char(text[1]);
}

inline auto text_is_hex_string(const char* text) noexcept -> bool {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  while (*text != '\0') {
    if (!text_is_hex_char(*text)) {
      return false;
    }
    text++;
  }
  return true;
}

inline auto util_safe_strcpy(char* dest, const char* src, size_t size) noexcept
    -> void {
  if (dest == nullptr || src == nullptr || size == 0 || dest == src) {
    return;
  }
  size_t i = 0;
  for (i = 0; i < size - 1 && src[i] != '\0'; ++i) {
    dest[i] = src[i];
  }
  dest[i] = '\0';
}

inline auto util_safe_strncat(char* dest, const char* src, size_t size) noexcept
    -> void {
  if (dest == nullptr || src == nullptr || size == 0) {
    return;
  }
  size_t dest_len = 0;
  while (dest_len < size && dest[dest_len] != '\0') {
    dest_len++;
  }
  if (dest_len >= size) {
    dest[size - 1] = '\0';
    return;
  }
  size_t i = 0;
  while (dest_len + 1 < size && src[i] != '\0') {
    dest[dest_len++] = src[i++];
  }
  dest[dest_len] = '\0';
}
