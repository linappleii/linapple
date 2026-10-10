// SPDX-License-Identifier: GPL-2.0-only
#include "Debug.h"

#include <cstdint>

#include "Debugger_Assembler.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Display.h"
#include "Debugger_Types.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"

auto debug_display(bool bInitDisasm) -> void {
  if (bInitDisasm) {
    InitDisasm();
  }

  if (DebugVideoMode::Instance().IsSet()) {
    uint32_t mode = 0;
    DebugVideoMode::Instance().Get(&mode);
    video_refresh_screen(mode, true);
    return;
  }

  UpdateDisplay(UPDATE_ALL);
}

auto debug_initialize() -> void {
  static bool bInitialized = false;
  if (bInitialized) {
    return;
  }

  AssemblerStartup();
  InitDisasm();

  bInitialized = true;
}

auto is_debug_stepping_at_full_speed() -> bool {
  return (system_state.mode == app_mode_stepping) && debug_full_speed;
}

bool debugger_eat_key = false;

uint16_t disasm_top_address = 0;
uint16_t disasm_bot_address = 0;
uint16_t disasm_cur_address = 0;

bool disasm_cur_bad = false;
int disasm_cur_line = 0;  // Aligned to Top or Center
int disasm_cur_state = CURSOR_NORMAL;

int disasm_win_height = 0;

int font_spacing = FONT_SPACING_CLEAN;
int font_height = CONSOLE_FONT_HEIGHT;

int watches_count = 0;
Watches watches[MAX_WATCHES] = {};

int window_last = WINDOW_CODE;
int window_this = WINDOW_CODE;
WindowConfig window_config[NUM_WINDOWS] = {};

int zero_page_pointers_count = 0;
ZeroPagePointers zero_page_pointers[MAX_ZEROPAGE_POINTERS] = {};

auto GetBreakpointInfo(uint16_t nOffset, bool& bBreakpointActive_,
                       bool& bBreakpointEnable_) -> bool {
  bBreakpointActive_ = false;
  bBreakpointEnable_ = false;
  for (int i = 0; i < breakpoints_count; i++) {
    if (breakpoints[i].bSet && breakpoints[i].address == nOffset) {
      bBreakpointActive_ = true;
      bBreakpointEnable_ = breakpoints[i].bEnabled;
      return true;
    }
  }
  return false;
}

constexpr int DEBUGGER_VERSION = make_version(2, 9, 0, 15);

constexpr int WINDOW_DATA_BYTES_PER_LINE = 8;

DebugVideoMode DebugVideoMode::instance_;
