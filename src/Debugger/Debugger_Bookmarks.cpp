// SPDX-License-Identifier: GPL-2.0-only
#include "Debugger_Bookmarks.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Debug.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"

// Globals
int g_bookmarks_count = 0;
Bookmark_t g_bookmarks[MAX_BOOKMARKS] = {};

extern uint16_t g_disasm_cur_address;
extern int g_disasm_cur_line;
extern MemoryTextFile_t g_config_state;

auto ConfigSave_BufferToDisk(const char* pFileName, ConfigSave_t eConfigSave)
    -> bool;
auto ConfigSave_PrepareHeader(const Parameters_e eCategory,
                              const Commands_e eCommandClear) -> void;
auto DisasmCalcTopBotAddress() -> void;

// Bookmark_t Functions
auto Bookmark_Add(const int iBookmark, const uint16_t address) -> bool {
  if (iBookmark < MAX_BOOKMARKS) {
    g_bookmarks[iBookmark].address = address;
    g_bookmarks[iBookmark].bSet = true;
    g_bookmarks_count++;
    return true;
  }

  return false;
}

auto Bookmark_Del(const uint16_t address) -> bool {
  bool bDeleted = false;
  for (auto& g_bookmark : g_bookmarks) {
    if (g_bookmark.address == address) {
      g_bookmark.bSet = false;
      bDeleted = true;
    }
  }
  return bDeleted;
}

auto Bookmark_Find(const uint16_t address) -> bool {
  // Ugh, linear search
  int iBookmark = 0;
  for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++) {
    if (g_bookmarks[iBookmark].address == address) {
      if (g_bookmarks[iBookmark].bSet) {
        return true;
      }
    }
  }
  return false;
}

auto Bookmark_Get(const int iBookmark, uint16_t& address) -> bool {
  if (iBookmark >= MAX_BOOKMARKS) {
    return false;
  }

  if (g_bookmarks[iBookmark].bSet) {
    address = g_bookmarks[iBookmark].address;
    return true;
  }

  return false;
}

auto Bookmark_Reset() -> void {
  int iBookmark = 0;
  for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++) {
    g_bookmarks[iBookmark].bSet = false;
  }
}

auto Bookmark_Size() -> int {
  g_bookmarks_count = 0;

  int iBookmark = 0;
  for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++) {
    if (g_bookmarks[iBookmark].bSet) {
      g_bookmarks_count++;
    }
  }

  return g_bookmarks_count;
}

auto CmdBookmark(int nArgs) -> Update_t { return CmdBookmarkAdd(nArgs); }

auto CmdBookmarkAdd(int nArgs) -> Update_t {
  // BMA [address]
  // BMA # address
  if (!nArgs) {
    return CmdZeroPageList(0);
  }

  int iArg = 1;
  int iBookmark = NO_6502_TARGET;

  if (nArgs > 1) {
    iBookmark = g_args[1].nValue;
    iArg++;
  }

  bool bAdded = false;
  for (; iArg <= nArgs; iArg++) {
    uint16_t address = g_args[iArg].nValue;

    if (iBookmark == NO_6502_TARGET) {
      iBookmark = 0;
      while ((iBookmark < MAX_BOOKMARKS) && (g_bookmarks[iBookmark].bSet)) {
        iBookmark++;
      }
    }

    if ((iBookmark >= MAX_BOOKMARKS) && !bAdded) {
      char sText[CONSOLE_WIDTH];
      snprintf(sText, sizeof(sText),
               "All bookmarks are currently in use.  (Max: %d)", MAX_BOOKMARKS);
      ConsoleDisplayPush(sText);
      return ConsoleUpdate();
    }

    if ((iBookmark < MAX_BOOKMARKS) && (g_bookmarks_count < MAX_BOOKMARKS)) {
      g_bookmarks[iBookmark].bSet = true;
      g_bookmarks[iBookmark].address = address;
      bAdded = true;
      g_bookmarks_count++;
      iBookmark++;
    }
  }

  if (!bAdded) {
    return Help_Arg_1(CMD_BOOKMARK_ADD);
  }

  return UPDATE_DISASM | ConsoleUpdate();
}

auto CmdBookmarkClear(int nArgs) -> Update_t {
  int iBookmark = 0;

  int iArg = 0;
  for (iArg = 1; iArg <= nArgs; iArg++) {
    if (!strcmp(g_args[nArgs].sArg, g_parameters[PARAM_WILDSTAR].name)) {
      for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++) {
        if (g_bookmarks[iBookmark].bSet) {
          g_bookmarks[iBookmark].bSet = false;
        }
      }
      break;
    }

    iBookmark = g_args[iArg].nValue;
    if (g_bookmarks[iBookmark].bSet) {
      g_bookmarks[iBookmark].bSet = false;
    }
  }

  return UPDATE_DISASM;
}

auto CmdBookmarkGoto(int nArgs) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(CMD_BOOKMARK_GOTO);
  }

  int iBookmark = g_args[1].nValue;

  uint16_t address = 0;
  if (Bookmark_Get(iBookmark, address)) {
    g_disasm_cur_address = address;
    g_disasm_cur_line = 0;
    DisasmCalcTopBotAddress();
  }

  return UPDATE_DISASM;
}

auto CmdBookmarkList(int nArgs) -> Update_t {
  (void)nArgs;
  if (!g_bookmarks_count) {
    char sText[CONSOLE_WIDTH];
    ConsoleBufferPushFormat(
        sText, "  There are no current bookmarks.  (Max: %d", MAX_BOOKMARKS);
  } else {
    bwz_ListAll(g_bookmarks, MAX_BOOKMARKS);
  }
  return ConsoleUpdate();
}

auto CmdBookmarkLoad(int nArgs) -> Update_t {
  if (nArgs == 1) {
    //    strcpy( sMiniFileName, pFileName );
    //  strcat( sMiniFileName, ".aws" ); // HACK: MAGIC STRING

    //    strcpy(sFileName, g_state.current_dir); //
    //    strcat(sFileName, sMiniFileName);
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdBookmarkSave(int nArgs) -> Update_t {
  char sText[CONSOLE_WIDTH];

  g_config_state.Reset();

  ConfigSave_PrepareHeader(PARAM_CAT_BOOKMARKS, CMD_BOOKMARK_CLEAR);

  int iBookmark = 0;
  while (iBookmark < MAX_BOOKMARKS) {
    if (g_bookmarks[iBookmark].bSet) {
      snprintf(sText, sizeof(sText), "%s %x %04X\n",
               g_commands[CMD_BOOKMARK_ADD].name, iBookmark,
               g_bookmarks[iBookmark].address);
      g_config_state.PushLine(sText);
    }
    iBookmark++;
  }

  if (nArgs) {
    if (!(g_args[1].bType & TYPE_QUOTED_2)) {
      return Help_Arg_1(CMD_BOOKMARK_SAVE);
    }

    if (ConfigSave_BufferToDisk(g_args[1].sArg, CONFIG_SAVE_FILE_CREATE)) {
      ConsoleBufferPush("Saved.");
      return ConsoleUpdate();
    }
  }

  return UPDATE_CONSOLE_DISPLAY;
}
