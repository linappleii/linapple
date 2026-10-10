// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdarg>
#include <cstdint>

#include "Debugger_Types.h"

enum : uint16_t {
  // Basic Symbol table has > 600 symbols
  // Lines, was 128, but need ~ 256+16 for PROFILE LIST
  // Output
  CONSOLE_HEIGHT = 768,

  // need min 256+ lines for "profile list"
  CONSOLE_BUFFER_HEIGHT = CONSOLE_HEIGHT,

  // Display viewport is at most 384 / 8 = 48 lines (MAX_DISPLAY_LINES)
  CONSOLE_DISPLAY_HEIGHT = 48,

  // Input
  HISTORY_HEIGHT = 128,
  HISTORY_WIDTH = 128,

  CONSOLE_FIRST_LINE = 1,  // where ConsoleDisplay is pushed up from
};

// Color ____________________________________________________________________

// typedef uint8_t ConChar;
using ConChar = int16_t;

// NOTE: Keep in sync ConsoleColors console_color !
enum ConsoleColors : uint8_t {
  CONSOLE_COLOR_K,      // 0
  CONSOLE_COLOR_x = 0,  // default console foreground
  CONSOLE_COLOR_R,      // 1 Red
  CONSOLE_COLOR_G,      // 2 Green
  CONSOLE_COLOR_Y,      // 3 Yellow
  CONSOLE_COLOR_B,      // 4 Blue
  CONSOLE_COLOR_M,      // 5 Magenta/Purple
  CONSOLE_COLOR_C,      // 6 Cyan
  CONSOLE_COLOR_W,      // 7 White
  CONSOLE_COLOR_O,      // 8 Orange
  CONSOLE_COLOR_k,      // 9 Grey
  CONSOLE_COLOR_b,      // : Light Blue
  NUM_CONSOLE_COLORS,
};
extern int console_color[NUM_CONSOLE_COLORS];

// Note: THe ` ~ key should always display ~ to prevent rendering errors
constexpr char CONSOLE_COLOR_ESCAPE_CHAR = '`';
constexpr uint8_t CONSOLE_COLOR_MASK = 0x7F;
constexpr uint8_t CONSOLE_COLOR_SHIFT = 8;
constexpr size_t CONSOLE_INPUT_EXTRA = 16;

// Console Help Color
constexpr const char* CHC_DEFAULT = "`0";
constexpr const char* CHC_USAGE = "`3";
constexpr const char* CHC_CATEGORY = "`6";
constexpr const char* CHC_COMMAND = "`2";   // Green
constexpr const char* CHC_KEY = "`1";       // Red
constexpr const char* CHC_ARG_MAND = "`7";  // < >
constexpr const char* CHC_ARG_OPT = "`4";   // [ ]
constexpr const char* CHC_ARG_SEP = "`9";   //  |  grey
constexpr const char* CHC_NUM_DEC =
    "`6";  // cyan looks better then yellow (SearchMemoryDisplay), S D000:FFFF
           // A9 00, PROFILE, HELP BP
constexpr const char* CHC_NUM_HEX = "`3";
constexpr const char* CHC_SYMBOL = "`2";   // Symbols
constexpr const char* CHC_ADDRESS = "`8";  // Hex Address
constexpr const char* CHC_ERROR = "`1";    // Red
constexpr const char* CHC_WARNING = "`5";  // Purple
constexpr const char* CHC_INFO = "`3";     // Yellow
constexpr const char* CHC_STRING = "`6";
constexpr const char* CHC_EXAMPLE = "`:";
constexpr const char* CHC_PATH = "`:";  // Light Blue

// ascii markup
inline auto ConsoleColor_IsCharMeta(uint8_t c) -> bool {
  return CONSOLE_COLOR_ESCAPE_CHAR == c;
}

inline auto ConsoleColor_IsCharColor(uint8_t c) -> bool {
  return (c >= '0') && ((c - '0') < NUM_CONSOLE_COLORS);
}

// Console "Native" Chars
//
// There are a few different ways of encoding color chars & mouse text
// Simplist method is to use a user-defined ESCAPE char to shift
// into color mode, or mouse text mode.  The other solution
// is to use a wide-char, simulating unicode16.
//
//    C1C0 char16 of High Byte (c1) and Low Byte (c0)
// 1) --??  Con: Colors chars take up extra chars.
//          Con: String Length is complicated.
//          Pro: simple to parse
//
// <-- WE USE THIS
// 2) ccea  Pro: Efficient packing of plain text and mouse text
//          Pro: Color is optional (only record new color)
//          Con: need to provide char8 and char16 API
//          Con: little more difficult to parse/convert plain text
//          i.e.
//       ea = 0x20 - 0x7F ASCII
//            0x80 - 0xFF Mouse Text  '@'-'Z' -> 0x00 - 0x1F
//    cc    = ASCII '0' - '9' (color)
// 3) ??cc  Con: Colors chars take up extra chars
// 4)  f??  Con: Colors chars take up extra chars
//
// Legend:
//       f Flag
//      -- Not Applicable (n/a)
//      ?? ASCII (0x20 - 0x7F)
//      ea Extended ASCII with High-Bit representing Mouse Text
//      cc Encoded Color / Mouse Text
//
inline auto ConsoleColor_IsColorOrMouse(ConChar g) -> bool {
  return g > CONSOLE_COLOR_MASK;
}

inline auto ConsoleColor_IsColor(ConChar g) -> bool {
  return ConsoleColor_IsCharColor(g >> CONSOLE_COLOR_SHIFT);
}

inline auto ConsoleColor_GetColor(ConChar g) -> uint32_t {
  const int iColor = (g >> CONSOLE_COLOR_SHIFT) - '0';
  if (iColor < NUM_CONSOLE_COLORS) {
    return console_color[iColor];
  }

  return console_color[0];
}

inline auto ConsoleColor_GetMeta(ConChar g) -> char {
  return ((g >> CONSOLE_COLOR_SHIFT) & CONSOLE_COLOR_MASK);
}

inline auto ConsoleChar_GetChar(ConChar g) -> char {
  return (g & CONSOLE_COLOR_MASK);
}

inline auto ConsoleColor_MakeMouse(uint8_t c) -> char {
  return ((c - '@') + (CONSOLE_COLOR_MASK + 1));
}

inline auto ConsoleColor_MakeMeta(uint8_t c) -> ConChar {
  ConChar g = (ConsoleColor_MakeMouse(c) << CONSOLE_COLOR_SHIFT);
  return g;
}

inline auto ConsoleColor_MakeColor(uint8_t color, uint8_t text) -> ConChar {
  ConChar g = (color << CONSOLE_COLOR_SHIFT) | text;
  return g;
}

// Return the string length without the markup
inline auto ConsoleColor_StringLength(const char* text) -> int {
  const char* src_ptr = text;
  int length = 0;
  while (*src_ptr != '\0') {
    if (ConsoleColor_IsCharMeta(*src_ptr)) {
      src_ptr++;  // Skip meta character
      if (*src_ptr != '\0') {
        src_ptr++;  // Skip color code
      }
    } else {
      length++;
      src_ptr++;
    }
  }
  return length;
}

// Globals __________________________________________________________________

// Buffer
extern bool console_buffer_paused;
extern int console_buffer_size;
extern ConChar
    console_buffer[CONSOLE_BUFFER_HEIGHT]
                    [CONSOLE_WIDTH];  // TODO: std::vector< Line >

// Cursor
extern char console_cursor[];

// Display
extern char console_prompt[];  // = ">!"; // input, assembler // NUM_PROMPTS
extern char
    console_prompt_str[];  // = ">"; // No, NOT Integer Basic!  The nostalgic
                             // '*' "Monitor" doesn't look as good, IMHO. :-(
extern int console_prompt_len;

extern bool console_full_width;  // = false;

extern int console_display_start;  // to allow scrolling
extern int console_display_total;  // number of lines added to console
extern int console_display_lines;
extern int console_display_width;
extern ConChar console_display[CONSOLE_DISPLAY_HEIGHT][CONSOLE_WIDTH];

// Input History
extern int history_lines_start;  // = 0;
extern int history_lines_total;  // = 0; // number of commands entered
extern char history_lines[HISTORY_HEIGHT][HISTORY_WIDTH];  // = {""};

// Input Line
// Raw input Line (has prompt)
extern char console_input[CONSOLE_WIDTH + CONSOLE_INPUT_EXTRA];

// Cooked input line (no prompt)
extern int console_input_chars;
extern char* console_input_ptr;        // points to past prompt
extern const char* console_first_arg;  // points to first arg
extern bool console_input_quoted;

extern int console_input_skip;

// Prototypes _______________________________________________________________

// Console

// Buffered
auto console_print(const char* text) -> bool;
auto ConsolePrintVa(char* buf, size_t bufsz, const char* pFormat, va_list va)
    -> bool;
template <size_t BufSize>
inline auto ConsolePrintVa(char (&buf)[BufSize], const char* pFormat,
                           va_list va) -> bool {
  return ConsolePrintVa(buf, BufSize, pFormat, va);
}
inline auto ConsolePrintFormat(char* buf, size_t bufsz, const char* pFormat,
                               ...) -> bool {
  va_list va;
  va_start(va, pFormat);
  bool const r = ConsolePrintVa(buf, bufsz, pFormat, va);
  va_end(va);
  return r;
}
template <size_t BufSize>
inline auto ConsolePrintFormat(char (&buf)[BufSize], const char* pFormat, ...)
    -> bool {
  va_list va;
  va_start(va, pFormat);
  bool const r = ConsolePrintVa(buf, pFormat, va);
  va_end(va);
  return r;
}

auto ConsoleBufferToDisplay() -> void;
auto ConsoleBufferPeek() -> const ConChar*;
auto ConsoleBufferPop() -> void;

auto ConsoleBufferPush(const char* text) -> bool;
auto ConsoleBufferPushVa(char* buf, size_t bufsz, const char* pFormat,
                         va_list va) -> bool;
template <size_t BufSize>
inline auto ConsoleBufferPushVa(char (&buf)[BufSize], const char* pFormat,
                                va_list va) -> bool {
  return ConsoleBufferPushVa(buf, BufSize, pFormat, va);
}
inline auto ConsoleBufferPushFormat(char* buf, size_t bufsz,
                                    const char* pFormat, ...) -> bool {
  va_list va;
  va_start(va, pFormat);
  bool const r = ConsoleBufferPushVa(buf, bufsz, pFormat, va);
  va_end(va);
  return r;
}
template <size_t BufSize>
inline auto ConsoleBufferPushFormat(char (&buf)[BufSize], const char* pFormat,
                                    ...) -> bool {
  va_list va;
  va_start(va, pFormat);
  bool const r = ConsoleBufferPushVa(buf, pFormat, va);
  va_end(va);
  return r;
}

auto ConsoleConvertFromText(ConChar* sText, const char* text) -> void;

// Display
auto console_display_error(const char* text) -> UpdateResult;
auto ConsoleDisplayPause() -> void;
auto ConsoleDisplayPush(const char* text) -> void;
auto ConsoleDisplayPush(const ConChar* text) -> void;
auto ConsoleUpdate() -> UpdateResult;
auto ConsoleFlush() -> void;

// Input
auto ConsoleInputToDisplay() -> void;
auto ConsoleInputPeek() -> const char*;
auto ConsoleInputClear() -> bool;
auto ConsoleInputBackSpace() -> bool;
auto ConsoleInputChar(char ch) -> bool;
auto ConsoleInputReset() -> void;
auto ConsoleInputTabCompletion() -> int;

auto ConsoleUpdateCursor(char ch) -> void;

auto ConsoleBufferTryUnpause(int nLines) -> UpdateResult;

// Scrolling
auto ConsoleScrollHome() -> UpdateResult;
auto ConsoleScrollEnd() -> UpdateResult;
auto ConsoleScrollUp(int nLines) -> UpdateResult;
auto ConsoleScrollDn(int nLines) -> UpdateResult;
auto ConsoleScrollPageUp() -> UpdateResult;
auto ConsoleScrollPageDn() -> UpdateResult;
