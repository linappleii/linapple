#ifndef _WINDEF_
#define _WINDEF_

#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *HANDLE;

#define MAX_PATH          260

#ifndef NULL
  #ifdef __cplusplus
    #define NULL    0
  #else
    #define NULL    ((void *)0)
  #endif
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef OPTIONAL
#define OPTIONAL
#endif

#undef far
#undef near
#undef pascal

#define far
#define near
#define pascal

#undef FAR
#undef NEAR
#define FAR                 far
#define NEAR                near

#ifndef CONST
#define CONST               const
#endif

typedef float FLOAT;
typedef uint8_t BOOL; // Some code might still expect 0/1 integer behavior

#define MAKEWORD(a, b)      ((uint16_t)(((uint8_t)(a)) | ((uint16_t)((uint8_t)(b))) << 8))
#define MAKELONG(a, b)      ((int32_t)(((uint16_t)(a)) | ((uint32_t)((uint16_t)(b))) << 16))
#define LOWORD(l)           ((uint16_t)(l))
#define HIWORD(l)           ((uint16_t)(((uint32_t)(l) >> 16) & 0xFFFF))
#define LOBYTE(w)           ((uint8_t)(w))
#define HIBYTE(w)           ((uint8_t)(((uint16_t)(w) >> 8) & 0xFF))

typedef uint32_t COLORREF;

#ifndef VOID
#define VOID void
#endif

typedef char WCHAR;
typedef char TCHAR;

typedef struct _OVERLAPPED {
  uint32_t Internal;
  uint32_t InternalHigh;
  uint32_t Offset;
  uint32_t OffsetHigh;
  HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef struct tagPOINT {
  int32_t x;
  int32_t y;
} POINT, *PPOINT, NEAR *NPPOINT, FAR *LPPOINT;

#define __TEXT(quote) quote
#define TEXT(quote) __TEXT(quote)

#define _tcschr strchr
#define _tcscspn strcspn
#define _tcsncat strncat
#define _tcscpy strcpy
#define _tcsncpy strncpy
#define _tcspbrk strpbrk
#define _tcsrchr strrchr
#define _tcsspn strspn
#define _tcsstr strstr
#define _tcstok strtok
#define _tcscmp strcmp
#define _tcsicmp strcasecmp
#define _tcsncmp strncmp
#define _tcsnicmp strncasecmp
#define _tcstoul strtoul
#define _tcstol strtol
#define _tcscat     strcat
#define _tcslen     strlen
#define _tcsxfrm    strxfrm
#define _tcsdup     strdup

#define MoveMemory(Destination, Source, Length) memmove((Destination),(Source),(Length))
#define FillMemory(Destination, Length, Fill) memset((Destination),(Fill),(Length))
#define EqualMemory(Destination, Source, Length) (!memcmp((Destination),(Source),(Length)))
#define CopyMemory(Destination, Source, Length) memcpy((Destination),(Source),(Length))
#define ZeroMemory(Destination, Length) memset((Destination),0,(Length))

static inline bool IsCharLower(char ch) {
  return isascii(ch) && islower(ch);
}

static inline bool IsCharUpper(char ch) {
  return isascii(ch) && isupper(ch);
}

#define FILE_BEGIN   SEEK_SET
#define FILE_CURRENT  SEEK_CUR
#define FILE_END  SEEK_END

#define DeleteFile remove
#define INVALID_HANDLE_VALUE NULL

#ifdef _DEBUG
  #include <cassert>
  #define _ASSERT(x) assert(x)
#else
  #define _ASSERT(x)
#endif

#ifdef __cplusplus
}
#endif

#endif
