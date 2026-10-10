// SPDX-License-Identifier: GPL-2.0-only
#include "Debugger_Console.h"

#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Debug.h"
#include "Debugger_Cmd_CPU.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Display.h"
#include "Debugger_Types.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Util_Text.h"

// Globals originally from Debug.cpp
const char input_cursor[] = "_\x7F";  // insert over-write
bool input_cursor_visible = false;
int input_cursor_index = CURSOR_OVERSTRIKE;  // which cursor to use
const int input_cursor_count = sizeof(input_cursor);

bool ignore_next_key = false;

static auto ConsoleInputHistoryPrev() -> UpdateResult;
static auto ConsoleInputHistoryNext() -> UpdateResult;

// Console
// ________________________________________________________________________________________

// See ConsoleInputReset() for why the console input
// is tied to the zero'th output of console_display
// and not using a seperate var: console_input[ CONSOLE_WIDTH ];
//
//          :          console_buffer[4] |      ^ console_display[5] : :
//          console_buffer[3] |      | console_display[4]  <-
//          console_display_total
// console_buffer_size -> console_buffer[2] |      | console_display[3] :
//          :          console_buffer[1] v      | console_display[2] : .
//          console_buffer[0] -----> | console_display[1]        .
//                                                |
// buffered_input[0] -----> ConsoleInput ---->  | console_display[0]
// buffered_input[1] ^
// buffered_input[2] |
// buffered_input[3] |

// Buffer
bool console_buffer_paused =
    false;  // buffered output is waiting for user to continue
int console_buffer_size = 0;
ConChar console_buffer[CONSOLE_BUFFER_HEIGHT]
                          [CONSOLE_WIDTH];  // TODO: std::vector< Line >

// Cursor
char console_cursor[] = "_";

// Display
char console_prompt[] = ">!";     // input, assembler // NUM_PROMPTS
char console_prompt_str[] = ">";  // No, NOT Integer Basic!  The nostalgic '*'
                                    // "Monitor" doesn't look as good, IMHO. :-(
int console_prompt_len = 1;

bool console_full_width = true;  // false

int console_display_start = 0;  // to allow scrolling
int console_display_total = 0;  // number of lines added to console
int console_display_lines = 0;
int console_display_width = 0;
ConChar console_display[CONSOLE_DISPLAY_HEIGHT][CONSOLE_WIDTH];

// Input History
int history_lines_start = 0;
int history_lines_total = 0;  // number of commands entered
char history_lines[HISTORY_HEIGHT][HISTORY_WIDTH] = {""};

// Input Line

// Raw input Line (has prompt)
char console_input[CONSOLE_WIDTH + 16];  // = console_display[0];

// Cooked input line (no prompt)
int console_input_chars = 0;
char* console_input_ptr = nullptr;        // points to past prompt
const char* console_first_arg = nullptr;  // points to first arg
bool console_input_quoted = false;        // Allows lower-case to be entered
int console_input_skip = 0;

int console_color[NUM_CONSOLE_COLORS] = {
    WHITE, RED,   GREEN,  YELLOW,     BLUE,       MAGENTA,
    CYAN,  WHITE, ORANGE, LIGHT_GRAY, LIGHT_BLUE,
};
// Prototypes _______________________________________________________________

// Console
// ________________________________________________________________________________________

//===========================================================================
auto ConsoleBufferPeek() -> const ConChar* { return console_buffer[0]; }

//===========================================================================
auto console_print(const char* text) -> bool {
  while (console_buffer_size >= CONSOLE_BUFFER_HEIGHT) {
    ConsoleBufferToDisplay();
  }

  // Convert color string to native console color text
  // Ignores console_display_width
  char c = 0;

  int x = 0;
  const char* src_ptr = text;
  ConChar* pDst = &console_buffer[console_buffer_size][0];

  ConChar g = 0;
  bool bHaveColor = false;
  char cColor = 0;

  while ((x < CONSOLE_WIDTH) && ((c = *src_ptr) != 0)) {
    if ((c == '\n') || (x >= (CONSOLE_WIDTH - 1))) {
      *pDst = 0;
      x = 0;
      if (console_buffer_size >= CONSOLE_BUFFER_HEIGHT) {
        ConsoleBufferToDisplay();
      } else {
        console_buffer_size++;
      }
      pDst = &console_buffer[console_buffer_size][0];
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

    if (src_ptr[1] == 0) {
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
  console_buffer_size++;

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
  while (console_buffer_size >= CONSOLE_BUFFER_HEIGHT) {
    ConsoleBufferToDisplay();
  }

  ConChar c = 0;

  int x = 0;
  const char* src_ptr = text;
  ConChar* pDst = &console_buffer[console_buffer_size][0];

  while ((x < CONSOLE_WIDTH) && ((*src_ptr) != 0)) {
    c = static_cast<unsigned char>(*src_ptr);
    if ((c == '\n') || (x == (CONSOLE_WIDTH - 1))) {
      *pDst = 0;
      x = 0;
      if (console_buffer_size >= CONSOLE_BUFFER_HEIGHT) {
        ConsoleBufferToDisplay();
      } else {
        console_buffer_size++;
      }
      src_ptr++;
      pDst = &console_buffer[console_buffer_size][0];
      continue;
    }

    *pDst = (c & CONSOLE_COLOR_MASK);
    x++;
    src_ptr++;
    pDst++;
  }
  *pDst = 0;
  console_buffer_size++;

  return true;
}

// Shifts the buffered console output "down"
//===========================================================================
auto ConsoleBufferPop() -> void {
  int y = 0;
  while (y < console_buffer_size) {
    memcpy(console_buffer[y], console_buffer[y + 1],
           sizeof(ConChar) * CONSOLE_WIDTH);
    y++;
  }

  console_buffer_size--;
  console_buffer_size = std::max(console_buffer_size, 0);
}

// Remove string from buffered output
//===========================================================================
auto ConsoleBufferToDisplay() -> void {
  ConsoleDisplayPush(ConsoleBufferPeek());
  ConsoleBufferPop();
}

// No mark-up. Straight ASCII conversion
//===========================================================================
auto ConsoleConvertFromText(ConChar* sText, const char* text) -> void {
  const char* src_ptr = text;
  ConChar* pDst = sText;
  while (src_ptr && ((*src_ptr) != 0)) {
    *pDst = static_cast<ConChar>(*src_ptr & CONSOLE_COLOR_MASK);
    src_ptr++;
    pDst++;
  }
  *pDst = 0;
}

//===========================================================================
auto console_display_error(const char* text) -> UpdateResult {
  ConsoleBufferPush(text);
  return ConsoleUpdate();
}

//===========================================================================
auto ConsoleDisplayPush(const char* text) -> void {
  ConChar sText[CONSOLE_WIDTH * 2];
  ConsoleConvertFromText(sText, text);
  ConsoleDisplayPush(sText);
}

// Shifts the console display lines "up"
//===========================================================================
auto ConsoleDisplayPush(const ConChar* text) -> void {
  int nLen = std::min(console_display_total,
                      CONSOLE_DISPLAY_HEIGHT - 1 - CONSOLE_FIRST_LINE);
  while ((nLen--) != 0) {
    memcpy(
        reinterpret_cast<char*>(
            console_display[(nLen + 1 + CONSOLE_FIRST_LINE)]),
        reinterpret_cast<char*>(console_display[nLen + CONSOLE_FIRST_LINE]),
        sizeof(ConChar) * CONSOLE_WIDTH);
  }

  if (text) {
    memcpy(reinterpret_cast<char*>(console_display[CONSOLE_FIRST_LINE]), text,
           sizeof(ConChar) * CONSOLE_WIDTH);
  }

  console_display_total++;
  console_display_total = std::min(
      console_display_total, CONSOLE_DISPLAY_HEIGHT - CONSOLE_FIRST_LINE);
}

//===========================================================================
auto ConsoleDisplayPause() -> void {
  if (console_buffer_size != 0) {
    util_safe_strcpy(console_input, "...press SPACE continue, ESC skip...",
                     sizeof(console_input));
    console_prompt_len = static_cast<int>(strlen(console_input));
    console_input_ptr = &console_input[console_prompt_len];
    console_input_chars = 0;
    console_buffer_paused = true;
  } else {
    ConsoleInputReset();
  }
}

//===========================================================================
auto ConsoleInputBackSpace() -> bool {
  if (console_input_chars != 0) {
    console_input_ptr[console_input_chars] = ' ';

    console_input_chars--;

    if ((console_input_ptr[console_input_chars] == '"') ||
        (console_input_ptr[console_input_chars] == '\'')) {
      console_input_quoted = !console_input_quoted;
    }

    console_input_ptr[console_input_chars] = ' ';
    return true;
  }
  return false;
}

// Clears prompt too
//===========================================================================
auto ConsoleInputClear() -> bool {
  memset(console_input, 0, sizeof(console_input));

  if (console_input_chars != 0) {
    console_input_chars = 0;
    return true;
  }
  return false;
}

//===========================================================================
auto ConsoleInputChar(const char ch) -> bool {
  if (console_input_chars < console_display_width)  // bug? include prompt?
  {
    console_input_ptr[console_input_chars] = ch;
    console_input_chars++;
    console_input_ptr[console_input_chars] = '\0';
    return true;
  }

  return false;
}

//===========================================================================
auto ConsoleUpdateCursor(char ch) -> void {
  if (ch != 0) {
    console_cursor[0] = ch;
  } else {
    ch = console_input[console_input_chars + console_prompt_len];
    if (ch == 0) {
      ch = ' ';
    }
    console_cursor[0] = ch;
  }
}

//===========================================================================
auto ConsoleInputPeek() -> const char* {
  //	return console_display[0];
  //	return console_input_ptr;
  return console_input;
}

//===========================================================================
auto ConsoleInputReset() -> void {
  // Not using console_input since we get drawing of the input Line for "Free"
  // Even if we add console scrolling, we don't need any special logic to draw
  // the input line.
  console_input_quoted = false;

  ConsoleInputClear();

  //	strcpy( console_input, console_prompt_str ); // Assembler can change
  // prompt
  console_input[0] = console_prompt_str[0];
  console_prompt_len = 1;

  console_input_ptr = &console_input[console_prompt_len];
  console_input_chars = 0;
}

//===========================================================================
auto ConsoleInputTabCompletion() -> int { return UPDATE_CONSOLE_INPUT; }

//===========================================================================
auto ConsoleScrollHome() -> UpdateResult {
  console_display_start = console_display_total - CONSOLE_FIRST_LINE;
  console_display_start = std::max(console_display_start, 0);

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleScrollEnd() -> UpdateResult {
  console_display_start = 0;

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleScrollUp(int nLines) -> UpdateResult {
  console_display_start += nLines;

  console_display_start = std::min(
      console_display_start, console_display_total - CONSOLE_FIRST_LINE);

  console_display_start = std::max(console_display_start, 0);

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleScrollDn(int nLines) -> UpdateResult {
  console_display_start -= nLines;
  console_display_start = std::max(console_display_start, 0);

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleScrollPageUp() -> UpdateResult {
  ConsoleScrollUp(console_display_lines - CONSOLE_FIRST_LINE);

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleScrollPageDn() -> UpdateResult {
  ConsoleScrollDn(console_display_lines - CONSOLE_FIRST_LINE);

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleBufferTryUnpause(int nLines) -> UpdateResult {
  for (int y = 0; y < nLines; y++) {
    ConsoleBufferToDisplay();
  }

  console_buffer_paused = false;
  if (console_buffer_size != 0) {
    console_buffer_paused = true;
    ConsoleDisplayPause();
    return UPDATE_CONSOLE_INPUT | UPDATE_CONSOLE_DISPLAY;
  }

  ConsoleInputReset();
  return UPDATE_CONSOLE_DISPLAY;
}

// Flush the console
//===========================================================================
auto ConsoleUpdate() -> UpdateResult {
  if (!console_buffer_paused) {
    int nLines = std::min(console_buffer_size, console_display_lines - 1);
    return ConsoleBufferTryUnpause(nLines);
  }

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto ConsoleFlush() -> void {
  int nLines = console_buffer_size;
  ConsoleBufferTryUnpause(nLines);
}

auto DebuggerCursorUpdate() -> void {
  if (system_state.mode != app_mode_debug) {
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
  input_cursor_visible ^= 1;
  if (input_cursor_visible) {
    ConsoleUpdateCursor(input_cursor[input_cursor_index]);
  } else {
    ConsoleUpdateCursor(0);  // show char under cursor
  }
}

auto DebuggerUpdate() -> void { DebuggerCursorUpdate(); }

auto debugger_input_console_char(char ch) -> void {
  assert(system_state.mode == app_mode_debug);

  if (system_state.mode != app_mode_debug) {
    return;
  }

  if (console_buffer_paused) {
    return;
  }

  if (ignore_next_key) {
    ignore_next_key = false;
    return;
  }

  if ((ch < ' ') || (ch > 126)) {
    return;
  }

  if ((ch == '"') || (ch == '\'')) {
    console_input_quoted = !console_input_quoted;
  }

  if (!console_input_quoted) {
    ch = static_cast<char>(toupper(ch));
  }
  ConsoleInputChar(ch);

  DebuggerCursorNext();

  DrawConsoleInput();
  stretch_blt_mem_to_frame_dc();
}

auto ToggleFullScreenConsole() -> void {
  if (window_this != WINDOW_CONSOLE) {
    CmdWindowViewConsole(0);
    return;
  }
  CmdWindowLast(0);
}

auto debugger_process_key(int keycode) -> void {
  if (system_state.mode != app_mode_debug) {
    return;
  }

  if (DebugVideoMode::Instance().IsSet()) {
    if ((linapple_key_lshift == keycode) || (linapple_key_rshift == keycode) ||
        (linapple_key_lctrl == keycode) || (linapple_key_rctrl == keycode) ||
        (linapple_key_menu == keycode)) {
      return;
    }

    // Normally any key press takes us out of "Viewing Apple Output" mode
    DebugVideoMode::Instance().Reset();
    UpdateDisplay(UPDATE_ALL);
    return;
  }

  UpdateResult bUpdateDisplay = UPDATE_NOTHING;

  // For long output, allow user to read it
  if (console_buffer_size != 0 &&
      ((linapple_key_space == keycode) || (linapple_key_return == keycode) ||
       (linapple_key_tab == keycode) || (linapple_key_escape == keycode))) {
    int nLines =
        (linapple_key_escape == keycode)
            ? console_buffer_size
            : std::min(console_buffer_size, console_display_lines - 1);
    ConsoleBufferTryUnpause(nLines);
    keycode = 0;  // don't single-step
  }

  if (keycode == linapple_key_backspace) {
    if (console_input_chars != 0) {
      ConsoleInputBackSpace();
      DebuggerCursorNext();
      DrawConsoleInput();
      stretch_blt_mem_to_frame_dc();
    }
  } else if ((keycode == linapple_key_return) ||
             (keycode == linapple_key_kp_enter)) {
    if (console_input_chars != 0) {
      bUpdateDisplay |=
          DebuggerProcessCommand(true);  // copy console input to console output
    } else {
      bUpdateDisplay |= CmdGoNormalSpeed(0);
    }
  } else if (keycode == linapple_key_escape) {
    if (console_input_chars != 0) {
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
    bool shift = false;
    bool ctrl = false;
    linapple_get_modifiers(&shift, &ctrl, nullptr, nullptr);

    switch (keycode) {
      case linapple_key_tab: {
        if (console_input_chars != 0) {
          bUpdateDisplay |= ConsoleInputTabCompletion();
        } else {
          ToggleFullScreenConsole();
          bUpdateDisplay |= UPDATE_ALL;
        }
        break;
      }

      case linapple_key_up:
        bUpdateDisplay |= ConsoleInputHistoryPrev();
        break;
      case linapple_key_down:
        bUpdateDisplay |= ConsoleInputHistoryNext();
        break;

      case linapple_key_pageup:
        if (ctrl) {
          bUpdateDisplay |= CmdCursorPageUp4K(0);
        } else if (shift) {
          bUpdateDisplay |= CmdCursorPageUp256(0);
        } else {
          bUpdateDisplay |= CmdCursorPageUp(0);
        }
        break;

      case linapple_key_pagedown:
        if (ctrl) {
          bUpdateDisplay |= CmdCursorPageDown4K(0);
        } else if (shift) {
          bUpdateDisplay |= CmdCursorPageDown256(0);
        } else {
          bUpdateDisplay |= CmdCursorPageDown(0);
        }
        break;

      case linapple_key_f1:
      case linapple_key_f2:
      case linapple_key_f3:
      case linapple_key_f4:
      case linapple_key_f5:
      case linapple_key_f6:
      case linapple_key_f7:
      case linapple_key_f8:
      case linapple_key_f9:
      case linapple_key_f10:
      case linapple_key_f11:
      case linapple_key_f12:
      default:
        break;
    }
  }

  if (bUpdateDisplay != 0) {
    UpdateDisplay(bUpdateDisplay);
  }
}

auto debugger_mouse_click(int /*x*/, int /*y*/) -> void {
  if (system_state.mode != app_mode_debug) {
    return;
  }

  bool shift = false;
  bool ctrl = false;
  bool solid_apple = false;
  linapple_get_modifiers(&shift, &ctrl, nullptr, &solid_apple);

  int iAltCtrlShift = 0;
  iAltCtrlShift |= solid_apple ? 1 << 0 : 0;
  iAltCtrlShift |= ctrl ? 1 << 1 : 0;
  iAltCtrlShift |= shift ? 1 << 2 : 0;

  // GH#462 disasm click #
  if (iAltCtrlShift != config_disasm_click) {
    return;
  }

  // TODO: WindowMouseClick( x, y );
}

static auto ConsoleInputHistoryPrev() -> UpdateResult {
  if (history_lines_total != 0) {
    // TODO: Implement history browsing
  }
  return UPDATE_NOTHING;
}

static auto ConsoleInputHistoryNext() -> UpdateResult {
  if (history_lines_total != 0) {
    // TODO: Implement history browsing
  }
  return UPDATE_NOTHING;
}
