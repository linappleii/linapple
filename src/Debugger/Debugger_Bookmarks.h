// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

// Globals
extern int g_bookmarks_count;
extern Bookmark_t g_bookmarks[MAX_BOOKMARKS];

// Bookmark_t Functions
auto Bookmark_Add(int iBookmark, uint16_t address) -> bool;
auto Bookmark_Del(uint16_t address) -> bool;
auto Bookmark_Find(uint16_t address) -> bool;
auto Bookmark_Get(int iBookmark, uint16_t& address) -> bool;
auto Bookmark_Reset() -> void;
auto Bookmark_Size() -> int;

auto CmdBookmark(int nArgs) -> Update_t;
auto CmdBookmarkAdd(int nArgs) -> Update_t;
auto CmdBookmarkClear(int nArgs) -> Update_t;
auto CmdBookmarkGoto(int nArgs) -> Update_t;
auto CmdBookmarkList(int nArgs) -> Update_t;
auto CmdBookmarkLoad(int nArgs) -> Update_t;
auto CmdBookmarkSave(int nArgs) -> Update_t;
