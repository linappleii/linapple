#pragma once

#include "Debugger_Types.h"

// Globals
extern int g_nBookmarks;
extern Bookmark_t g_aBookmarks[MAX_BOOKMARKS];

// Bookmark Functions
bool _Bookmark_Add(const int iBookmark, const unsigned short nAddress);
bool _Bookmark_Del(const unsigned short nAddress);
bool Bookmark_Find(const unsigned short nAddress);
bool _Bookmark_Get(const int iBookmark, unsigned short &nAddress);
void _Bookmark_Reset();
int _Bookmark_Size();

Update_t CmdBookmark(int nArgs);
Update_t CmdBookmarkAdd(int nArgs);
Update_t CmdBookmarkClear(int nArgs);
Update_t CmdBookmarkGoto(int nArgs);
Update_t CmdBookmarkList(int nArgs);
Update_t CmdBookmarkLoad(int nArgs);
Update_t CmdBookmarkSave(int nArgs);
