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

inline auto eat_eol(const char* pSrc) -> const char* {
  if (pSrc) {
    if (*pSrc == CHAR_LF) {
      pSrc++;
    }

    if (*pSrc == CHAR_CR) {
      pSrc++;
    }
  }
  return pSrc;
}

inline auto skip_white_space(const char* pSrc) -> const char* {
  while (pSrc && ((*pSrc == CHAR_SPACE) || (*pSrc == CHAR_TAB))) {
    pSrc++;
  }
  return pSrc;
}

inline auto skip_white_space_reverse(const char* pSrc, const char* pStart) -> const
    char* {
  while (pSrc && ((*pSrc == CHAR_SPACE) || (*pSrc == CHAR_TAB)) &&
         (pSrc > pStart)) {
    pSrc--;
  }
  return pSrc;
}

inline auto skip_until_char(const char* pSrc, const char nDelim) -> const char* {
  while (pSrc && (*pSrc)) {
    if (*pSrc == nDelim) {
      break;
    }
    pSrc++;
  }
  return pSrc;
}

inline auto skip_until_eol(const char* pSrc) -> const char* {
  // EOL delims: NULL, LF, CR
  while (pSrc && (*pSrc)) {
    if ((*pSrc == CHAR_LF) || (*pSrc == CHAR_CR)) {
      break;
    }
    pSrc++;
  }
  return pSrc;
}

inline auto skip_until_tab(const char* pSrc) -> const char* {
  while (pSrc && (*pSrc)) {
    if (*pSrc == CHAR_TAB) {
      break;
    }
    pSrc++;
  }
  return pSrc;
}

inline auto skip_until_white_space(const char* pSrc) -> const char* {
  while (pSrc && (*pSrc)) {
    if ((*pSrc == CHAR_SPACE) || (*pSrc == CHAR_TAB)) {
      break;
    }
    pSrc++;
  }
  return pSrc;
}

inline auto skip_until_white_space_reverse(const char* pSrc, const char* pStart)
    -> const char* {
  while (pSrc && (pSrc > pStart)) {
    if ((*pSrc == CHAR_SPACE) || (*pSrc == CHAR_TAB)) {
      break;
    }
    pSrc--;
  }
  return pSrc;
}

/** Assumes text are valid hex digits! */
inline auto text_convert_2_chars_to_byte(char* pText) -> uint8_t {
  uint8_t n = ((pText[0] <= '@') ? (pText[0] - '0') : (pText[0] - 'A' + 10))
              << 4;
  n += ((pText[1] <= '@') ? (pText[1] - '0') : (pText[1] - 'A' + 10)) << 0;
  return n;
}

inline auto text_is_hex_char(char nChar) -> bool {
  if ((nChar >= '0') && (nChar <= '9')) {
    return true;
  }

  if ((nChar >= 'A') && (nChar <= 'F')) {
    return true;
  }

  if ((nChar >= 'a') && (nChar <= 'f')) {
    return true;
  }

  return false;
}

inline auto text_is_hex_byte(char* pText) -> bool {
  if (text_is_hex_char(pText[0]) && text_is_hex_char(pText[1])) {
    return true;
  }

  return false;
}

inline auto text_is_hex_string(const char* pText) -> bool {
  while (*pText) {
    if (!text_is_hex_char(*pText)) {
      return false;
    }

    pText++;
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
