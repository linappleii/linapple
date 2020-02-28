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

/* Description: main
 *
 * Author: Various
 */

/* Adaptation for SDL and POSIX (l) by beom beotiger, Nov-Dec 2007, krez beotiger March 2012 AD */
/*

Linappple-pie was adapted in OCT 2015 for use with Retropie.
By Mark Ormond.
*/


#include "stdafx.h"
#include <cassert>
#include <string>
#include <vector>
//#pragma  hdrstop
#include "Log.h"
#include "MouseInterface.h"

// for time logging
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <unistd.h>
#include <curl/curl.h>
#include <stdlib.h>

#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

#include "asset.h"

#ifdef __APPLE__
#include "AlertHooks.h"
#endif

#ifdef __APPLE__
#include "AlertHooks.h"
#endif

//char VERSIONSTRING[] = "xx.yy.zz.ww";

// Satisfy modern compiler standards
static char TITLE_APPLE_2_[] = TITLE_APPLE_2;
static char TITLE_APPLE_2_PLUS_[] = TITLE_APPLE_2_PLUS;
static char TITLE_APPLE_2E_[] = TITLE_APPLE_2E;
static char TITLE_APPLE_2E_ENHANCED_[] = TITLE_APPLE_2E_ENHANCED;

TCHAR *g_pAppTitle = TITLE_APPLE_2E_ENHANCED_;

eApple2Type g_Apple2Type = A2TYPE_APPLE2EEHANCED;

BOOL behind = 0;      // Redundant
DWORD cumulativecycles = 0;      // Wraps after ~1hr 9mins
DWORD cyclenum = 0;      // Used by SpkrToggle() for non-wave sound
DWORD emulmsec = 0;
static DWORD emulmsec_frac = 0;
bool g_bFullSpeed = false;
bool hddenabled = false;
static bool g_bBudgetVideo = false;
static bool g_uMouseInSlot4 = false;  // not any mouse in slot4??--bb
// Win32
//HINSTANCE g_hInstance          = (HINSTANCE)0;

AppMode_e g_nAppMode = MODE_LOGO;

// Default screen sizes
// SCREEN_WIDTH & SCREEN_HEIGHT defined in Frame.h
UINT g_ScreenWidth = SCREEN_WIDTH;
UINT g_ScreenHeight = SCREEN_HEIGHT;

//static int lastmode         = MODE_LOGO;    -- not used???
DWORD needsprecision = 0;      // Redundant
TCHAR g_sProgramDir[MAX_PATH] = TEXT("");
TCHAR g_sCurrentDir[MAX_PATH] = TEXT(""); // Also Starting Dir for Slot6 disk images?? --bb
TCHAR g_sHDDDir[MAX_PATH] = TEXT(""); // starting dir for HDV (Apple][ HDD) images?? --bb
TCHAR g_sSaveStateDir[MAX_PATH] = TEXT(""); // starting dir for states --bb
TCHAR g_sParallelPrinterFile[MAX_PATH] = TEXT("Printer.txt");  // default file name for Parallel printer

// FTP Variables
TCHAR g_sFTPLocalDir[MAX_PATH] = TEXT(""); // FTP Local Dir, see linapple.conf for details
TCHAR g_sFTPServer[MAX_PATH] = TEXT(""); // full path to default FTP server
TCHAR g_sFTPServerHDD[MAX_PATH] = TEXT(""); // full path to default FTP server

//TCHAR     g_sFTPUser[256] = TEXT("anonymous"); // user name
//TCHAR     g_sFTPPass[256] = TEXT("mymail@hotmail.com"); // password
TCHAR g_sFTPUserPass[512] = TEXT("anonymous:mymail@hotmail.com"); // full login line

bool g_bResetTiming = false;      // Redundant
BOOL restart = 0;

// several parameters affecting the speed of emulated CPU
DWORD g_dwSpeed = SPEED_NORMAL;  // Affected by Config dialog's speed slider bar
double g_fCurrentCLK6502 = CLK_6502;  // Affected by Config dialog's speed slider bar
static double g_fMHz = 1.0;      // Affected by Config dialog's speed slider bar

int g_nCpuCyclesFeedback = 0;
DWORD g_dwCyclesThisFrame = 0;

FILE *g_fh = NULL; // file for logging, let's use stderr instead?
bool g_bDisableDirectSound = false;  // direct sound, use SDL Sound, or SDL_mixer???

CSuperSerialCard sg_SSC;
CMouseInterface sg_Mouse;

UINT g_Slot4 = CT_Mockingboard;  // CT_Mockingboard or CT_MouseInterface

CURL *g_curl = NULL;  // global easy curl resourse
//===========================================================================

// ???? what is DBG_CALC_FREQ???  O_O   --bb
#define DBG_CALC_FREQ 0
#if DBG_CALC_FREQ
const UINT MAX_CNT = 256;
double g_fDbg[MAX_CNT];
UINT g_nIdx = 0;
double g_fMeanPeriod,g_fMeanFreq;
ULONG g_nPerfFreq = 0;
#endif

//For MacOsX
/*int DlgAlert_Notice(char *text)
{
#ifdef ALERT_HOOKS
    //if (!Main_UnPauseEmulation())
    //    Main_PauseEmulation(true);
    //if(!bInFullScreen)
        return HookedAlertNotice(text);
#endif
}
*/

//---------------------------------------------------------------------------

void ContinueExecution()
{
  static BOOL pageflipping = 0; //?

  const double fUsecPerSec = 1.e6;

  const UINT nExecutionPeriodUsec = 1000;    // 1.0ms
  const double fExecutionPeriodClks = g_fCurrentCLK6502 * ((double) nExecutionPeriodUsec / fUsecPerSec);

  bool bScrollLock_FullSpeed = g_bScrollLock_FullSpeed; //g_uScrollLockToggle;

  g_bFullSpeed = ((g_dwSpeed == SPEED_MAX) || bScrollLock_FullSpeed ||
                  (DiskIsSpinning() && enhancedisk && !Spkr_IsActive() && !MB_IsActive()));

  if (g_bFullSpeed) {
    // Don't call Spkr_Mute() - will get speaker clicks
    MB_Mute();
    SysClk_StopTimer();
    g_nCpuCyclesFeedback = 0;  // For the case when this is a big -ve number
    //    SetPriorityNormal();
  } else {
    // Don't call Spkr_Demute()
    MB_Demute();
    SysClk_StartTimerUsec(nExecutionPeriodUsec);
    // Switch to higher priority, eg. for audio (BUG #015394)
    //    SetPriorityAboveNormal();
  }

  //

  int nCyclesToExecute = (int) fExecutionPeriodClks + g_nCpuCyclesFeedback;
  if (nCyclesToExecute < 0)
    nCyclesToExecute = 0;

  DWORD dwExecutedCycles = CpuExecute(nCyclesToExecute);
  g_dwCyclesThisFrame += dwExecutedCycles;

  //

  cyclenum = dwExecutedCycles;

  DiskUpdatePosition(dwExecutedCycles);
  JoyUpdatePosition();
  // the next call does not present  in current Applewin as on March 2012??
  VideoUpdateVbl(g_dwCyclesThisFrame);

  SpkrUpdate(cyclenum);
  sg_SSC.CommUpdate(cyclenum);
  PrintUpdate(cyclenum);

  //

  const DWORD CLKS_PER_MS = (DWORD) g_fCurrentCLK6502 / 1000;

  emulmsec_frac += dwExecutedCycles;
  if (emulmsec_frac > CLKS_PER_MS) {
    emulmsec += emulmsec_frac / CLKS_PER_MS;
    emulmsec_frac %= CLKS_PER_MS;
  }

  //
  // DETERMINE WHETHER THE SCREEN WAS UPDATED, THE DISK WAS SPINNING,
  // OR THE KEYBOARD I/O PORTS WERE BEING EXCESSIVELY QUERIED THIS CLOCKTICK
  VideoCheckPage(0);
  BOOL screenupdated = VideoHasRefreshed();
  BOOL systemidle = 0;  //(KeybGetNumQueries() > (clockgran << 2));  //  && (!ranfinegrain);  // TO DO

  if (screenupdated)
    pageflipping = 3;

  //

  if (g_dwCyclesThisFrame >= dwClksPerFrame) {
    g_dwCyclesThisFrame -= dwClksPerFrame;

    if (g_nAppMode != MODE_LOGO) {
      VideoUpdateFlash();

      static BOOL anyupdates = 0;
      //static DWORD lastcycles     = 0;
      static BOOL lastupdates[2] = {0, 0};

      anyupdates |= screenupdated;

      //

      //lastcycles = cumulativecycles;
      if ((!anyupdates) && (!lastupdates[0]) && (!lastupdates[1]) && VideoApparentlyDirty()) {
        VideoCheckPage(1);
        static DWORD lasttime = 0;
        DWORD currtime = GetTickCount();
        if ((!g_bFullSpeed) || (currtime-lasttime >= (DWORD)((graphicsmode || !systemidle) ? 100 : 25))) {
          if (!g_bBudgetVideo || (currtime - lasttime >= 200)) {   // update every 12 frames
            VideoRefreshScreen();
            lasttime = currtime;
          }
        }
        screenupdated = 1;
      }

      lastupdates[1] = lastupdates[0];
      lastupdates[0] = anyupdates;
      anyupdates = 0;

      if (pageflipping)
        pageflipping--;
    }

    MB_EndOfVideoFrame();
  }

  //

  if (!g_bFullSpeed) {
    SysClk_WaitTimer();

    #if DBG_CALC_FREQ
    if(g_nPerfFreq)
    {
      //QueryPerformanceCounter((LARGE_INTEGER*)&nTime1); QueryPerformanceFrequency
      LONG nTime1 = GetTickCount();//no QueryPerformanceCounter and alike
      LONG nTimeDiff = nTime1 - nTime0;
      double fTime = (double)nTimeDiff / (double)(LONG)g_nPerfFreq;

      g_fDbg[g_nIdx] = fTime;
      g_nIdx = (g_nIdx+1) & (MAX_CNT-1);
      g_fMeanPeriod = 0.0;
      for(UINT n=0; n<MAX_CNT; n++)
        g_fMeanPeriod += g_fDbg[n];
      g_fMeanPeriod /= (double)MAX_CNT;
      g_fMeanFreq = 1.0 / g_fMeanPeriod;
    }
    #endif
  }
}

// SetBudgetVideo
// This sets the video to only update every 12 60Hz frames for
// computers with limited graphics hardware.
// Entry: BudgetVideo on/off
void SetBudgetVideo(bool budgetVideo)
{
  g_bBudgetVideo = budgetVideo;
}

// GetBudgetVideo
// This returns the current BudgetVideo status
// Exit: BudgetVideo
bool GetBudgetVideo()
{
  return g_bBudgetVideo;
}

//===========================================================================

void SetCurrentCLK6502()
{
  static DWORD dwPrevSpeed = (DWORD) - 1;

  if (dwPrevSpeed == g_dwSpeed)
    return;

  dwPrevSpeed = g_dwSpeed;

  // SPEED_MIN    =  0 = 0.50 MHz
  // SPEED_NORMAL = 10 = 1.00 MHz
  //                20 = 2.00 MHz
  // SPEED_MAX-1  = 39 = 3.90 MHz
  // SPEED_MAX    = 40 = ???? MHz (run full-speed, /g_fCurrentCLK6502/ is ignored)


  if (g_dwSpeed < SPEED_NORMAL)
    g_fMHz = 0.5 + (double) g_dwSpeed * 0.05;
  else
    g_fMHz = (double) g_dwSpeed / 10.0;

  g_fCurrentCLK6502 = CLK_6502 * g_fMHz;

  //
  // Now re-init modules that are dependent on /g_fCurrentCLK6502/
  //

  SpkrReinitialize();
  MB_Reinitialize();
}

//===========================================================================
void EnterMessageLoop()
{
  //  MSG message;
  SDL_Event event;

  //  PeekMessage(&message, NULL, 0, 0, PM_NOREMOVE);
  while (true)

    //  while (message.message!=WM_QUIT)
  {
    if (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT && event.key.keysym.sym != SDLK_F4)
        return;
      FrameDispatchMessage(&event);



      //     if (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
      //     {
      //       TranslateMessage(&message);
      //       DispatchMessage(&message);

      while ((g_nAppMode == MODE_RUNNING) || (g_nAppMode == MODE_STEPPING)) {
        if (SDL_PollEvent(&event)) {
          if (event.type == SDL_QUIT && event.key.keysym.sym != SDLK_F4)
            return;
          FrameDispatchMessage(&event);
        } else if (g_nAppMode == MODE_STEPPING) {
          DebugContinueStepping();
        } else {
          ContinueExecution();
          if (g_nAppMode != MODE_DEBUG) {
            if (joyexitenable) {
              CheckJoyExit();
              if (joyquitevent)
                return;
            }
            if (g_bFullSpeed)
              ContinueExecution();
          }
        }
      }
    } else {
      if (g_nAppMode == MODE_DEBUG)
        DebuggerUpdate();
      else if (g_nAppMode == MODE_LOGO || g_nAppMode == MODE_PAUSED)
        SDL_Delay(100);    // Stop process hogging CPU
    }
  }
}

//===========================================================================
// void GetProgramDirectory () {
//   GetModuleFileName((HINSTANCE)0,g_sProgramDir,MAX_PATH);
//   g_sProgramDir[MAX_PATH-1] = 0;
//   int loop = _tcslen(g_sProgramDir);
//   while (loop--)
//     if ((g_sProgramDir[loop] == TEXT('\\')) ||
//         (g_sProgramDir[loop] == TEXT(':'))) {
//       g_sProgramDir[loop+1] = 0;
//       break;
//     }
// }


//---------------------------------------------------------------------------

int DoDiskInsert(int nDrive, LPSTR szFileName)
{
  //   DWORD dwAttributes = GetFileAttributes(szFileName);
  //
  //
  //   if(dwAttributes == INVALID_FILE_ATTRIBUTES)
  //   {
  //     return -1;
  //   }
  //
  //   BOOL bWriteProtected = (dwAttributes & FILE_ATTRIBUTE_READONLY) ? TRUE : FALSE;

  return DiskInsert(nDrive, szFileName, 0, 0);
}

bool ValidateDirectory(char *dir)
{
  bool ret = false;
  if (dir && *dir) {
    struct stat st;
    if (stat("/tmp", &st) == 0)
      if ((st.st_mode & S_IFDIR) != 0)
        ret = true;
  }
  printf("%s is dir? %d\n", dir, ret);
  return ret;
}

void SetDiskImageDirectory(char *regKey, int driveNumber)
{
  char *szHDFilename = NULL;
  if (RegLoadString(TEXT("Configuration"), TEXT(regKey), 1, &szHDFilename, MAX_PATH)) {
    if (!ValidateDirectory(szHDFilename)) {
      RegSaveString(TEXT("Configuration"), TEXT(regKey), 1, "/");
      RegLoadString(TEXT("Configuration"), TEXT(regKey), 1, &szHDFilename, MAX_PATH);
    }

    DoDiskInsert(driveNumber, szHDFilename);
    free(szHDFilename);
  }
}

//Sets the emulator to automatically boot, rather than load the flash screen on startup
void setAutoBoot()
{
  // autostart
  SDL_Event user_ev;
  user_ev.type = SDL_USEREVENT;
  user_ev.user.code = 1;  //restart
  SDL_PushEvent(&user_ev);
}

//===========================================================================
// Let us load main configuration from config file.  Y_Y  --bb
void LoadConfiguration()
{
  if (registry) {
    DWORD dwComputerType;
    LOAD(TEXT("Computer Emulation"), &dwComputerType);

    switch (dwComputerType) {
      // NB. No A2TYPE_APPLE2E

      case 0:
        g_Apple2Type = A2TYPE_APPLE2;
        break;
      case 1:
        g_Apple2Type = A2TYPE_APPLE2PLUS;
        break;
      case 2:
        g_Apple2Type = A2TYPE_APPLE2EEHANCED;
        break;
      default:
        g_Apple2Type = A2TYPE_APPLE2EEHANCED;
        break;
    }
  }

  // determine Apple type and set appropriate caption -- should be in (F9)switching modes?
  switch (g_Apple2Type) {
    case A2TYPE_APPLE2:
      g_pAppTitle = TITLE_APPLE_2_;
      break;
    case A2TYPE_APPLE2PLUS:
      g_pAppTitle = TITLE_APPLE_2_PLUS_;
      break;
    case A2TYPE_APPLE2E:
      g_pAppTitle = TITLE_APPLE_2E_;
      break;
    case A2TYPE_APPLE2EEHANCED:
      g_pAppTitle = TITLE_APPLE_2E_ENHANCED_;
      break;
    default:
      break;
  }

  if (registry) {
    LOAD(TEXT("Joystick 0"), &joytype[0]);
    LOAD(TEXT("Joystick 1"), &joytype[1]);
    LOAD(TEXT("Joy0Index"), &joy1index);
    LOAD(TEXT("Joy1Index"), &joy2index);

    LOAD(TEXT("Joy0Button1"), &joy1button1);
    LOAD(TEXT("Joy0Button2"), &joy1button2);
    LOAD(TEXT("Joy1Button1"), &joy2button1);

    LOAD(TEXT("Joy0Axis0"), &joy1axis0);
    LOAD(TEXT("Joy0Axis1"), &joy1axis1);
    LOAD(TEXT("Joy1Axis0"), &joy2axis0);
    LOAD(TEXT("Joy1Axis1"), &joy2axis1);
    LOAD(TEXT("JoyExitEnable"), &joyexitenable);
    LOAD(TEXT("JoyExitButton0"), &joyexitbutton0);
    LOAD(TEXT("JoyExitButton1"), &joyexitbutton1);
  }

  if (joytype[0] == 1)
    printf("Joystick 1 Index # = %i, Name = %s \nButton 1 = %i, Button 2 = %i \nAxis 0 = %i,Axis 1 = %i\n", joy1index,
           SDL_JoystickName(joy1index), joy1button1, joy1button2, joy1axis0, joy1axis1);
  if (joytype[1] == 1)
    printf("Joystick 2 Index # = %i, Name = %s \nButton 1 = %i \nAxis 0 = %i,Axis 1 = %i\n", joy2index,
           SDL_JoystickName(joy2index), joy2button1, joy2axis0, joy2axis1);

  if (registry)
    LOAD(TEXT("Sound Emulation"), &soundtype);

  DWORD dwSerialPort;
  if (registry)
    LOAD(TEXT("Serial Port"), &dwSerialPort);
  sg_SSC.SetSerialPort(dwSerialPort); // ----------- why it is here????

  if (registry) {
    LOAD(TEXT("Emulation Speed"), &g_dwSpeed);

    LOAD(TEXT("Enhance Disk Speed"), (DWORD * ) & enhancedisk);//
    LOAD(TEXT("Video Emulation"), &g_videotype);
  }
  //  printf("Video Emulation = %d\n", videotype);

  DWORD dwTmp = 0;  // temp var

  if (registry)
    LOAD(TEXT("Fullscreen"), &dwTmp);  // load fullscreen flag
  fullscreen = (BOOL) dwTmp;
  dwTmp = 1;
  if (registry)
    LOAD(TEXT(REGVALUE_SHOW_LEDS), &dwTmp);  // load Show Leds flag
  g_ShowLeds = (BOOL) dwTmp;

  //printf("Fullscreen = %d\n", fullscreen);
  //  LOAD(TEXT("Uthernet Active")  ,(DWORD *)&tfe_enabled);

  SetCurrentCLK6502();  // set up real speed

  //
  if (registry)
    if (LOAD(TEXT(REGVALUE_MOUSE_IN_SLOT4), &dwTmp))
      g_uMouseInSlot4 = dwTmp;
  g_Slot4 = g_uMouseInSlot4 ? CT_MouseInterface : CT_Mockingboard;

  //   if(LOAD(TEXT(REGVALUE_SPKR_VOLUME), &dwTmp))
  //       SpkrSetVolume(dwTmp, 100);    // volume by default?
  //
  //   if(LOAD(TEXT(REGVALUE_MB_VOLUME), &dwTmp))
  //       MB_SetVolume(dwTmp, 100);      // volume by default?? --bb

  if (registry)
    if (LOAD(TEXT(REGVALUE_SOUNDCARD_TYPE), &dwTmp))
      MB_SetSoundcardType((eSOUNDCARDTYPE) dwTmp);

  if (registry)
    if (LOAD(TEXT(REGVALUE_SAVE_STATE_ON_EXIT), &dwTmp))
      g_bSaveStateOnExit = (dwTmp != 0);

  if (registry)
    if (LOAD(TEXT(REGVALUE_HDD_ENABLED), &dwTmp))
      hddenabled = (bool) dwTmp;// after MemInitialize
  //    HD_SetEnabled(dwTmp ? true : false);
  //  printf("g_bHD_Enabled = %d\n", g_bHD_Enabled);

  char *szHDFilename = NULL;

  if (registry)
    if (RegLoadString(TEXT("Configuration"), TEXT("Monochrome Color"), 1, &szHDFilename, 10)) {
      if (!sscanf(szHDFilename, "#%X", &monochrome))
        monochrome = 0xC0C0C0;
      free(szHDFilename);
      szHDFilename = NULL;
    }

  dwTmp = 0;
  if (registry)
    LOAD(TEXT("Boot at Startup"), &dwTmp);  //

  if (dwTmp) {
    // autostart
    setAutoBoot();
  }

  dwTmp = 0;
  if (registry)
    LOAD(TEXT("Slot 6 Autoload"), &dwTmp);  // load autoinsert for Slot 6 flag
  if (dwTmp) {
    // Load floppy disk images and insert it automatically in slot 6 drive 1 and 2
    static char szDiskImage1[] = REGVALUE_DISK_IMAGE1;
    SetDiskImageDirectory(szDiskImage1, 0);
    SetDiskImageDirectory(szDiskImage1, 1);
  } else {
    Asset_InsertMasterDisk();
  }

  // Load hard disk images and insert it automatically in slot 7
  if (registry)
    if (RegLoadString(TEXT("Configuration"), TEXT(REGVALUE_HDD_IMAGE1), 1, &szHDFilename, MAX_PATH)) {
      //    printf("LoadConfiguration: returned string is: %s\n", szHDFilename);
      HD_InsertDisk2(0, szHDFilename);
      free(szHDFilename);
      szHDFilename = NULL;
    }

  if (registry)
    if (RegLoadString(TEXT("Configuration"), TEXT(REGVALUE_HDD_IMAGE2), 1, &szHDFilename, MAX_PATH)) {
      //    printf("LoadConfiguration: returned string is: %s\n", szHDFilename);
      HD_InsertDisk2(1, szHDFilename);
      free(szHDFilename);
      szHDFilename = NULL;
    }

  // file name for Parallel Printer
  if (registry)
    if (RegLoadString(TEXT("Configuration"), TEXT(REGVALUE_PPRINTER_FILENAME), 1, &szHDFilename, MAX_PATH)) {
      if (strlen(szHDFilename) > 1)
        strncpy(g_sParallelPrinterFile, szHDFilename, MAX_PATH);
      free(szHDFilename);
      szHDFilename = NULL;
    }


  // for joysticks use default Y-,X-trims
  //   if(LOAD(TEXT(REGVALUE_PDL_XTRIM), &dwTmp))
  //       JoySetTrim((short)dwTmp, true);
  //   if(LOAD(TEXT(REGVALUE_PDL_YTRIM), &dwTmp))
  //       JoySetTrim((short)dwTmp, false);
  // we do not use this, scroll lock ever toggling full-speed???
  //   if(LOAD(TEXT(REGVALUE_SCROLLLOCK_TOGGLE), &dwTmp))
  //     g_uScrollLockToggle = dwTmp;

  //

  char *szFilename = NULL;
  double scrFactor = 0.0;
  // Define screen sizes
  if (registry)
    if (RegLoadString(TEXT("Configuration"), TEXT("Screen factor"), 1, &szFilename, 16)) {
      scrFactor = atof(szFilename);
      if (scrFactor > 0.1) {
        g_ScreenWidth = UINT(g_ScreenWidth * scrFactor);
        g_ScreenHeight = UINT(g_ScreenHeight * scrFactor);
      }
      free(szFilename);
      szFilename = NULL;
    }

  if (registry)
    if (scrFactor <= 0.1) {
      // Try to set Screen Width & Height directly
      dwTmp = 0;
      LOAD(TEXT("Screen Width"), &dwTmp);
      if (dwTmp > 0)
        g_ScreenWidth = dwTmp;
      dwTmp = 0;
      LOAD(TEXT("Screen Height"), &dwTmp);
      if (dwTmp > 0)
        g_ScreenHeight = dwTmp;
    }

  if (registry)
    if (RegLoadString(TEXT("Configuration"), TEXT(REGVALUE_SAVESTATE_FILENAME), 1, &szFilename, MAX_PATH)) {
      Snapshot_SetFilename(szFilename);  // If not in Registry than default will be used
      free(szFilename);
      szFilename = NULL;
    }

  // Current/Starting Dir is the "root" of where the user keeps his disk images
  if (registry)
    RegLoadString(TEXT("Preferences"), REGVALUE_PREF_START_DIR, 1, &szFilename, MAX_PATH);

  if (szFilename) {
    strcpy(g_sCurrentDir, szFilename);
    free(szFilename);
    szFilename = NULL;
  }
  //  SetCurrentDirectory(g_sCurrentDir);
  if (strlen(g_sCurrentDir) == 0 || g_sCurrentDir[0] != '/') //something is wrong in dir name?
  {//
    char *tmp = getenv("HOME"); /* we don't have HOME?  ^_^  0_0  $_$  */
    if (tmp == NULL)
      strcpy(g_sCurrentDir, "/");  //begin from the root, then
    else
      strcpy(g_sCurrentDir, tmp);
  }

  // Load starting directory for HDV (Apple][ HDD) images
  if (registry)
    RegLoadString(TEXT("Preferences"), REGVALUE_PREF_HDD_START_DIR, 1, &szFilename, MAX_PATH);

  if (szFilename) {
    strcpy(g_sHDDDir, szFilename);
    free(szFilename);
    szFilename = NULL;
  }
  //  SetCurrentDirectory(g_sCurrentDir);
  if (strlen(g_sHDDDir) == 0 || g_sHDDDir[0] != '/') //something is wrong in dir name?
  {
    char *tmp = getenv("HOME"); /* we don't have HOME?  ^_^  0_0  $_$  */
    if (tmp == NULL)
      strcpy(g_sHDDDir, "/");  //begin from the root, then
    else
      strcpy(g_sHDDDir, tmp);
  }


  // Load starting directory for saving current states
  if (registry)
    RegLoadString(TEXT("Preferences"), REGVALUE_PREF_SAVESTATE_DIR, 1, &szFilename, MAX_PATH);
  if (szFilename) {
    strcpy(g_sSaveStateDir, szFilename);
    free(szFilename);
    szFilename = NULL;
  }
  if (strlen(g_sSaveStateDir) == 0 || g_sSaveStateDir[0] != '/') //something is wrong in dir name?
  {
    char *tmp = getenv("HOME"); /* we don't have HOME?  ^_^  0_0  $_$  */
    if (tmp == NULL)
      strcpy(g_sSaveStateDir, "/");  //begin from the root, then
    else
      strcpy(g_sSaveStateDir, tmp);
  }

  // Read and fill FTP variables - server, local dir, user name and password
  if (registry)
    RegLoadString(TEXT("Preferences"), REGVALUE_FTP_DIR, 1, &szFilename, MAX_PATH);

  if (szFilename) {
    strcpy(g_sFTPServer, szFilename);
    free(szFilename);
    szFilename = NULL;
  }

  if (registry)
    RegLoadString(TEXT("Preferences"), REGVALUE_FTP_HDD_DIR, 1, &szFilename, MAX_PATH);

  if (szFilename) {
    strcpy(g_sFTPServerHDD, szFilename);
    free(szFilename);
    szFilename = NULL;
  }

  if (registry)
    RegLoadString(TEXT("Preferences"), REGVALUE_FTP_LOCAL_DIR, 1, &szFilename, MAX_PATH);

  if (szFilename) {
    strcpy(g_sFTPLocalDir, szFilename);
    free(szFilename);
    szFilename = NULL;
  }

  if (registry)
    RegLoadString(TEXT("Preferences"), REGVALUE_FTP_USERPASS, 1, &szFilename, 512);

  if (szFilename) {
    strcpy(g_sFTPUserPass, szFilename);
    free(szFilename);
    szFilename = NULL;
  }
  // Print some debug strings
  printf("Ready login = %s\n", g_sFTPUserPass);

  //
  // ****By now we deal without Uthernet interface! --bb****
  //   char szUthernetInt[MAX_PATH] = {0};
  //   RegLoadString(TEXT("Configuration"),TEXT("Uthernet Interface"),1,szUthernetInt,MAX_PATH);
  //   update_tfe_interface(szUthernetInt,NULL);

}

// Splits a string into a sequence of substrings each delimited by delimiter.
std::vector <std::string> split(const std::string &string, const std::string &delimiter = " ")
{
  std::vector <std::string> parts;

  size_t last = 0, pos = 0;

  do {
    pos = string.find(delimiter, last);
    parts.push_back(string.substr(last, pos));
    last = pos + 1;
  } while (pos != std::string::npos);

  return parts;
}

// LoadAllConfigurations attempts to load all configuration values in an
// operating system specific manner.
//
// Default configuration values should have already been set statically.
// So, on Linux, check to see if the user specified a config file via a
// command-line switch. If they did, load values from it and no other file.
// Otherwise, try loading from known system-wide configuration locations then
// load from known user-specific configuration locations.
void LoadAllConfigurations(const char *userSpecifiedFilename)
{
  // At this point, registry should not be set. If it is, it means someone
  // probably tried to load configuration values in a different function.
  assert(!registry);

  // Default values should be set, but some other globals that depend on them
  // may not be.
  LoadConfiguration();

  if (userSpecifiedFilename) {
    registry = fopen(userSpecifiedFilename, "r+");
    if (!registry) {
      registry = fopen(userSpecifiedFilename, "w+");
      if (!registry) {
        std::cerr << "WARNING! Failed to open config file: " << userSpecifiedFilename << std::endl;
        exit(EXIT_SUCCESS);
      }
    }
    LoadConfiguration();
    return;
  }

  char *envvar = NULL;

  // Try known system configuration paths.
  envvar = getenv("XDG_CONFIG_DIRS");

  std::string xdgConfigDirs = envvar ? envvar : "";
  if (xdgConfigDirs.length() == 0)
    xdgConfigDirs = "/etc/xdg";

  std::vector <std::string> sysConfigDirs(split(xdgConfigDirs, ":"));

  // Suppport the old /etc/linapple/linapple.conf location.
  sysConfigDirs.push_back("/etc");

  for (std::vector<std::string>::reverse_iterator it = sysConfigDirs.rbegin(); it != sysConfigDirs.rend(); it++) {
    std::string config = *it + "/linapple/linapple.conf";
    registry = fopen(config.c_str(), "r");
    if (!registry)
      continue;

    LoadConfiguration();
    fclose(registry);
    registry = NULL;
  }

  // Next, try known user-specified paths.
  char *home = getenv("HOME");

  envvar = getenv("XDG_CONFIG_HOME");
  std::string xdgConfigHome = envvar ? envvar : "";
  if (xdgConfigHome.length() == 0 && home) {
    xdgConfigHome = std::string(home) + "/.config";
  }

  std::vector <std::string> configDir;
  configDir.push_back(xdgConfigDirs + "/linapple/linapple.conf");
  if (home) {
    // Suppport old locations under HOME.
    configDir.push_back(std::string(home) + "/linapple/linapple.conf");
    configDir.push_back(std::string(home) + "/.linapple/linapple.conf");
  }

  std::string lastSuccessfulUserConfig;
  for (std::vector<std::string>::reverse_iterator it = configDir.rbegin(); it != configDir.rend(); it++) {
    registry = fopen((*it).c_str(), "r");
    if (!registry)
      continue;

    lastSuccessfulUserConfig = *it;

    LoadConfiguration();
    fclose(registry);
    registry = NULL;
  }

  // TODO: add a corresponding SaveConfiguration function in which the user's
  // preferences can be saved instead of this hack of opening the last
  // successfully opened user file or creating one if it doesn't exist.

  // Note: there is NO REASON to update the system-wide configuration files in
  // code. Doing so could affect all other users. Instead, a super-user should
  // edit the /etc/xdg/linapple/linapple.conf by hand.
  if (lastSuccessfulUserConfig.length() > 0) {
    registry = fopen(lastSuccessfulUserConfig.c_str(), "r+");
    return;
  }

  if (!home) {
    std::cerr << "WARNING!" << " No HOME set and no known location for user config files."
              << " This can lead to unexpected behavior, even program crashes." << std::endl;
    return;
  }

  std::string userDir(home);
  mkdir((userDir + "/.config").c_str(), 0700);
  mkdir((userDir + "/.config/linapple").c_str(), 0700);
  registry = fopen((userDir + "/.config/linapple/linapple.conf").c_str(), "w+");
}

//===========================================================================
void RegisterExtensions()
{ // TO DO: register extensions for KDE or GNOME desktops?? Do not know, if it is sane idea. He-he. --bb


  //   TCHAR szCommandTmp[MAX_PATH];
  //   GetModuleFileName((HMODULE)0,szCommandTmp,MAX_PATH);
  //
  //   TCHAR command[MAX_PATH];
  //   wsprintf(command, "\"%s\"",  szCommandTmp);  // Wrap  path & filename  in quotes &  null terminate
  //
  //   TCHAR icon[MAX_PATH];
  //   wsprintf(icon,TEXT("%s,1"),(LPCTSTR)command);
  //
  //   _tcscat(command,TEXT(" \"%1\""));      // Append "%1"
  //
  //   RegSetValue(HKEY_CLASSES_ROOT,".bin",REG_SZ,"DiskImage",10);
  //   RegSetValue(HKEY_CLASSES_ROOT,".do"  ,REG_SZ,"DiskImage",10);
  //   RegSetValue(HKEY_CLASSES_ROOT,".dsk",REG_SZ,"DiskImage",10);
  //   RegSetValue(HKEY_CLASSES_ROOT,".nib",REG_SZ,"DiskImage",10);
  //   RegSetValue(HKEY_CLASSES_ROOT,".po"  ,REG_SZ,"DiskImage",10);
  // //  RegSetValue(HKEY_CLASSES_ROOT,".aws",REG_SZ,"DiskImage",10);  // TO DO
  // //  RegSetValue(HKEY_CLASSES_ROOT,".hdv",REG_SZ,"DiskImage",10);  // TO DO
  //
  //   RegSetValue(HKEY_CLASSES_ROOT,
  //         "DiskImage",
  //         REG_SZ,"Disk Image",21);
  //
  //   RegSetValue(HKEY_CLASSES_ROOT,
  //         "DiskImage\\DefaultIcon",
  //         REG_SZ,icon,_tcslen(icon)+1);
  //
  //   RegSetValue(HKEY_CLASSES_ROOT,
  //         "DiskImage\\shell\\open\\command",
  //         REG_SZ,command,_tcslen(command)+1);
  //
  //   RegSetValue(HKEY_CLASSES_ROOT,
  //         "DiskImage\\shell\\open\\ddeexec",
  //         REG_SZ,"%1",3);
  //
  //   RegSetValue(HKEY_CLASSES_ROOT,
  //         "DiskImage\\shell\\open\\ddeexec\\application",
  //         REG_SZ,"applewin",9);
  //
  //   RegSetValue(HKEY_CLASSES_ROOT,
  //         "DiskImage\\shell\\open\\ddeexec\\topic",
  //         REG_SZ,"system",7);
}

void PrintHelp()
{
  printf("usage: linapple [options]\n"
         "\n"
         "LinApple is an emulator for Apple ][, Apple ][+, Apple //e, and Enhanced Apple //e computers.\n"
         "\n"
         "  -h|--help      show this help message\n"
         "  --d1 <file>    insert disk image into first drive\n"
         "  --d2 <file>    insert disk image into second drive\n"
         "  -b|--autoboot  boot/reset at startup\n"
         "  -f             run fullscreen\n"
         "  -l             write log to 'AppleWin.log'\n"
         #ifdef RAMWORKS
         "  -r PAGES       allocate PAGES to Ramworks (1-127)\n"
         #endif
         "  --benchmark    benchmark and quit\n"
         "\n");
}

//---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
  bool bLog = false;
  bool bSetFullScreen = false;
  bool bBoot = false;
  bool bBenchMark = false;
  LPSTR szConfigurationFile = NULL;
  LPSTR szImageName_drive1 = NULL;
  LPSTR szImageName_drive2 = NULL;
  LPSTR szSnapshotFile = NULL;

  int opt;
  int optind = 0;
  const char *optname;
  static struct option longopts[] = {{"autoboot", 0,                 0, 0},
                                     {"conf",     required_argument, 0, 0},
                                     {"d1",       required_argument, 0, 0},
                                     {"d2",       required_argument, 0, 0},
                                     {"help",     0,                 0, 0},
                                     {"state",    required_argument, 0, 0},
                                     {0,          0,                 0, 0}};

  while ((opt = getopt_long(argc, argv, "1:2:abfhlr:", longopts, &optind)) != -1) {
    switch (opt) {
      case '1':
        szImageName_drive1 = optarg;
        break;

      case '2':
        szImageName_drive2 = optarg;
        break;

      case 'b':
        bBoot = true;
        break;

      case 'f':
        bSetFullScreen = true;
        break;

      case 'h':
        PrintHelp();
        return 0;
        break;

      case 'l':
        bLog = true;
        break;

      #ifdef RAMWORKS
      case 'r': // RamWorks size [1..127]
        g_uMaxExPages = atoi(optarg);
        if (g_uMaxExPages > 127)
          g_uMaxExPages = 128;
        else if (g_uMaxExPages < 1)
          g_uMaxExPages = 1;
        break;
      #endif

      case 0:
        optname = longopts[optind].name;
        if (!strcmp(optname, "autoboot")) {
          bBoot = true;
        } else if (!strcmp(optname, "benchmark")) {
          bBenchMark = true;
        } else if (!strcmp(optname, "conf")) {
          szConfigurationFile = optarg;
        } else if (!strcmp(optname, "d1")) {
          szImageName_drive1 = optarg;
        } else if (!strcmp(optname, "d2")) {
          szImageName_drive2 = optarg;
        } else if (!strcmp(optname, "help")) {
          PrintHelp();
          return 0;
        } else if (!strcmp(optname, "state")) {
          szSnapshotFile = optarg;
        } else {
          printf("Unknown option '%s'.\n\n", optname);
          PrintHelp();
          return 255;
        }
        break;

      default:
        printf("Unknown option '%s'.\n\n", optarg);
        PrintHelp();
        return 255;
    }
  }

  if (bLog)
    LogInitialize();
  if (!Asset_Init())
    return 1;

  #if DBG_CALC_FREQ
  //QueryPerformanceFrequency((LARGE_INTEGER*)&g_nPerfFreq);
  g_nPerfFreq = 1000;//milliseconds?
  if(g_fh) fprintf(g_fh, "Performance frequency = %d\n",g_nPerfFreq);
  #endif

  //-----

  // Initialize COM
  // . NB. DSInit() is done when g_hFrameWindow is created (WM_CREATE)

  if (InitSDL())
    return 1; // init SDL subsystems, set icon
  //  CoInitialize( NULL );   ------- what is it initializing?----------------------------------------

  // CURL routines
  curl_global_init(CURL_GLOBAL_DEFAULT);
  g_curl = curl_easy_init();
  if (!g_curl) {
    printf("Could not initialize CURL easy interface");
    return 1;
  }
  /* Set user name and password to access FTP server */
  curl_easy_setopt(g_curl, CURLOPT_USERPWD, g_sFTPUserPass);
  //
  // just do not see why we need this timer???
  /*bool bSysClkOK =*/ SysClk_InitTimer();

  // DO ONE-TIME INITIALIZATION
  //  g_hInstance = passinstance;
  MemPreInitialize();    // Call before any of the slot devices are initialized
  //  GdiSetBatchLimit(512);
  //  GetProgramDirectory();
  //  RegisterExtensions();
  //  FrameRegisterClass();
  ImageInitialize();
  DiskInitialize();
  CreateColorMixMap();  // For tv emulation g_nAppMode

  do {
    // DO INITIALIZATION THAT MUST BE REPEATED FOR A RESTART
    restart = 0;
    g_nAppMode = MODE_LOGO;
    fullscreen = false;

    //Start with default configuration, which we will override if command line options were specified
    LoadAllConfigurations(szConfigurationFile);

    //Overwrite configuration file's set fullscreen option, if one was specified on the command line
    if (bSetFullScreen) {
      fullscreen = bSetFullScreen;
    }

    //This part of the code inserts disks if any were specified on the command line, overwriting the
    //configuration settings.
    int nError = 0;
    if (szImageName_drive1) {
      nError = DoDiskInsert(0, szImageName_drive1);
      if (nError) {
        LOG("Cannot insert image %s into drive 1.", szImageName_drive1);
        break;
      }
    }
    if (szImageName_drive2) {
      nError |= DoDiskInsert(1, szImageName_drive2);
      if (nError) {
        LOG("Cannot insert image %s into drive 2.", szImageName_drive2);
        break;
      }
    }

    FrameCreateWindow();

    if (!DSInit())
      soundtype = SOUND_NONE;    // Direct Sound and Stuff

    MB_Initialize();  // Mocking board
    SpkrInitialize();  // Speakers - of Apple][ ...grrrrrrrrrrr, I love them!--bb
    DebugInitialize();
    JoyInitialize();
    MemInitialize();
    HD_SetEnabled(hddenabled);
    //printf("g_bHD_Enabled = %d\n", g_bHD_Enabled);

    VideoInitialize();


    //     if (!bSysClkOK)
    //     {
    //       MessageBox(g_hFrameWindow, "DirectX failed to create SystemClock instance", TEXT("AppleWin Error"), MB_OK);
    //       PostMessage(g_hFrameWindow, WM_DESTROY, 0, 0);  // Close everything down
    //     }

    //    tfe_init();
    Snapshot_Startup();    // Do this after everything has been init'ed
    if (szSnapshotFile) {
      Snapshot_SetFilename(szSnapshotFile);
      LOG("[main ] using state file '%s'\n", Snapshot_GetFilename());
      Snapshot_LoadState();
    }


    /*  ------Will be fullscreened and booted later. I promise. --bb          */
    //     if(bSetFullScreen)
    //     {
    //       PostMessage(g_hFrameWindow, WM_KEYDOWN, VK_F1+BTN_FULLSCR, 0);
    //       PostMessage(g_hFrameWindow, WM_KEYUP,   VK_F1+BTN_FULLSCR, 0);
    //       bSetFullScreen = false;
    //     }
    //
    //Automatically boot from disk if specified on the command line
    if (bBoot) {
      setAutoBoot();
    }

    JoyReset();
    SetUsingCursor(0);

    // trying fullscreen
    if (!fullscreen)
      SetNormalMode();
    else
      SetFullScreenMode();

    DrawFrameWindow();  // we do not have WM_PAINT?


    // ENTER THE MAIN MESSAGE LOOP
    if (bBenchMark)
      VideoBenchmark(); // start VideoBenchmark and exit
    else
      EnterMessageLoop();  // else we just start game
    // on WM_DESTROY event:


    Snapshot_Shutdown();
    DebugDestroy();
    //    printf("Quitting. Snapshot_Shutdown-ed!\n");
    if (!restart) {
      DiskDestroy();
      ImageDestroy();
      HD_Cleanup();
    }
    //    printf("Quitting. DiskDestroy, ImageDestroy and HD_Cleanup!\n");
    PrintDestroy();
    sg_SSC.CommDestroy();
    CpuDestroy();
    MemDestroy();
    //    printf("Quitting. PrintDestroy, sg_SSC.CommDestroy, CPU-MEMDestroy!\n");
    SpkrDestroy();
    //    printf("Quitting. SpkrDestroy!!\n");

    VideoDestroy();
    //    printf("Quitting. VideoDestroy!!\n");
    MB_Destroy();
    //    printf("Quitting. MB_Destroy!\n");
    // end of WM_DESTROY event
    MB_Reset();
    //    printf("Quitting. MB_Reset!\n");
    sg_Mouse.Uninitialize(); // Maybe restarting due to switching slot-4 card from mouse to MB
    //    printf("Quitting. Mouse.Uninitialize!!!\n");
    JoyShutDown();  // close opened (if any) joysticks
  } while (restart);

  // Release COM
  DSUninit();
  SysClk_UninitTimer();
  //  CoUninitialize();------------------------------- what is it uninitializing?--------------------------

  //  tfe_shutdown();

  RiffFinishWriteFile();
  if (registry != NULL) {
    fclose(registry);    //close conf file (linapple.conf by default)
  }

  SDL_Quit();
  curl_easy_cleanup(g_curl);
  curl_global_cleanup();
  Asset_Quit();
  LogDestroy();
  printf("Linapple: successfully exited!\n");
  return 0;
}

