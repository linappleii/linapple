#include "core/Common.h"
#include "Debugger_Bookmarks.h"
#include "Debug.h"
#include "apple2/CPU.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Debugger_Symbols.h"
#include "Debugger_Breakpoints.h"
#include "core/Log.h"
#include <cassert>
#include <cstddef>

// Globals
int        g_nBookmarks = 0;
Bookmark_t g_aBookmarks[ MAX_BOOKMARKS ];

extern unsigned short g_nDisasmCurAddress;
extern int g_nDisasmCurLine;
extern MemoryTextFile_t g_ConfigState;

bool ConfigSave_BufferToDisk ( const char *pFileName, ConfigSave_t eConfigSave );
void ConfigSave_PrepareHeader ( const Parameters_e eCategory, const Commands_e eCommandClear );
void DisasmCalcTopBotAddress ();

// Bookmark Functions
bool _Bookmark_Add( const int iBookmark, const unsigned short nAddress )
{
  if (iBookmark < MAX_BOOKMARKS)
  {
    g_aBookmarks[ iBookmark ].nAddress = nAddress;
    g_aBookmarks[ iBookmark ].bSet     = true;
    g_nBookmarks++;
    return true;
  }

  return false;
}


bool _Bookmark_Del( const unsigned short nAddress )
{
  bool bDeleted = false;
  for (int iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++ )
  {
    if (g_aBookmarks[ iBookmark ].nAddress == nAddress)
    {
      g_aBookmarks[ iBookmark ].bSet = false;
      bDeleted = true;
    }
  }
  return bDeleted;
}

bool Bookmark_Find( const unsigned short nAddress )
{
  // Ugh, linear search
  int iBookmark;
  for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++ )
  {
    if (g_aBookmarks[ iBookmark ].nAddress == nAddress)
    {
      if (g_aBookmarks[ iBookmark ].bSet)
        return true;
    }
  }
  return false;
}


bool _Bookmark_Get( const int iBookmark, unsigned short & nAddress )
{
  if (iBookmark >= MAX_BOOKMARKS)
    return false;

  if (g_aBookmarks[ iBookmark ].bSet)
  {
    nAddress = g_aBookmarks[ iBookmark ].nAddress;
    return true;
  }

  return false;
}


void _Bookmark_Reset()
{
  int iBookmark = 0;
  for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++ )
  {
    g_aBookmarks[ iBookmark ].bSet = false;
  }
}


int _Bookmark_Size()
{
  g_nBookmarks = 0;

  int iBookmark;
  for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++ )
  {
    if (g_aBookmarks[ iBookmark ].bSet)
      g_nBookmarks++;
  }

  return g_nBookmarks;
}

Update_t CmdBookmark (int nArgs)
{
  return CmdBookmarkAdd( nArgs );
}

Update_t CmdBookmarkAdd (int nArgs )
{
  // BMA [address]
  // BMA # address
  if (! nArgs)
  {
    return CmdZeroPageList( 0 );
  }

  int iArg = 1;
  int iBookmark = NO_6502_TARGET;

  if (nArgs > 1)
  {
    iBookmark = g_aArgs[ 1 ].nValue;
    iArg++;
  }

  bool bAdded = false;
  for (; iArg <= nArgs; iArg++ )
  {
    unsigned short nAddress = g_aArgs[ iArg ].nValue;

    if (iBookmark == NO_6502_TARGET)
    {
      iBookmark = 0;
      while ((iBookmark < MAX_BOOKMARKS) && (g_aBookmarks[iBookmark].bSet))
      {
        iBookmark++;
      }
    }

    if ((iBookmark >= MAX_BOOKMARKS) && !bAdded)
    {
      char sText[ CONSOLE_WIDTH ];
      sprintf( sText, "All bookmarks are currently in use.  (Max: %d)", MAX_BOOKMARKS );
      ConsoleDisplayPush( sText );
      return ConsoleUpdate();
    }

    if ((iBookmark < MAX_BOOKMARKS) && (g_nBookmarks < MAX_BOOKMARKS))
    {
      g_aBookmarks[iBookmark].bSet = true;
      g_aBookmarks[iBookmark].nAddress = nAddress;
      bAdded = true;
      g_nBookmarks++;
      iBookmark++;
    }
  }

  if (!bAdded)
    goto _Help;

  return UPDATE_DISASM | ConsoleUpdate();

_Help:
  return Help_Arg_1( CMD_BOOKMARK_ADD );

}


Update_t CmdBookmarkClear (int nArgs)
{
  int iBookmark = 0;

  int iArg;
  for (iArg = 1; iArg <= nArgs; iArg++ )
  {
    if (! strcmp(g_aArgs[nArgs].sArg, g_aParameters[ PARAM_WILDSTAR ].m_sName))
    {
      for (iBookmark = 0; iBookmark < MAX_BOOKMARKS; iBookmark++ )
      {
        if (g_aBookmarks[ iBookmark ].bSet)
          g_aBookmarks[ iBookmark ].bSet = false;
      }
      break;
    }

    iBookmark = g_aArgs[ iArg ].nValue;
    if (g_aBookmarks[ iBookmark ].bSet)
      g_aBookmarks[ iBookmark ].bSet = false;
  }

  return UPDATE_DISASM;
}


Update_t CmdBookmarkGoto ( int nArgs )
{
  if (! nArgs)
    return Help_Arg_1( CMD_BOOKMARK_GOTO );

  int iBookmark = g_aArgs[ 1 ].nValue;

  unsigned short nAddress;
  if (_Bookmark_Get( iBookmark, nAddress ))
  {
    g_nDisasmCurAddress = nAddress;
    g_nDisasmCurLine = 0;
    DisasmCalcTopBotAddress();
  }

  return UPDATE_DISASM;
}


Update_t CmdBookmarkList (int nArgs)
{
  (void)nArgs;
  if (! g_nBookmarks)
  {
    char sText[ CONSOLE_WIDTH ];
    ConsoleBufferPushFormat( sText, "  There are no current bookmarks.  (Max: %d", MAX_BOOKMARKS );
  }
  else
  {
    _BWZ_ListAll( g_aBookmarks, MAX_BOOKMARKS );
  }
  return ConsoleUpdate();
}


Update_t CmdBookmarkLoad (int nArgs)
{
  if (nArgs == 1)
  {
//    strcpy( sMiniFileName, pFileName );
  //  strcat( sMiniFileName, ".aws" ); // HACK: MAGIC STRING

//    strcpy(sFileName, g_state.sCurrentDir); //
//    strcat(sFileName, sMiniFileName);
  }

  return UPDATE_CONSOLE_DISPLAY;
}


Update_t CmdBookmarkSave (int nArgs)
{
  char sText[ CONSOLE_WIDTH ];

  g_ConfigState.Reset();

  ConfigSave_PrepareHeader( PARAM_CAT_BOOKMARKS, CMD_BOOKMARK_CLEAR );

  int iBookmark = 0;
  while (iBookmark < MAX_BOOKMARKS)
  {
    if (g_aBookmarks[ iBookmark ].bSet)
    {
      sprintf( sText, "%s %x %04X\n"
        , g_aCommands[ CMD_BOOKMARK_ADD ].m_sName
        , iBookmark
        , g_aBookmarks[ iBookmark ].nAddress
      );
      g_ConfigState.PushLine( sText );
    }
    iBookmark++;
  }

  if (nArgs)
  {
    if (! (g_aArgs[ 1 ].bType & TYPE_QUOTED_2))
      return Help_Arg_1( CMD_BOOKMARK_SAVE );

    if (ConfigSave_BufferToDisk( g_aArgs[ 1 ].sArg, CONFIG_SAVE_FILE_CREATE ))
    {
      ConsoleBufferPush(  "Saved."  );
      return ConsoleUpdate();
    }
  }

  return UPDATE_CONSOLE_DISPLAY;
}
