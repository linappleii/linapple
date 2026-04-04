/*
linapple : An Apple //e emulator for Linux

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

/* Description: Core sound related functionality
 *
 * Author: Tom Charlesworth
 */

#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <assert.h>
#include "stdafx.h"
#include "Timer.h"

static unsigned int g_dwUsecPeriod = 0;

bool SysClk_InitTimer() {
  return true;
}

void SysClk_UninitTimer() {
}

inline uint32_t uSecSinceStart() {
  struct timeval latest;
  static struct timeval start;
  static bool first = true;

  if (first) {
    gettimeofday(&start, NULL);
    first = false;
    return 0;
  }

  gettimeofday(&latest, NULL);
  long seconds = latest.tv_sec - start.tv_sec;
  long useconds = latest.tv_usec - start.tv_usec;

  return (uint32_t)(seconds * 1000000 + useconds);
}

inline void nsleep(unsigned long us) {
  struct timespec req = {};
  time_t sec = (time_t) (us / 1000000);
  long nsec = (long)((us % 1000000) * 1000);
  req.tv_sec = sec;
  req.tv_nsec = nsec;
  while (nanosleep(&req, &req) == -1) {
    continue;
  }
}

void SysClk_WaitTimer() {
  static uint32_t old = 0;
  uint32_t current;
  uint32_t elapsed;

  while (true) {
    current = uSecSinceStart();
    elapsed = current - old;
    if (elapsed >= g_dwUsecPeriod) {
      old = current;
      return;
    }
    
    // If we have more than 500us left, sleep for a bit instead of spinning
    uint32_t remaining = g_dwUsecPeriod - elapsed;
    if (remaining > 500) {
      nsleep(remaining / 2); // Sleep for half the remaining time
    }
  }
}

void SysClk_StartTimerUsec(unsigned int dwUsecPeriod) {
  g_dwUsecPeriod = dwUsecPeriod;
}

void SysClk_StopTimer() {
}
