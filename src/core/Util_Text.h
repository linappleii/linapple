#pragma once

#include <cstddef>
#include <cstdint>
#define CHAR_LF '\x0D'
#define CHAR_CR '\x0A'
#define CHAR_SPACE ' '
#define CHAR_TAB '\t'
#define CHAR_QUOTE_DOUBLE '"'
#define CHAR_QUOTE_SINGLE '\''
#define CHAR_ESCAPE '\x1B'

static inline auto is_char_lower(char ch) -> bool {
  return (ch >= 'a' && ch <= 'z');
}

static inline auto is_char_upper(char ch) -> bool {
  return (ch >= 'A' && ch <= 'Z');
}

inline auto eat_eol(const char* src_ptr) -> const char* {
  if (src_ptr) {
    if (*src_ptr == CHAR_LF) {
      src_ptr++;
    }

    if (*src_ptr == CHAR_CR) {
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

/** Assumes text are valid hex digits! */
inline auto text_convert_2_chars_to_byte(char* text) -> uint8_t {
  uint8_t n = ((text[0] <= '@') ? (text[0] - '0') : (text[0] - 'A' + 10)) << 4;
  n += ((text[1] <= '@') ? (text[1] - '0') : (text[1] - 'A' + 10)) << 0;
  return n;
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

inline void Util_SafeStrCpy(char* dest, const char* src, size_t size) {
  if (size == 0) {
    return;
  }
  size_t i = 0;
  for (i = 0; i < size - 1 && src[i] != '\0'; i++) {
    dest[i] = src[i];
  }
  dest[i] = '\0';
}
