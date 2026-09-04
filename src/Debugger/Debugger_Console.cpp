// SPDX-License-Identifier: GPL-2.0-only
#include "Debugger_Types.h"
#include "apple2/Video.h"
/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2010, Tom Charlesworth, Michael Pohoreski
Copyright (C) 2020, Thorsten Brehm

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Debugger Console support
 *
 * Author: Copyright (C) 2006 - 2010 Michael Pohoreski
 */

#include <unistd.h>

#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Debug.h"
#include "Debugger_Cmd_CPU.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Parser.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "core/Util_Text.h"

// Globals originally from Debug.cpp
const char g_input_cursor[] = "_\x7F";  // insert over-write
bool g_input_cursor_visible = false;
int g_input_cursor_index = CURSOR_OVERSTRIKE;  // which cursor to use
const int g_input_cursor_count = sizeof(g_input_cursor);

bool g_ignore_next_key = false;

extern bool g_debug_full_speed;

auto ConsoleInputHistoryPrev() -> Update_t;
auto ConsoleInputHistoryNext() -> Update_t;

// Console
// ________________________________________________________________________________________

// See ConsoleInputReset() for why the console input
// is tied to the zero'th output of g_console_display
// and not using a seperate var: g_console_input[ CONSOLE_WIDTH ];
//
//          :          g_console_buffer[4] |      ^ g_console_display[5] : :
//          g_console_buffer[3] |      | g_console_display[4]  <-
//          g_console_display_total
// g_console_buffer_size -> g_console_buffer[2] |      | g_console_display[3] :
//          :          g_console_buffer[1] v      | g_console_display[2] : .
//          g_console_buffer[0] -----> | g_console_display[1]        .
//                                                |
// g_buffered_input[0] -----> ConsoleInput ---->  | g_console_display[0]
// g_buffered_input[1] ^
// g_buffered_input[2] |
// g_buffered_input[3] |

// Buffer
bool g_console_buffer_paused =
    false;  // buffered output is waiting for user to continue
int g_console_buffer_size = 0;
conchar_t g_console_buffer[CONSOLE_BUFFER_HEIGHT]
                          [CONSOLE_WIDTH];  // TODO: std::vector< line_t >

// Cursor
char g_console_cursor[] = "_";

// Display
char g_console_prompt[] = ">!";     // input, assembler // NUM_PROMPTS
char g_console_prompt_str[] = ">";  // No, NOT Integer Basic!  The nostalgic '*'
                                    // "Monitor" doesn't look as good, IMHO. :-(
int g_console_prompt_len = 1;

bool g_console_full_width = true;  // false

int g_console_display_start = 0;  // to allow scrolling
int g_console_display_total = 0;  // number of lines added to console
int g_console_display_lines = 0;
int g_console_display_width = 0;
conchar_t g_console_display[CONSOLE_HEIGHT][CONSOLE_WIDTH];

// Input History
int g_history_lines_start = 0;
int g_history_lines_total = 0;  // number of commands entered
char g_history_lines[HISTORY_HEIGHT][HISTORY_WIDTH] = {""};

// Input Line

// Raw input Line (has prompt)
char g_console_input[CONSOLE_WIDTH + 16];  // = g_console_display[0];

// Cooked input line (no prompt)
int g_console_input_chars = 0;
char* g_console_input_ptr = nullptr;        // points to past prompt
const char* g_console_first_arg = nullptr;  // points to first arg
bool g_console_input_quoted = false;        // Allows lower-case to be entered
int g_console_input_skip = 0;

int g_console_color[NUM_CONSOLE_COLORS] = {
    WHITE, RED,   GREEN,  YELLOW,     BLUE,      MAGENTA,
    CYAN,  WHITE, ORANGE, LIGHT_GRAY, LIGHT_BLUE};
// Prototypes _______________________________________________________________

// Console
// ________________________________________________________________________________________

auto ConsoleLineLength(const conchar_t* text) -> int {
  int nLen = 0;
  const conchar_t* src_ptr = text;

  if (text) {
    while (*src_ptr) {
      src_ptr++;
    }
    nLen = src_ptr - text;
  }
  return nLen;
}

//===========================================================================
auto ConsoleBufferPeek() -> const conchar_t* { return g_console_buffer[0]; }

//===========================================================================
auto console_print(const char* text) -> bool {
  while (g_console_buffer_size >= CONSOLE_BUFFER_HEIGHT) {
    ConsoleBufferToDisplay();
  }

  // Convert color string to native console color text
  // Ignores g_console_display_width
  char c = 0;

  int x = 0;
  const char* src_ptr = text;
  conchar_t* pDst = &g_console_buffer[g_console_buffer_size][0];

  conchar_t g = 0;
  bool bHaveColor = false;
  char cColor = 0;

  while ((x < CONSOLE_WIDTH) && (c = *src_ptr)) {
    if ((c == '\n') || (x >= (CONSOLE_WIDTH - 1))) {
      *pDst = 0;
      x = 0;
      if (g_console_buffer_size >= CONSOLE_BUFFER_HEIGHT) {
        ConsoleBufferToDisplay();
      } else {
        g_console_buffer_size++;
      }
      pDst = &g_console_buffer[g_console_buffer_size][0];
      continue;
    }

    g = (c & CONSOLE_COLOR_MASK);

    // `# `A  color encode mouse text
    if (!ConsoleColor_IsCharMeta(c)) {
      if (bHaveColor) {
        g = ConsoleColor_MakeColor(cColor, c);
        bHaveColor = false;
      }
      *pDst = g;
      x++;
      pDst++;
      src_ptr++;
      continue;
    }

    if (!src_ptr[1]) {
      break;
    }

    if (ConsoleColor_IsCharMeta(src_ptr[1])) {  // ` `
      bHaveColor = false;
      cColor = 0;
      g = ConsoleColor_MakeColor(cColor, c);
      *pDst = g;
      x++;
      pDst++;
    } else if (ConsoleColor_IsCharColor(src_ptr[1])) {  // ` #
      cColor = src_ptr[1];
      bHaveColor = true;
    } else {  // ` @
      c = ConsoleColor_MakeMouse(src_ptr[1]);
      g = ConsoleColor_MakeColor(cColor, c);
      *pDst = g;
      x++;
      pDst++;
    }
    src_ptr += 2;
  }
  *pDst = 0;
  g_console_buffer_size++;

  return true;
}

auto ConsolePrintVa(char* buf, size_t bufsz, const char* pFormat, va_list va)
    -> bool {
  vsnprintf(buf, bufsz, pFormat, va);
  return console_print(buf);
}

auto ConsoleBufferPushVa(char* buf, size_t bufsz, const char* pFormat,
                         va_list va) -> bool {
  vsnprintf(buf, bufsz, pFormat, va);
  return ConsoleBufferPush(buf);
}

// Add string to buffered output
// Shifts the buffered console output lines "Up"
//===========================================================================
auto ConsoleBufferPush(const char* text) -> bool {
  while (g_console_buffer_size >= CONSOLE_BUFFER_HEIGHT) {
    ConsoleBufferToDisplay();
  }

  conchar_t c = 0;

  int x = 0;
  const char* src_ptr = text;
  conchar_t* pDst = &g_console_buffer[g_console_buffer_size][0];

  while ((x < CONSOLE_WIDTH) && *src_ptr) {
    c = *src_ptr;
    if ((c == '\n') || (x == (CONSOLE_WIDTH - 1))) {
      *pDst = 0;
      x = 0;
      if (g_console_buffer_size >= CONSOLE_BUFFER_HEIGHT) {
        ConsoleBufferToDisplay();
      } else {
        g_console_buffer_size++;
      }
      src_ptr++;
      pDst = &g_console_buffer[g_console_buffer_size][0];
      continue;
    }

    *pDst = (c & CONSOLE_COLOR_MASK);
    x++;
    src_ptr++;
    pDst++;
  }
  *pDst = 0;
  g_console_buffer_size++;

  return true;
}

// Shifts the buffered console output "down"
//===========================================================================
auto ConsoleBufferPop() -> void {
  int y = 0;
  while (y < g_console_buffer_size) {
    memcpy(g_console_buffer[y], g_console_buffer[y + 1],
           sizeof(conchar_t) * CONSOLE_WIDTH);
    y++;
  }

  g_console_buffer_size--;
  if (g_console_buffer_size < 0) {
    g_console_buffer_size = 0;
  }
}

// Remove string from buffered output
//===========================================================================
auto ConsoleBufferToDisplay() -> void {
  ConsoleDisplayPush(ConsoleBufferPeek());
  ConsoleBufferPop();
}

// No mark-up. Straight ASCII conversion
//===========================================================================
auto ConsoleConvertFromText(conchar_t* sText, const char* text) -> void {
  const char* src_ptr = text;
  conchar_t* pDst = sText;
  while (src_ptr && *src_ptr) {
    *pDst = static_cast<conchar_t>(*src_ptr & CONSOLE_COLOR_MASK);
    src_ptr++;
    pDst++;
  }
  *pDst = 0;
}

//===========================================================================
auto console_display_error(const char* text) -> Update_t {
  ConsoleBufferPush(text);
  return ConsoleUpdate();
}

//===========================================================================
auto ConsoleDisplayPush(const char* text) -> void {
  conchar_t sText[CONSOLE_WIDTH * 2];
  ConsoleConvertFromText(sText, text);
  ConsoleDisplayPush(sText);
}

// Shifts the console display lines "up"
//===========================================================================
auto ConsoleDisplayPush(const conchar_t* text) -> void {
  int nLen =
      MIN(g_console_display_total, CONSOLE_HEIGHT - 1 - CONSOLE_FIRST_LINE);
  while (nLen--) {
    memcpy(
        reinterpret_cast<char*>(
            g_console_display[(nLen + 1 + CONSOLE_FIRST_LINE)]),
        reinterpret_cast<char*>(g_console_display[nLen + CONSOLE_FIRST_LINE]),
        sizeof(conchar_t) * CONSOLE_WIDTH);
  }

  if (text) {
    memcpy(reinterpret_cast<char*>(g_console_display[CONSOLE_FIRST_LINE]), text,
           sizeof(conchar_t) * CONSOLE_WIDTH);
  }

  g_console_display_total++;
  if (g_console_display_total > (CONSOLE_HEIGHT - CONSOLE_FIRST_LINE)) {
    g_console_display_total = (CONSOLE_HEIGHT - CONSOLE_FIRST_LINE);
  }
}

//===========================================================================
auto ConsoleDisplayPause() -> void {
  if (g_console_buffer_size) {
#if CONSOLE_INPUT_CHAR16
    ConsoleConvertFromText(g_console_input,
                           "...press SPACE continue, ESC skip...");
    g_console_prompt_len = ConsoleLineLength(g_console_input);
#else
    util_safe_strcpy(g_console_input, "...press SPACE continue, ESC skip...",
                     sizeof(g_console_input));
    g_console_prompt_len = static_cast<int>(strlen(g_console_input));
#endif
    g_console_input_ptr = &g_console_input[g_console_prompt_len];
    g_console_input_chars = 0;
    g_console_buffer_paused = true;
  } else {
    ConsoleInputReset();
  }
}

//===========================================================================
auto ConsoleInputBackSpace() -> bool {
  if (g_console_input_chars) {
    g_console_input_ptr[g_console_input_chars] = CHAR_SPACE;

    g_console_input_chars--;

    if ((g_console_input_ptr[g_console_input_chars] == CHAR_QUOTE_DOUBLE) ||
        (g_console_input_ptr[g_console_input_chars] == CHAR_QUOTE_SINGLE)) {
      g_console_input_quoted = !g_console_input_quoted;
    }

    g_console_input_ptr[g_console_input_chars] = CHAR_SPACE;
    return true;
  }
  return false;
}

// Clears prompt too
//===========================================================================
auto ConsoleInputClear() -> bool {
  memset(g_console_input, 0, sizeof(g_console_input));

  if (g_console_input_chars) {
    g_console_input_chars = 0;
    return true;
  }
  return false;
}

//===========================================================================
auto ConsoleInputChar(const char ch) -> bool {
  if (g_console_input_chars < g_console_display_width)  // bug? include prompt?
  {
    g_console_input_ptr[g_console_input_chars] = ch;
    g_console_input_chars++;
    g_console_input_ptr[g_console_input_chars] = '\0';
    return true;
  }

  return false;
}

//===========================================================================
auto ConsoleUpdateCursor(char ch) -> void {
  if (ch) {
    g_console_cursor[0] = ch;
  } else {
    ch = g_console_input[g_console_input_chars + g_console_prompt_len];
    if (!ch) {
      ch = CHAR_SPACE;
    }
    g_console_cursor[0] = ch;
  }
}

//===========================================================================
auto ConsoleInputPeek() -> const char* {
  //	return g_console_display[0];
  //	return g_console_input_ptr;
  return g_console_input;
}

//===========================================================================
auto ConsoleInputReset() -> void {
  // Not using g_console_input since we get drawing of the input Line for "Free"
  // Even if we add console scrolling, we don't need any special logic to draw
  // the input line.
  g_console_input_quoted = false;

  ConsoleInputClear();

  //	strcpy( g_console_input, g_console_prompt_str ); // Assembler can change
  // prompt
  g_console_input[0] = g_console_prompt_str[0];
  g_console_prompt_len = 1;

  g_console_input_ptr = &g_console_input[g_console_prompt_len];
  g_console_input_chars = 0;
}

//===========================================================================
auto ConsoleInputTabCompletion() -> int { return UPDATE_CONSOLE_INPUT; }

//===========================================================================
auto ConsoleScrollHome() -> Update_t {
  g_console_display_start = g_console_display_total - CONSOLE_FIRST_LINE;
  if (g_console_display_start < 0) {
    g_console_display_start = 0;
  }

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleScrollEnd() -> Update_t {
  g_console_display_start = 0;

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleScrollUp(int nLines) -> Update_t {
  g_console_display_start += nLines;

  if (g_console_display_start >
      (g_console_display_total - CONSOLE_FIRST_LINE)) {
    g_console_display_start = (g_console_display_total - CONSOLE_FIRST_LINE);
  }

  if (g_console_display_start < 0) {
    g_console_display_start = 0;
  }

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleScrollDn(int nLines) -> Update_t {
  g_console_display_start -= nLines;
  if (g_console_display_start < 0) {
    g_console_display_start = 0;
  }

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleScrollPageUp() -> Update_t {
  ConsoleScrollUp(g_console_display_lines - CONSOLE_FIRST_LINE);

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleScrollPageDn() -> Update_t {
  ConsoleScrollDn(g_console_display_lines - CONSOLE_FIRST_LINE);

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleBufferTryUnpause(int nLines) -> Update_t {
  for (int y = 0; y < nLines; y++) {
    ConsoleBufferToDisplay();
  }

  g_console_buffer_paused = false;
  if (g_console_buffer_size) {
    g_console_buffer_paused = true;
    ConsoleDisplayPause();
    return UPDATE_CONSOLE_INPUT | UPDATE_CONSOLE_DISPLAY;
  }

  ConsoleInputReset();
  return UPDATE_CONSOLE_DISPLAY;
}

// Flush the console
//===========================================================================
auto ConsoleUpdate() -> Update_t {
  if (!g_console_buffer_paused) {
    int nLines = MIN(g_console_buffer_size, g_console_display_lines - 1);
    return ConsoleBufferTryUnpause(nLines);
  }

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleFlush() -> void {
  int nLines = g_console_buffer_size;
  ConsoleBufferTryUnpause(nLines);
}

auto DebuggerCursorUpdate() -> void {
  if (g_state.mode != MODE_DEBUG) {
    return;
  }

  const int nUpdatesPerSecond = 4;
  const uint32_t nUpdateInternal_ms = 1000 / nUpdatesPerSecond;
  static uint32_t nBeg = linapple_get_ticks();
  uint32_t nNow = linapple_get_ticks();

  if (((nNow - nBeg) < nUpdateInternal_ms) ||
      DebugVideoMode::Instance().IsSet()) {
    usleep(1000);  // Stop process hogging CPU
    return;
  }

  nBeg = nNow;
  DebuggerCursorNext();
  DrawConsoleCursor();
  stretch_blt_mem_to_frame_dc();
}

auto DebuggerCursorNext() -> void {
  g_input_cursor_visible ^= true;
  if (g_input_cursor_visible) {
    ConsoleUpdateCursor(g_input_cursor[g_input_cursor_index]);
  } else {
    ConsoleUpdateCursor(0);  // show char under cursor
  }
}

auto DebuggerUpdate() -> void { DebuggerCursorUpdate(); }

auto debugger_input_console_char(char ch) -> void {
  assert(g_state.mode == MODE_DEBUG);

  if (g_state.mode != MODE_DEBUG) {
    return;
  }

  if (g_console_buffer_paused) {
    return;
  }

  if (g_ignore_next_key) {
    g_ignore_next_key = false;
    return;
  }

  if ((ch < CHAR_SPACE) || (ch > 126)) {
    return;
  }

  if ((ch == CHAR_QUOTE_DOUBLE) || (ch == CHAR_QUOTE_SINGLE)) {
    g_console_input_quoted = !g_console_input_quoted;
  }

  if (!g_console_input_quoted) {
#if ALLOW_INPUT_LOWERCASE
#else
    ch = static_cast<char>(toupper(ch));
#endif
  }
  ConsoleInputChar(ch);

  DebuggerCursorNext();

  DrawConsoleInput();
  stretch_blt_mem_to_frame_dc();
}

auto ToggleFullScreenConsole() -> void {
  if (g_window_this != WINDOW_CONSOLE) {
    CmdWindowViewConsole(0);
    return;
  }
  CmdWindowLast(0);
}

extern auto CmdWindowViewConsole(int) -> Update_t;
extern auto CmdWindowLast(int) -> Update_t;
extern auto CmdGoNormalSpeed(int) -> Update_t;
extern auto CursorMoveUpAligned(int) -> void;
extern auto CursorMoveDownAligned(int) -> void;
extern auto WindowGetHeight(int) -> int;
extern auto CmdCursorPageUp(int) -> Update_t;
extern auto CmdCursorPageDown(int) -> Update_t;
extern auto CmdCursorPageUp256(int) -> Update_t;
extern auto CmdCursorPageDown256(int) -> Update_t;
extern auto CmdCursorPageUp4K(int) -> Update_t;
extern auto CmdCursorPageDown4K(int) -> Update_t;

auto debugger_process_key(int keycode) -> void {
  if (g_state.mode != MODE_DEBUG) {
    return;
  }

  if (DebugVideoMode::Instance().IsSet()) {
    if ((LINAPPLE_KEY_LSHIFT == keycode) || (LINAPPLE_KEY_RSHIFT == keycode) ||
        (LINAPPLE_KEY_LCTRL == keycode) || (LINAPPLE_KEY_RCTRL == keycode) ||
        (LINAPPLE_KEY_MENU == keycode)) {
      return;
    }

    // Normally any key press takes us out of "Viewing Apple Output" mode
    DebugVideoMode::Instance().Reset();
    UpdateDisplay(UPDATE_ALL);
    return;
  }

  Update_t bUpdateDisplay = UPDATE_NOTHING;

  // For long output, allow user to read it
  if (g_console_buffer_size != 0 &&
      ((LINAPPLE_KEY_SPACE == keycode) || (LINAPPLE_KEY_RETURN == keycode) ||
       (LINAPPLE_KEY_TAB == keycode) || (LINAPPLE_KEY_ESCAPE == keycode))) {
    int nLines = (LINAPPLE_KEY_ESCAPE == keycode)
                     ? g_console_buffer_size
                     : MIN(g_console_buffer_size, g_console_display_lines - 1);
    ConsoleBufferTryUnpause(nLines);
    keycode = 0;  // don't single-step
  }

  if (keycode == LINAPPLE_KEY_BACKSPACE) {
    if (g_console_input_chars) {
      ConsoleInputBackSpace();
      DebuggerCursorNext();
      DrawConsoleInput();
      stretch_blt_mem_to_frame_dc();
    }
  } else if ((keycode == LINAPPLE_KEY_RETURN) ||
             (keycode == LINAPPLE_KEY_KP_ENTER)) {
    if (g_console_input_chars) {
      bUpdateDisplay |=
          DebuggerProcessCommand(true);  // copy console input to console output
    } else {
      bUpdateDisplay |= CmdGoNormalSpeed(0);
    }
  } else if (keycode == LINAPPLE_KEY_ESCAPE) {
    if (g_console_input_chars) {
      ConsoleInputReset();
      bUpdateDisplay |= UPDATE_CONSOLE_INPUT;
    } else {
      // Exit Debugger
      debug_end();
      return;
    }
  } else if ((keycode >= ' ') && (keycode <= 127)) {
    debugger_input_console_char(keycode);
  } else {
    KeyboardModifiers_t mods = {};
    size_t mods_sz = sizeof(mods);
    peripheral_query(0, keyboard_query_mods, &mods, &mods_sz);

    switch (keycode) {
      case LINAPPLE_KEY_TAB: {
        if (g_console_input_chars) {
          bUpdateDisplay |= ConsoleInputTabCompletion();
        } else {
          ToggleFullScreenConsole();
          bUpdateDisplay |= UPDATE_ALL;
        }
        break;
      }

      case LINAPPLE_KEY_UP:
        bUpdateDisplay |= ConsoleInputHistoryPrev();
        break;
      case LINAPPLE_KEY_DOWN:
        bUpdateDisplay |= ConsoleInputHistoryNext();
        break;

      case LINAPPLE_KEY_PAGEUP:
        if (mods.ctrl) {
          bUpdateDisplay |= CmdCursorPageUp4K(0);
        } else if (mods.shift) {
          bUpdateDisplay |= CmdCursorPageUp256(0);
        } else {
          bUpdateDisplay |= CmdCursorPageUp(0);
        }
        break;

      case LINAPPLE_KEY_PAGEDOWN:
        if (mods.ctrl) {
          bUpdateDisplay |= CmdCursorPageDown4K(0);
        } else if (mods.shift) {
          bUpdateDisplay |= CmdCursorPageDown256(0);
        } else {
          bUpdateDisplay |= CmdCursorPageDown(0);
        }
        break;

      case LINAPPLE_KEY_F1:
      case LINAPPLE_KEY_F2:
      case LINAPPLE_KEY_F3:
      case LINAPPLE_KEY_F4:
      case LINAPPLE_KEY_F5:
      case LINAPPLE_KEY_F6:
      case LINAPPLE_KEY_F7:
      case LINAPPLE_KEY_F8:
      case LINAPPLE_KEY_F9:
      case LINAPPLE_KEY_F10:
      case LINAPPLE_KEY_F11:
      case LINAPPLE_KEY_F12:
        break;

      default:
        break;
    }
  }

  if (bUpdateDisplay) {
    UpdateDisplay(bUpdateDisplay);
  }
}

auto debugger_mouse_click(int /*x*/, int /*y*/) -> void {
  if (g_state.mode != MODE_DEBUG) {
    return;
  }

  KeyboardModifiers_t mods = {};
  size_t mods_sz = sizeof(mods);
  peripheral_query(0, keyboard_query_mods, &mods, &mods_sz);

  int iAltCtrlShift = 0;
  iAltCtrlShift |= mods.alt ? 1 << 0 : 0;
  iAltCtrlShift |= mods.ctrl ? 1 << 1 : 0;
  iAltCtrlShift |= mods.shift ? 1 << 2 : 0;

  // GH#462 disasm click #
  if (iAltCtrlShift != g_config_disasm_click) {
    return;
  }

  // TODO: WindowMouseClick( x, y );
}

auto ConsoleInputHistoryPrev() -> Update_t {
  if (g_history_lines_total) {
    // TODO: Implement history browsing
  }
  return UPDATE_NOTHING;
}

auto ConsoleInputHistoryNext() -> Update_t {
  if (g_history_lines_total) {
    // TODO: Implement history browsing
  }
  return UPDATE_NOTHING;
}
