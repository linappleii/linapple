#pragma once

#include "Debugger_Types.h"

// Globals
extern int g_bookmarks_count;
extern Bookmark_t g_bookmarks[MAX_BOOKMARKS];

// Bookmark_t Functions
bool _Bookmark_Add(const int iBookmark, const uint16_t address);
bool _Bookmark_Del(const uint16_t address);
bool Bookmark_Find(const uint16_t address);
bool _Bookmark_Get(const int iBookmark, uint16_t& address);
void _Bookmark_Reset();
int _Bookmark_Size();

Update_t CmdBookmark(int nArgs);
Update_t CmdBookmarkAdd(int nArgs);
Update_t CmdBookmarkClear(int nArgs);
Update_t CmdBookmarkGoto(int nArgs);
Update_t CmdBookmarkList(int nArgs);
Update_t CmdBookmarkLoad(int nArgs);
Update_t CmdBookmarkSave(int nArgs);
