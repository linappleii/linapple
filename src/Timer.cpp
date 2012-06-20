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

/*	Adaption for Linux+SDL done by beom beotiger. Peace! LLL */

// Timers like functions for Windows and Posix
#include "stdafx.h"
#include "Timer.h"

#ifndef _WIN32
//for Timers try to use POSIX compliant timers
#include <signal.h>
#include <sys/time.h>
#endif

// for Assertion
#include <assert.h>

#ifndef _WIN32
//===============================================================================//
//		Timer Functions - POSIX specific 				//
//=============================================================================//
// Vars
static bool g_bRefClockTimerActive = false;
static DWORD g_dwLastUsecPeriod = 0;

static bool g_bTimerToggle = false;
struct sigaction sa_SysClk;
struct itimerval mytimeset;

void SysClk_TickTimer(int signum)
{	// should occur every specified times per second
	g_bTimerToggle = true;	// just set the toggle flag, and leave peacefully? --bb
}

bool SysClk_InitTimer()
{// first initialization of the timer
/*	memset(&sa_SysClk, 0, sizeof(sa_SysClk));	// clear sigaction struct
	sa_SysClk.sa_handler = &SysClk_TickTimer;
	sigaction(SIGALRM, &sa_SysClk, NULL);	// set SIGALRM handler*/
	if(signal(SIGALRM, SysClk_TickTimer) == SIG_ERR)
		return false;

	printf("Timer has been initted!\n");
 	return true;
}

void SysClk_UninitTimer()
{
	SysClk_StopTimer();
//	signal(SIGALRM, NULL);
}

void SysClk_WaitTimer()
{
//	printf("Waiting timer...\n");
	
	if(!g_bRefClockTimerActive)
		return;
// pause() - better than that?

	while(!g_bTimerToggle)
		usleep(1);	// do nothing is something doing also? 0_0 --bb
	g_bTimerToggle = false;
	
//	printf("Timer has been ticked!\n");
}

void SysClk_StartTimerUsec(DWORD dwUsecPeriod)
{
	// starting timer during dwUsecPeriod in microseconds???
//	printf("Timer started %d usec\n", dwUsecPeriod);
	if(g_bRefClockTimerActive && (g_dwLastUsecPeriod == dwUsecPeriod))
		return;

	SysClk_StopTimer();

	// to comply with Windows DirectShow REFERENCE_TIME, which is in units of 100 nanoseconds
	mytimeset.it_interval.tv_sec = 0;
	mytimeset.it_interval.tv_usec =  dwUsecPeriod;// * 10  100;
	mytimeset.it_value.tv_sec = 0;
	mytimeset.it_value.tv_usec = dwUsecPeriod;// * 10 / 100; 

	if(setitimer(ITIMER_REAL, &mytimeset, NULL) != 0) {
		fprintf(stderr, "Error creating timer (setitimer failed)\n");
		_ASSERT(0);
		return;
	}

	g_dwLastUsecPeriod = dwUsecPeriod;
	g_bRefClockTimerActive = true;
}

void SysClk_StopTimer()
{
	if(!g_bRefClockTimerActive)
		return;

	// Zero values just disables timers
	mytimeset.it_interval.tv_sec = 0;
	mytimeset.it_interval.tv_usec = 0;
	mytimeset.it_value.tv_sec = 0;
	mytimeset.it_value.tv_usec = 0;

	setitimer(ITIMER_REAL, &mytimeset, NULL);

	g_bTimerToggle = true;
	g_bRefClockTimerActive = false;
}

#else
  //===============================================================================//
 //		Timer Functions - WINDOWS specific 										  //
//===============================================================================//

// Vars
static DWORD g_dwAdviseToken;
static IReferenceClock *g_pRefClock = NULL;
static HANDLE g_hSemaphore = NULL;
static bool g_bRefClockTimerActive = false;
static DWORD g_dwLastUsecPeriod = 0;


bool SysClk_InitTimer()
{
	g_hSemaphore = CreateSemaphore(NULL, 0, 1, NULL);		// Max count = 1
	if (g_hSemaphore == NULL)
	{
		fprintf(stderr, "Error creating semaphore\n");
		return false;
	}

	if (CoCreateInstance(CLSID_SystemClock, NULL, CLSCTX_INPROC,
                         IID_IReferenceClock, (LPVOID*)&g_pRefClock) != S_OK)
	{
		fprintf(stderr, "Error initialising COM\n");
		return false;	// Fails for Win95!
	}

	return true;
}

void SysClk_UninitTimer()
{
	SysClk_StopTimer();

	SAFE_RELEASE(g_pRefClock);

	if (CloseHandle(g_hSemaphore) == 0)
		fprintf(stderr, "Error closing semaphore handle\n");
}

//

void SysClk_WaitTimer()
{
	if(!g_bRefClockTimerActive)
		return;

	WaitForSingleObject(g_hSemaphore, INFINITE);
}

void SysClk_StartTimerUsec(DWORD dwUsecPeriod)
{
	if(g_bRefClockTimerActive && (g_dwLastUsecPeriod == dwUsecPeriod))
		return;

	SysClk_StopTimer();

	REFERENCE_TIME rtPeriod = (REFERENCE_TIME) (dwUsecPeriod * 10);	// In units of 100ns
	REFERENCE_TIME rtNow;

	HRESULT hr = g_pRefClock->GetTime(&rtNow);
	// S_FALSE : Returned time is the same as the previous value

	if ((hr != S_OK) && (hr != S_FALSE))
	{
		fprintf(stderr, "Error creating timer (GetTime failed)\n");
		_ASSERT(0);
		return;
	}

	if (g_pRefClock->AdvisePeriodic(rtNow, rtPeriod, g_hSemaphore, &g_dwAdviseToken) != S_OK)
	{
		fprintf(stderr, "Error creating timer (AdvisePeriodic failed)\n");
		_ASSERT(0);
		return;
	}

	g_dwLastUsecPeriod = dwUsecPeriod;
	g_bRefClockTimerActive = true;
}

void SysClk_StopTimer()
{
	if(!g_bRefClockTimerActive)
		return;

	if (g_pRefClock->Unadvise(g_dwAdviseToken) != S_OK)
	{
		fprintf(stderr, "Error deleting timer\n");
		_ASSERT(0);
		return;
	}

	g_bRefClockTimerActive = false;
}
#endif
