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
int bookmarks_count = 0;
Bookmark bookmarks[MAX_BOOKMARKS] = {};

#include "Debugger_Cmd_Config.h"

// Bookmark Functions
auto Bookmark_Add(const int iBookmark, const uint16_t address) -> bool {
  if (iBookmark < MAX_BOOKMARKS) {
    bookmarks[iBookmark].address = address;
    bookmarks[iBookmark].bSet = true;
    bookmarks_count++;
    return true;
  }

  return false;
}

auto Bookmark_Del(const uint16_t address) -> bool {
  bool bDeleted = false;
  for (auto& bookmark : bookmarks) {
    if (bookmark.address == address) {
      bookmark.bSet = false;
      bDeleted = true;
    }
  }
  return bDeleted;
}

auto Bookmark_Find(const uint16_t address) -> bool {
  // Ugh, linear search
  int iBookmark = 0;
  for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++) {
    if ((bookmarks[iBookmark].address == address) &&
        bookmarks[iBookmark].bSet) {
      return true;
    }
  }
  return false;
}

auto Bookmark_Get(const int iBookmark, uint16_t& address) -> bool {
  if (iBookmark >= MAX_BOOKMARKS) {
    return false;
  }

  if (bookmarks[iBookmark].bSet) {
    address = bookmarks[iBookmark].address;
    return true;
  }

  return false;
}

auto Bookmark_Reset() -> void {
  int iBookmark = 0;
  for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++) {
    bookmarks[iBookmark].bSet = false;
  }
}

auto Bookmark_Size() -> int {
  bookmarks_count = 0;

  int iBookmark = 0;
  for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++) {
    if (bookmarks[iBookmark].bSet) {
      bookmarks_count++;
    }
  }

  return bookmarks_count;
}

auto CmdBookmark(int nArgs) -> UpdateResult { return CmdBookmarkAdd(nArgs); }

auto CmdBookmarkAdd(int nArgs) -> UpdateResult {
  // BMA [address]
  // BMA # address
  if (nArgs == 0) {
    return CmdZeroPageList(0);
  }

  int iArg = 1;
  int iBookmark = NO_6502_TARGET;

  if (nArgs > 1) {
    iBookmark = args[1].nValue;
    iArg++;
  }

  bool bAdded = false;
  for (; iArg <= nArgs; iArg++) {
    uint16_t address = args[iArg].nValue;

    if (iBookmark == NO_6502_TARGET) {
      iBookmark = 0;
      while ((iBookmark < MAX_BOOKMARKS) && bookmarks[iBookmark].bSet) {
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

    if ((iBookmark < MAX_BOOKMARKS) && (bookmarks_count < MAX_BOOKMARKS)) {
      bookmarks[iBookmark].bSet = true;
      bookmarks[iBookmark].address = address;
      bAdded = true;
      bookmarks_count++;
      iBookmark++;
    }
  }

  if (!bAdded) {
    return Help_Arg_1(CMD_BOOKMARK_ADD);
  }

  return UPDATE_DISASM | ConsoleUpdate();
}

auto CmdBookmarkClear(int nArgs) -> UpdateResult {
  int iBookmark = 0;

  int iArg = 0;
  for (iArg = 1; iArg <= nArgs; iArg++) {
    if (strcmp(args[nArgs].sArg, parameters[PARAM_WILDSTAR].name) == 0) {
      for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++) {
        if (bookmarks[iBookmark].bSet) {
          bookmarks[iBookmark].bSet = false;
        }
      }
      break;
    }

    iBookmark = args[iArg].nValue;
    if (bookmarks[iBookmark].bSet) {
      bookmarks[iBookmark].bSet = false;
    }
  }

  return UPDATE_DISASM;
}

auto CmdBookmarkGoto(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    return Help_Arg_1(CMD_BOOKMARK_GOTO);
  }

  int iBookmark = args[1].nValue;

  uint16_t address = 0;
  if (Bookmark_Get(iBookmark, address)) {
    disasm_cur_address = address;
    disasm_cur_line = 0;
    DisasmCalcTopBotAddress();
  }

  return UPDATE_DISASM;
}

auto CmdBookmarkList(int nArgs) -> UpdateResult {
  (void)nArgs;
  if (bookmarks_count == 0) {
    char sText[CONSOLE_WIDTH];
    ConsoleBufferPushFormat(
        sText, "  There are no current bookmarks.  (Max: %d", MAX_BOOKMARKS);
  } else {
    bwz_ListAll(bookmarks, MAX_BOOKMARKS);
  }
  return ConsoleUpdate();
}

auto CmdBookmarkLoad(int nArgs) -> UpdateResult {
  if (nArgs == 1) {
    //    strcpy( sMiniFileName, pFileName );
    //  strcat( sMiniFileName, ".aws" ); // HACK: MAGIC STRING

    //    strcpy(sFileName, system_state.current_dir); //
    //    strcat(sFileName, sMiniFileName);
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdBookmarkSave(int nArgs) -> UpdateResult {
  char sText[CONSOLE_WIDTH];

  config_state.Reset();

  ConfigSave_PrepareHeader(PARAM_CAT_BOOKMARKS, CMD_BOOKMARK_CLEAR);

  int iBookmark = 0;
  while (iBookmark < MAX_BOOKMARKS) {
    if (bookmarks[iBookmark].bSet) {
      snprintf(sText, sizeof(sText), "%s %x %04X\n",
               commands[CMD_BOOKMARK_ADD].name, iBookmark,
               bookmarks[iBookmark].address);
      config_state.PushLine(sText);
    }
    iBookmark++;
  }

  if (nArgs != 0) {
    if ((args[1].bType & TYPE_QUOTED_2) == 0) {
      return Help_Arg_1(CMD_BOOKMARK_SAVE);
    }

    if (ConfigSave_BufferToDisk(args[1].sArg, CONFIG_SAVE_FILE_CREATE)) {
      ConsoleBufferPush("Saved.");
      return ConsoleUpdate();
    }
  }

  return UPDATE_CONSOLE_DISPLAY;
}
