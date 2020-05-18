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

/* Description: Core sound related functionality
 *
 * Author: Tom Charlesworth
 */

/*  Adaption for Linux+SDL done by beom beotiger. Peace! LLL */

// Timers like functions for Windows and Posix

// for usleep()
#include <unistd.h>
#include "stdafx.h"
#include "Timer.h"

#ifndef _WIN32
//for Timers try to use POSIX compliant timers
#include <signal.h>
#include <sys/time.h>
#endif

// for Assertion
#include <assert.h>
// for usleep()
#include <unistd.h>

#ifndef _WIN32
static unsigned int g_dwUsecPeriod = 0;

bool SysClk_InitTimer() {
  return true;
}

void SysClk_UninitTimer() {
}


inline Uint32 uSecSinceStart() {
  struct timeval latest;
  static struct timeval start;
  static bool first = true;

  if (first) {
    gettimeofday(&start, NULL);
    first = false;
    return 0;
  }

  gettimeofday(&latest, NULL);
  if (latest.tv_sec == start.tv_sec) {
    return latest.tv_usec - start.tv_usec;
  }
  return (1000000000 - start.tv_usec) + latest.tv_usec + (latest.tv_sec - (start.tv_sec + 1)) * 1000000000;
}

inline void nsleep(unsigned long us) {
  struct timespec req = {0};
  time_t sec = (int) (us / 1000000);
  us = us - (sec * 1000000);
  req.tv_sec = sec;
  req.tv_nsec = us;
  while (nanosleep(&req, &req) == -1) {
    continue;
  }
}

void SysClk_WaitTimer() {
  static Uint32 old = 0;
  Uint32 current;
  Uint32 elapsed;

  // Loop until next period
  // if more than 500usec sleep to give up CPU
  while (1) {
    current = uSecSinceStart();
    elapsed = current - old;
    if (elapsed >= g_dwUsecPeriod) {
      old = current;
      return;
    }
    #if 1
    if ((g_dwUsecPeriod - elapsed) > 500) {
      nsleep(1);
    }
    #endif
  }
}

void SysClk_StartTimerUsec(unsigned int dwUsecPeriod) {
  g_dwUsecPeriod = dwUsecPeriod;
}

void SysClk_StopTimer() {
}

#else
// Timer Functions - WINDOWS specific                       //

static unsigned int g_dwAdviseToken;
static IReferenceClock *g_pRefClock = NULL;
static HANDLE g_hSemaphore = NULL;
static bool g_bRefClockTimerActive = false;
static unsigned int g_dwLastUsecPeriod = 0;

bool SysClk_InitTimer() {
  g_hSemaphore = CreateSemaphore(NULL, 0, 1, NULL);    // Max count = 1
  if (g_hSemaphore == NULL) {
    fprintf(stderr, "Error creating semaphore\n");
    return false;
  }

  if (CoCreateInstance(CLSID_SystemClock, NULL, CLSCTX_INPROC,
                       IID_IReferenceClock, (LPVOID*)&g_pRefClock) != S_OK) {
    fprintf(stderr, "Error initialising COM\n");
    return false;  // Fails for Win95!
  }

  return true;
}

void SysClk_UninitTimer() {
  SysClk_StopTimer();
  SAFE_RELEASE(g_pRefClock);
  if (CloseHandle(g_hSemaphore) == 0) {
    fprintf(stderr, "Error closing semaphore handle\n");
  }
}

void SysClk_WaitTimer() {
  if(!g_bRefClockTimerActive) {
    return;
  }
  WaitForSingleObject(g_hSemaphore, INFINITE);
}

void SysClk_StartTimerUsec(unsigned int dwUsecPeriod) {
  if(g_bRefClockTimerActive && (g_dwLastUsecPeriod == dwUsecPeriod)) {
    return;
  }

  SysClk_StopTimer();

  REFERENCE_TIME rtPeriod = (REFERENCE_TIME) (dwUsecPeriod * 10);  // In units of 100ns
  REFERENCE_TIME rtNow;
  HRESULT hr = g_pRefClock->GetTime(&rtNow);

  if ((hr != S_OK) && (hr != S_FALSE)) {
    fprintf(stderr, "Error creating timer (GetTime failed)\n");
    _ASSERT(0);
    return;
  }

  if (g_pRefClock->AdvisePeriodic(rtNow, rtPeriod, g_hSemaphore, &g_dwAdviseToken) != S_OK) {
    fprintf(stderr, "Error creating timer (AdvisePeriodic failed)\n");
    _ASSERT(0);
    return;
  }

  g_dwLastUsecPeriod = dwUsecPeriod;
  g_bRefClockTimerActive = true;
}

void SysClk_StopTimer() {
  if(!g_bRefClockTimerActive) {
    return;
  }

  if (g_pRefClock->Unadvise(g_dwAdviseToken) != S_OK) {
    fprintf(stderr, "Error deleting timer\n");
    _ASSERT(0);
    return;
  }

  g_bRefClockTimerActive = false;
}
#endif
