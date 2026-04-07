#include <cassert>
/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2014, Tom Charlesworth, Michael Pohoreski
Copyright (C) 2020, Thorsten Brehm

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
 * Author: Copyright (C) 2006-2010 Michael Pohoreski
 */


#include "stdafx.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Memory.h"
#include "Debugger_Cmd_CPU.h"

#include "Debugger_Help.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Assembler.h"
#include "Debugger_Display.h"
#include "Debugger_Symbols.h"
#include "Debugger_Range.h"
#include "Debugger_Color.h"
#include "Debugger_Cmd_Config.h"
#include "Debugger_Cmd_Benchmark.h"
#include "Debugger_Cmd_ZeroPage.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Cmd_Output.h"
#include "Debugger_Cmd_CPU.h"
#include "Debugger_Assembler.h"
#include "Debugger_Commands.h"
#include "Video.h"

// for usleep
#include <unistd.h>


#define ALLOW_INPUT_LOWERCASE 1

void DebugDisplay(bool bInitDisasm)
{
  if (bInitDisasm) {
    InitDisasm();
  }

  if (DebugVideoMode::Instance().IsSet())
  {
    uint32_t mode = 0;
    DebugVideoMode::Instance().Get(&mode);
    VideoRefreshScreen(mode, true);
    return;
  }

  UpdateDisplay( UPDATE_ALL );
}
bool IsDebugSteppingAtFullSpeed(void)
{
  return (g_state.mode == MODE_STEPPING) && g_bDebugFullSpeed;
}

DebugVideoMode DebugVideoMode::m_Instance;
