/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

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

/* Description: Debugger
 *
 * Author: Copyright (C) 2006, Michael Pohoreski
 */


/* Needs adaptation for SDL and POSIX --bb */
/* There are just dummy wrappers for real functions can be found in AppleWin sources
  at http://applewin.berlios.de  */


// disable warning C4786: symbol greater than 255 character:
//#pragma warning(disable: 4786)

#include "stdafx.h"

// Full-Speed debugging
int g_nDebugOnBreakInvalid = 0;
int g_iDebugOnOpcode = 0;
bool g_bDebugDelayBreakCheck = false;

int g_nBreakpoints = 0;


DWORD extbench = 0;
bool g_bDebuggerViewingAppleOutput = false;


BOOL g_bProfiling = 0;
int g_nDebugSteps = 0;


// Still called from external file
void DebugDisplay(BOOL bDrawBackground)
{
  //  Update_t bUpdateFlags = UPDATE_ALL;

  //  if (! bDrawBackground)
  //    bUpdateFlags &= ~UPDATE_BACKGROUND;

  //  UpdateDisplay( bUpdateFlags );
}

//===========================================================================
void DebuggerMouseClick(int x, int y)
{
  if (g_nAppMode != MODE_DEBUG)
    return;
}

void DebugEnd()
{

}


//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
void DebuggerProcessKey(int keycode)
//void DebugProcessCommand (int keycode)
{
  if (g_nAppMode != MODE_DEBUG)
    return;

}

//===========================================================================
void DebuggerUpdate()
{
  //  DebuggerCursorUpdate();
}


void DebugContinueStepping()
{

}

//===========================================================================
void DebugDestroy()
{
  //  DebugEnd();
}


//===========================================================================
void DebugInitialize()
{
  //  AssemblerOff(); // update prompt
}

void DebuggerInputConsoleChar(TCHAR ch)
{
  if ((g_nAppMode == MODE_STEPPING) && (ch == DEBUG_EXIT_KEY)) {
    g_nDebugSteps = 0; // Exit Debugger
  }

  if (g_nAppMode != MODE_DEBUG)
    return;
}

