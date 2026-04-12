/*
linapple : An Apple //e emulator for Linux

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

#include <cstring>
#include <cstdio>
#include <cstdarg>
#include "core/Log.h"
#include "core/Common_Globals.h"

static LogLevel g_verbosity = LogLevel::Info;

static void vLogOutput(LogLevel level, const char* format, va_list args) {
  if (level > g_verbosity) {
    return;
  }

  char output[512];
  vsnprintf(output, sizeof(output) - 1, format, args);

  if (g_fh) {
    fprintf(g_fh, "%s", output);
    fflush(g_fh);
  }

  // Terminal visibility
  if (level <= LogLevel::Error) {
    fprintf(stderr, "ERROR: %s", output);
    fflush(stderr);
  } else if (level == LogLevel::Perf) {
    printf("PERF: %s", output);
    fflush(stdout);
  } else if (level <= LogLevel::Info) {
    printf("%s", output);
    fflush(stdout);
  }
}

namespace Logger {
  void Initialize() {
    g_fh = fopen("AppleWin.log", "a+t");
    struct timeval tv{};
    struct tm *ptm = nullptr;
    char time_str[40];
    gettimeofday(&tv, nullptr);
    ptm = localtime(&tv.tv_sec);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", ptm);
    if (g_fh) {
      fprintf(g_fh, "*** Logging started: %s\n", time_str);
    }
  }

  void SetVerbosity(LogLevel level) {
    g_verbosity = level;
  }

  void Error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vLogOutput(LogLevel::Error, format, args);
    va_end(args);
  }

  void Warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vLogOutput(LogLevel::Warn, format, args);
    va_end(args);
  }

  void Info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vLogOutput(LogLevel::Info, format, args);
    va_end(args);
  }

  void Perf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vLogOutput(LogLevel::Perf, format, args);
    va_end(args);
  }

  void Debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vLogOutput(LogLevel::Debug, format, args);
    va_end(args);
  }

  void Destroy() {
    if (g_fh) {
      fprintf(g_fh, "*** Logging ended\n\n");
      fclose(g_fh);
      g_fh = NULL;
    }
  }
} // namespace Logger

