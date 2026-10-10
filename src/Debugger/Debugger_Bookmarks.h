// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

// Globals
extern int bookmarks_count;
extern Bookmark bookmarks[MAX_BOOKMARKS];

// Bookmark Functions
auto Bookmark_Add(int iBookmark, uint16_t address) -> bool;
auto Bookmark_Del(uint16_t address) -> bool;
auto Bookmark_Find(uint16_t address) -> bool;
auto Bookmark_Get(int iBookmark, uint16_t& address) -> bool;
auto Bookmark_Reset() -> void;
auto Bookmark_Size() -> int;

auto CmdBookmark(int nArgs) -> UpdateResult;
auto CmdBookmarkAdd(int nArgs) -> UpdateResult;
auto CmdBookmarkClear(int nArgs) -> UpdateResult;
auto CmdBookmarkGoto(int nArgs) -> UpdateResult;
auto CmdBookmarkList(int nArgs) -> UpdateResult;
auto CmdBookmarkLoad(int nArgs) -> UpdateResult;
auto CmdBookmarkSave(int nArgs) -> UpdateResult;
