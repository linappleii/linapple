#pragma once

#include <cstddef>
#define CHAR_LF           '\x0D'
#define CHAR_CR           '\x0A'
#define CHAR_SPACE        ' '
#define CHAR_TAB          '\t'
#define CHAR_QUOTE_DOUBLE '"'
#define CHAR_QUOTE_SINGLE '\''
#define CHAR_ESCAPE       '\x1B'

inline auto EatEOL                    ( const char *pSrc ) -> const char*
    {
      if (pSrc)
      {
        if (*pSrc == CHAR_LF) {
          pSrc++;
        }

        if (*pSrc == CHAR_CR) {
          pSrc++;
        }
      }
      return pSrc;
    }

inline auto SkipWhiteSpace ( const char *pSrc ) -> const char* {
      while (pSrc && ((*pSrc == CHAR_SPACE) || (*pSrc == CHAR_TAB)))
      {
        pSrc++;
      }
      return pSrc;
    }

inline auto SkipWhiteSpaceReverse (const char *pSrc, const char *pStart) -> const char*
    {
      while (pSrc && ((*pSrc == CHAR_SPACE) || (*pSrc == CHAR_TAB)) && (pSrc > pStart))
      {
        pSrc--;
      }
      return pSrc;
    }

inline auto SkipUntilChar (const char *pSrc, const char nDelim) -> const char*
    {
      while (pSrc && (*pSrc))
      {
        if (*pSrc == nDelim) {
          break;
        }
        pSrc++;
      }
      return pSrc;
    }

inline auto SkipUntilEOL (const char *pSrc) -> const char*
    {
      // EOL delims: NULL, LF, CR
      while (pSrc && (*pSrc))
      {
        if ((*pSrc == CHAR_LF) || (*pSrc == CHAR_CR))
        {
          break;
        }
        pSrc++;
      }
      return pSrc;
    }

inline auto SkipUntilTab (const char *pSrc) -> const char* {
      while (pSrc && (*pSrc))
      {
        if (*pSrc == CHAR_TAB)
        {
          break;
        }
        pSrc++;
      }
      return pSrc;
    }

inline auto SkipUntilWhiteSpace (const char *pSrc) -> const char* {
      while (pSrc && (*pSrc))
      {
        if ((*pSrc == CHAR_SPACE) || (*pSrc == CHAR_TAB))
        {
          break;
        }
        pSrc++;
      }
      return pSrc;
    }

inline auto SkipUntilWhiteSpaceReverse ( const char *pSrc, const char *pStart ) -> const char*
    {
      while (pSrc && (pSrc > pStart))
      {
        if ((*pSrc == CHAR_SPACE) || (*pSrc == CHAR_TAB))
        {
          break;
        }
        pSrc--;
      }
      return pSrc;
    }

/** Assumes text are valid hex digits! */
inline auto TextConvert2CharsToByte ( char *pText ) -> unsigned char {
  unsigned char n = ((pText[0] <= '@') ? (pText[0] - '0') : (pText[0] - 'A' + 10)) << 4;
    n += ((pText[1] <= '@') ? (pText[1] - '0') : (pText[1] - 'A' + 10)) << 0;
  return n;
}

inline auto TextIsHexChar( char nChar ) -> bool {
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

inline auto TextIsHexByte( char *pText ) -> bool {
  if (TextIsHexChar( pText[0] ) &&
    TextIsHexChar( pText[1] )) {
    return true;
  }

  return false;
}

inline auto TextIsHexString ( const char* pText ) -> bool {
  while (*pText) {
    if (! TextIsHexChar( *pText )) {
      return false;
    }

    pText++;
  }
  return true;
}

inline void Util_SafeStrCpy(char* dest, const char* src, size_t size) {
  if (size == 0) return;
  size_t i = 0;
  for (i = 0; i < size - 1 && src[i] != '\0'; i++) {
      dest[i] = src[i];
  }
  dest[i] = '\0';
}
