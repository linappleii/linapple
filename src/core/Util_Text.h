#pragma once

#include <cstddef>
#include <cstdint>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-type-member-init)

constexpr char CHAR_CR = '\r';  // 0x0D
constexpr char CHAR_LF = '\n';  // 0x0A
constexpr char CHAR_SPACE = ' ';
constexpr char CHAR_TAB = '\t';
constexpr char CHAR_QUOTE_DOUBLE = '"';
constexpr char CHAR_QUOTE_SINGLE = '\'';
constexpr char CHAR_ESCAPE = '\x1B';

static inline auto is_char_lower(char ch) -> bool {
  return (ch >= 'a' && ch <= 'z');
}

static inline auto is_char_upper(char ch) -> bool {
  return (ch >= 'A' && ch <= 'Z');
}

inline auto eat_eol(const char* src_ptr) -> const char* {
  if (src_ptr) {
    if (*src_ptr == CHAR_CR) {
      src_ptr++;
    }
    if (*src_ptr == CHAR_LF) {
      src_ptr++;
    }
  }
  return src_ptr;
}

inline auto skip_white_space(const char* src_ptr) -> const char* {
  while (src_ptr && ((*src_ptr == CHAR_SPACE) || (*src_ptr == CHAR_TAB))) {
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_white_space_reverse(const char* src_ptr, const char* start)
    -> const char* {
  while (src_ptr && ((*src_ptr == CHAR_SPACE) || (*src_ptr == CHAR_TAB)) &&
         (src_ptr > start)) {
    src_ptr--;
  }
  return src_ptr;
}

inline auto skip_until_char(const char* src_ptr, const char delim) -> const
    char* {
  while (src_ptr && (*src_ptr)) {
    if (*src_ptr == delim) {
      break;
    }
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_until_eol(const char* src_ptr) -> const char* {
  // EOL delims: NUL, LF, CR
  while (src_ptr && (*src_ptr)) {
    if ((*src_ptr == CHAR_LF) || (*src_ptr == CHAR_CR)) {
      break;
    }
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_until_tab(const char* src_ptr) -> const char* {
  while (src_ptr && (*src_ptr)) {
    if (*src_ptr == CHAR_TAB) {
      break;
    }
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_until_white_space(const char* src_ptr) -> const char* {
  while (src_ptr && (*src_ptr)) {
    if ((*src_ptr == CHAR_SPACE) || (*src_ptr == CHAR_TAB)) {
      break;
    }
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_until_white_space_reverse(const char* src_ptr,
                                           const char* start) -> const char* {
  while (src_ptr && (src_ptr > start)) {
    if ((*src_ptr == CHAR_SPACE) || (*src_ptr == CHAR_TAB)) {
      break;
    }
    src_ptr--;
  }
  return src_ptr;
}

constexpr uint8_t HEX_ALPHA_OFFSET = 10;
constexpr uint8_t NIBBLE_SHIFT = 4;

inline auto hex_char_to_val(char c) -> uint8_t {
  if (c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0');
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<uint8_t>(c - 'A' + HEX_ALPHA_OFFSET);
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<uint8_t>(c - 'a' + HEX_ALPHA_OFFSET);
  }
  return 0;
}

/** Assumes text are valid hex digits! */
inline auto text_convert_2_chars_to_byte(const char* text) -> uint8_t {
  return static_cast<uint8_t>((hex_char_to_val(text[0]) << NIBBLE_SHIFT) |
                              hex_char_to_val(text[1]));
}

inline auto text_is_hex_char(char ch) -> bool {
  if ((ch >= '0') && (ch <= '9')) {
    return true;
  }

  if ((ch >= 'A') && (ch <= 'F')) {
    return true;
  }

  if ((ch >= 'a') && (ch <= 'f')) {
    return true;
  }

  return false;
}

inline auto text_is_hex_byte(char* text) -> bool {
  if (text_is_hex_char(text[0]) && text_is_hex_char(text[1])) {
    return true;
  }

  return false;
}

inline auto text_is_hex_string(const char* text) -> bool {
  while (*text) {
    if (!text_is_hex_char(*text)) {
      return false;
    }

    text++;
  }
  return true;
}

inline void util_safe_strcpy(char* dest, const char* src, size_t size) {
  if (size == 0) {
    return;
  }
  size_t i = 0;
  for (i = 0; i < size - 1 && src[i] != '\0'; i++) {
    dest[i] = src[i];
  }
  dest[i] = '\0';
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-type-member-init)
