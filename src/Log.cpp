/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski, Nick Westgate

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

/* Description: Log
 *
 * Author: Nick Westgate
 */

#include "stdafx.h"
//#pragma  hdrstop
#include <string.h>

//---------------------------------------------------------------------------

void LogInitialize()
{
  g_fh = fopen("AppleWin.log", "a+t");  // Open log file (append & text g_nAppMode)
  // Start of Unix(tm) specific code
  struct timeval tv;
  struct tm *ptm;
  char time_str[40];
  gettimeofday(&tv, NULL);
  ptm = localtime(&tv.tv_sec);
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", ptm);
  // end of Unix(tm) specific code
  fprintf(g_fh, "*** Logging started: %s\n", time_str);
}

void LogOutput(LPCTSTR format, ...)
{
  if (!g_fh)
    return;

  va_list args;
  va_start(args, format);

  TCHAR output[512];

  vsnprintf(output, sizeof(output) - 1, format, args);
  //    OutputDebugString(output);
  fprintf(g_fh, "%s", output);
}

void LogDestroy()
{
  if (g_fh) {
    fprintf(g_fh, "*** Logging ended\n\n");
    fclose(g_fh);
  }
}

//---------------------------------------------------------------------------
