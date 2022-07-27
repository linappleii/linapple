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
#include <X11/Xlib.h>
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

// Satisfy modern compiler standards
static char TITLE_APPLE_2_[] = TITLE_APPLE_2;
static char TITLE_APPLE_2_PLUS_[] = TITLE_APPLE_2_PLUS;
static char TITLE_APPLE_2E_[] = TITLE_APPLE_2E;
static char TITLE_APPLE_2E_ENHANCED_[] = TITLE_APPLE_2E_ENHANCED;

char *g_pAppTitle = TITLE_APPLE_2E_ENHANCED_;

// backend video x11, fbcon, dispmanx, kmsdrm, etc.
char videoDriverName[100];

eApple2Type g_Apple2Type = A2TYPE_APPLE2EEHANCED;

bool behind = 0;
unsigned int cumulativecycles = 0;      // Wraps after ~1hr 9mins
unsigned int cyclenum = 0;
unsigned int emulmsec = 0;
static unsigned int emulmsec_frac = 0;
bool g_bFullSpeed = false;
bool hddenabled = false;
unsigned int clockslot;
static bool g_bBudgetVideo = false;
static bool g_uMouseInSlot4 = false;  // not any mouse in slot4??--bb

AppMode_e g_nAppMode = MODE_LOGO;

// Default screen sizes
// SCREEN_WIDTH & SCREEN_HEIGHT defined in Frame.h
unsigned int g_ScreenWidth = SCREEN_WIDTH;
unsigned int g_ScreenHeight = SCREEN_HEIGHT;

unsigned int needsprecision = 0;
char g_sProgramDir[MAX_PATH] = TEXT("");
char g_sCurrentDir[MAX_PATH] = TEXT(""); // Also Starting Dir for Slot6 disk images?? --bb
char g_sHDDDir[MAX_PATH] = TEXT(""); // starting dir for HDV (Apple][ HDD) images?? --bb
char g_sSaveStateDir[MAX_PATH] = TEXT(""); // starting dir for states --bb
char g_sParallelPrinterFile[MAX_PATH] = TEXT("Printer.txt");  // default file name for Parallel printer

// FTP Variables
char g_sFTPLocalDir[MAX_PATH] = TEXT(""); // FTP Local Dir, see linapple.conf for details
char g_sFTPServer[MAX_PATH] = TEXT(""); // full path to default FTP server
char g_sFTPServerHDD[MAX_PATH] = TEXT(""); // full path to default FTP server

char g_sFTPUserPass[512] = TEXT("anonymous:mymail@hotmail.com"); // full login line

bool g_bResetTiming = false;
bool restart = 0;

// several parameters affecting the speed of emulated CPU
unsigned int g_dwSpeed = SPEED_NORMAL;  // Affected by Config dialog's speed slider bar
double g_fCurrentCLK6502 = CLOCK_6502;  // Affected by Config dialog's speed slider bar
static double g_fMHz = 1.0;      // Affected by Config dialog's speed slider bar

int g_nCpuCyclesFeedback = 0;
unsigned int g_dwCyclesThisFrame = 0;

FILE *g_fh = NULL; // file for logging, let's use stderr instead?
bool g_bDisableDirectSound = false;  // direct sound, use SDL Sound, or SDL_mixer???

CSuperSerialCard sg_SSC;
CMouseInterface sg_Mouse;

unsigned int g_Slot4 = CT_Mockingboard;  // CT_Mockingboard or CT_MouseInterface
CURL *g_curl = NULL;  // global easy curl resourse

#define DBG_CALC_FREQ 0
#if DBG_CALC_FREQ
const unsigned int MAX_CNT = 256;
double g_fDbg[MAX_CNT];
unsigned int g_nIdx = 0;
double g_fMeanPeriod,g_fMeanFreq;
ULONG g_nPerfFreq = 0;
#endif

static unsigned int g_uModeStepping_Cycles = 0;
static bool g_uModeStepping_LastGetKey_ScrollLock = false;

void ContinueExecution()
{
  const double fUsecPerSec = 1.e6;
  const unsigned int nExecutionPeriodUsec = 1000;    // 1.0ms
  const double fExecutionPeriodClks = g_fCurrentCLK6502 * ((double) nExecutionPeriodUsec / fUsecPerSec);

  bool bScrollLock_FullSpeed = g_bScrollLock_FullSpeed;

  g_bFullSpeed = ((g_dwSpeed == SPEED_MAX) || bScrollLock_FullSpeed || IsDebugSteppingAtFullSpeed() ||
                  (DiskIsSpinning() && enhancedisk && !Spkr_IsActive() && !MB_IsActive()));

  if (g_bFullSpeed) {
    // Don't call Spkr_Mute() - will get speaker clicks
    MB_Mute();
    SysClk_StopTimer();
    g_nCpuCyclesFeedback = 0;  // For the case when this is a big -ve number
  } else {
    // Don't call Spkr_Demute()
    MB_Demute();
    SysClk_StartTimerUsec(nExecutionPeriodUsec);
  }

  int nCyclesWithFeedback = (int) fExecutionPeriodClks + g_nCpuCyclesFeedback;
  if (nCyclesWithFeedback < 0) {
    nCyclesWithFeedback = 0;
  }
  const DWORD uCyclesToExecuteWithFeedback = (nCyclesWithFeedback >= 0) ? nCyclesWithFeedback
                                       : 0;

  const DWORD uCyclesToExecute = (g_nAppMode == MODE_RUNNING)   ? uCyclesToExecuteWithFeedback
                          /* MODE_STEPPING */ : 0;

  DWORD uActualCyclesExecuted = CpuExecute(uCyclesToExecute);
  g_dwCyclesThisFrame += uActualCyclesExecuted;

  cyclenum = uActualCyclesExecuted;

  DiskUpdatePosition(uActualCyclesExecuted);
  JoyUpdatePosition();
  // the next call does not present in current Applewin as on March 2012??
  VideoUpdateVbl(g_dwCyclesThisFrame);
  //

  unsigned int uSpkrActualCyclesExecuted = uActualCyclesExecuted;

  bool bModeStepping_WaitTimer = false;
  if (g_nAppMode == MODE_STEPPING && !IsDebugSteppingAtFullSpeed())
  {
    g_uModeStepping_Cycles += uActualCyclesExecuted;
    if (g_uModeStepping_Cycles >= uCyclesToExecuteWithFeedback)
    {
      uSpkrActualCyclesExecuted = g_uModeStepping_Cycles;

      g_uModeStepping_Cycles -= uCyclesToExecuteWithFeedback;
      bModeStepping_WaitTimer = true;
    }
  }

  // For MODE_STEPPING: do this speaker update periodically
  // - Otherwise kills performance due to sound-buffer lock/unlock for every 6502 opcode!
  if (g_nAppMode == MODE_RUNNING || bModeStepping_WaitTimer)
    SpkrUpdate(uSpkrActualCyclesExecuted);

  sg_SSC.CommUpdate(cyclenum);
  PrintUpdate(cyclenum);

  const unsigned int CLKS_PER_MS = (unsigned int) g_fCurrentCLK6502 / 1000;

  emulmsec_frac += uActualCyclesExecuted;
  if (emulmsec_frac > CLKS_PER_MS) {
    emulmsec += emulmsec_frac / CLKS_PER_MS;
    emulmsec_frac %= CLKS_PER_MS;
  }

  // Determine whether the screen was updated, the disk was spinning,
  // or the keyboard I/O ports were being excessively queried this clocktick
  if (g_singlethreaded) {
    VideoCheckPage(0);
  }
  bool screenupdated = VideoHasRefreshed();
  screenupdated |= (!g_singlethreaded);
  bool systemidle = 0;

  if (g_dwCyclesThisFrame >= dwClksPerFrame) {
    g_dwCyclesThisFrame -= dwClksPerFrame;

    if (g_nAppMode != MODE_LOGO) {
      VideoUpdateFlash();

      static bool anyupdates = 0;
      static bool lastupdates[2] = {0, 0};

      anyupdates |= screenupdated;
      bool update_clause = ((!anyupdates) && (!lastupdates[0]) && (!lastupdates[1])) || (!g_singlethreaded);
      if (update_clause && VideoApparentlyDirty()) {
        VideoCheckPage(1);
        static unsigned int lasttime = 0;
        unsigned int currtime = GetTickCount();
        if ((!g_bFullSpeed) || (currtime - lasttime >= (unsigned int)((graphicsmode || !systemidle) ? 100 : 25))) {
          if (!g_bBudgetVideo || (currtime - lasttime >= 200)) {   // update every 12 frames
            VideoRefreshScreen();
            if (!g_singlethreaded) {
              // This tells the video to schedule a frame update now.
              // It will run in another thread, another core.
              VideoSetNextScheduledUpdate();
            }

            lasttime = currtime;
          }
        }
        screenupdated = 1;
      }

      lastupdates[1] = lastupdates[0];
      lastupdates[0] = anyupdates;
      anyupdates = 0;
    }
    MB_EndOfVideoFrame();
  }

  if ((g_nAppMode == MODE_RUNNING && !g_bFullSpeed) || bModeStepping_WaitTimer)
  {
    SysClk_WaitTimer();

    #if DBG_CALC_FREQ
    if(g_nPerfFreq)
    {
      int nTime1 = GetTickCount(); //no QueryPerformanceCounter and alike
      int nTimeDiff = nTime1 - nTime0;
      double fTime = (double)nTimeDiff / (double)(int)g_nPerfFreq;

      g_fDbg[g_nIdx] = fTime;
      g_nIdx = (g_nIdx+1) & (MAX_CNT-1);
      g_fMeanPeriod = 0.0;
      for(unsigned int n=0; n<MAX_CNT; n++) {
        g_fMeanPeriod += g_fDbg[n];
      }
      g_fMeanPeriod /= (double)MAX_CNT;
      g_fMeanFreq = 1.0 / g_fMeanPeriod;
    }
    #endif
  }
}

void SingleStep(bool bReinit)
{
  if (bReinit)
  {
    g_uModeStepping_Cycles = 0;
    g_uModeStepping_LastGetKey_ScrollLock = false;
  }

  ContinueExecution();
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

void SetCurrentCLK6502()
{
  static unsigned int dwPrevSpeed = (unsigned int) - 1;

  if (dwPrevSpeed == g_dwSpeed) {
    return;
  }

  dwPrevSpeed = g_dwSpeed;

  // SPEED_MIN    =  0 = 0.50 MHz
  // SPEED_NORMAL = 10 = 1.00 MHz
  //                20 = 2.00 MHz
  // SPEED_MAX-1  = 39 = 3.90 MHz
  // SPEED_MAX    = 40 = ???? MHz (run full-speed, /g_fCurrentCLK6502/ is ignored)

  if (g_dwSpeed < SPEED_NORMAL) {
    g_fMHz = 0.5 + (double) g_dwSpeed * 0.05;
  } else {
    g_fMHz = (double) g_dwSpeed / 10.0;
  }

  g_fCurrentCLK6502 = CLOCK_6502 * g_fMHz;

  // Now re-init modules that are dependent on /g_fCurrentCLK6502/
  SpkrReinitialize();
  MB_Reinitialize();
}

void EnterMessageLoop()
{
  //  MSG message;
  SDL_Event event;

  while (true) {
    bool event_was_key_F4 = false;

    if (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT && !event_was_key_F4) {
        return;
      }
      event_was_key_F4 = (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
        && event.key.keysym.sym == SDLK_F4;
      FrameDispatchMessage(&event);

      while ((g_nAppMode == MODE_RUNNING) || (g_nAppMode == MODE_STEPPING)) {
        if (SDL_PollEvent(&event)) {
          if (event.type == SDL_QUIT && !event_was_key_F4) {
            return;
          }
          event_was_key_F4 = (event.type==SDL_KEYDOWN || event.type==SDL_KEYUP)
            && event.key.keysym.sym == SDLK_F4;
          FrameDispatchMessage(&event);
        } else if (g_nAppMode == MODE_STEPPING) {
          DebugContinueStepping();
        } else {
          ContinueExecution();
          if (g_nAppMode != MODE_DEBUG) {
            if (joyexitenable) {
              CheckJoyExit();
              if (joyquitevent) {
                return;
              }
            }
            if ((g_bFullSpeed)||(IsDebugSteppingAtFullSpeed())) {
              ContinueExecution();
            }
          }
        }
      }
    } else {
      if (g_nAppMode == MODE_DEBUG) {
        DebuggerUpdate();
      } else if (g_nAppMode == MODE_LOGO || g_nAppMode == MODE_PAUSED) {
        SDL_Delay(100); // Stop process hogging CPU
      }
    }
  }
}

int DoDiskInsert(int nDrive, LPSTR szFileName)
{
  return DiskInsert(nDrive, szFileName, 0, 0);
}

bool ValidateDirectory(char *dir)
{
  bool ret = false;
  if (dir && *dir) {
    struct stat st;
    if (stat("/tmp", &st) == 0) {
      if ((st.st_mode & S_IFDIR) != 0) {
        ret = true;
      }
    }
  }
  printf("%s is dir? %d\n", dir ? dir : "undefined", ret);
  return ret;
}

void SetDiskImageDirectory(char *regKey, int driveNumber)
{
  char *szHDFilename = NULL;
  if (RegLoadString(TEXT("Configuration"), TEXT(regKey), 1, &szHDFilename, MAX_PATH)) {
    if (!ValidateDirectory(szHDFilename)) {
      free(szHDFilename);
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
  SDL_Event user_ev;
  user_ev.type = SDL_USEREVENT;
  user_ev.user.code = 1;  //restart
  SDL_PushEvent(&user_ev);
}

// Let us load main configuration from config file.  Y_Y  --bb
void LoadConfiguration()
{
  if (registry) {
    unsigned int dwComputerType = g_Apple2Type;
    LOAD(TEXT("Computer Emulation"), &dwComputerType);

    switch (dwComputerType) {
      case 0:
        g_Apple2Type = A2TYPE_APPLE2;
        break;
      case 1:
        g_Apple2Type = A2TYPE_APPLE2PLUS;
        break;
      case 2:
        g_Apple2Type = A2TYPE_APPLE2E;
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
  printf("Selected machine type: %s\n", g_pAppTitle);

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

  if (joytype[0] == 1) {
    printf("Joystick 1 Index # = %i, Name = %s \nButton 1 = %i, Button 2 = %i \nAxis 0 = %i,Axis 1 = %i\n", joy1index,
           SDL_JoystickName(joy1index), joy1button1, joy1button2, joy1axis0, joy1axis1);
  }
  if (joytype[1] == 1) {
    printf("Joystick 2 Index # = %i, Name = %s \nButton 1 = %i \nAxis 0 = %i,Axis 1 = %i\n", joy2index,
           SDL_JoystickName(joy2index), joy2button1, joy2axis0, joy2axis1);
  }

  // default: use keyboard language according to environment
  {
    const char* pEnvLanguage = getenv("LANG");
    if (pEnvLanguage)
    {
      if (0 == strncmp(pEnvLanguage, "de", 2))
        g_KeyboardLanguage = German_DE;
      else
      if (0 == strncmp(pEnvLanguage, "fr", 2))
        g_KeyboardLanguage = French_FR;
      else
      if (0 == strncmp(pEnvLanguage, "en_UK", 5))
        g_KeyboardLanguage = English_UK;
    }
  }

  // check if configuration file contains specific keyboard language
  if (registry)
  {
    unsigned int Language = 0;
    if (LOAD(TEXT(REGVALUE_KEYB_TYPE), &Language)) {
      switch(Language)
      {
        case 1:
          g_KeyboardLanguage = English_US;
          break;
        case 2:
          g_KeyboardLanguage = English_UK;
          break;
        case 3:
          g_KeyboardLanguage = French_FR;
          break;
        case 4:
          g_KeyboardLanguage = German_DE;
          break;
      }
    }

    printf("Selected keyboard type: ");
    switch(g_KeyboardLanguage)
    {
      case English_UK:
        printf("UK\n");
        break;
      case French_FR:
        printf("French\n");
        break;
      case German_DE:
        printf("German\n");
        break;
      default:
        printf("US\n");
        break;
    }

    unsigned int ToggleSwitch = 0;
    if (LOAD(TEXT(REGVALUE_KEYB_CHARSET_SWITCH), &ToggleSwitch)) {
      // select initial value of the keyboard character set toggle switch
      g_KeyboardRockerSwitch = (ToggleSwitch>=1);
      printf("Keyboard rocker switch: %s\n", (g_KeyboardRockerSwitch) ? "local charset" : "standard/US charset");
    }
  }

  if (registry) {
    LOAD(TEXT("Sound Emulation"), &soundtype);
  }
  unsigned int dwSerialPort;
  if (registry) {
    LOAD(TEXT("Serial Port"), &dwSerialPort);
  }
  sg_SSC.SetSerialPort(dwSerialPort);

  if (registry) {
    LOAD(TEXT("Emulation Speed"), &g_dwSpeed);
    LOAD(TEXT("Enhance Disk Speed"), (unsigned int * ) & enhancedisk);
    LOAD(TEXT("Video Emulation"), &g_videotype);
    LOAD(TEXT("Singlethreaded"), &g_singlethreaded);
  }

  unsigned int dwTmp = 0;  // temp var

  if (registry) {
    LOAD(TEXT("Fullscreen"), &dwTmp);  // load fullscreen flag
  }
  fullscreen = (bool) dwTmp;
  dwTmp = 1;
  if (registry) {
    LOAD(TEXT(REGVALUE_SHOW_LEDS), &dwTmp);  // load Show Leds flag
  }
  g_ShowLeds = (bool) dwTmp;

  SetCurrentCLK6502();  // set up real speed

  if (registry) {
    if (LOAD(TEXT(REGVALUE_MOUSE_IN_SLOT4), &dwTmp)) {
      g_uMouseInSlot4 = dwTmp;
    }
  }
  g_Slot4 = g_uMouseInSlot4 ? CT_MouseInterface : CT_Mockingboard;

  if (registry) {
    if (LOAD(TEXT(REGVALUE_SOUNDCARD_TYPE), &dwTmp)) {
      MB_SetSoundcardType((eSOUNDCARDTYPE) dwTmp);
    }
  }

  if (registry) {
    if (LOAD(TEXT(REGVALUE_SAVE_STATE_ON_EXIT), &dwTmp)) {
      g_bSaveStateOnExit = (dwTmp != 0);
    }
  }

  if (registry) {
    if (LOAD(TEXT(REGVALUE_PRINTER_APPEND), &dwTmp)) {
      g_bPrinterAppend = dwTmp != 0;
    }
  }

  if (registry) {
    if (LOAD(TEXT(REGVALUE_HDD_ENABLED), &dwTmp)) {
      hddenabled = (bool) dwTmp;
    }
  }

  if (registry) {
    if (LOAD(TEXT(REGVALUE_CLOCK_SLOT), &clockslot)) {
      if (clockslot < 1 || clockslot > 7)
        clockslot = 0;
    }
  }

  char *szHDFilename = NULL;

  if (registry) {
    if (RegLoadString(TEXT("Configuration"), TEXT("Monochrome Color"), 1, &szHDFilename, 10)) {
      if (!sscanf(szHDFilename, "#%X", &monochrome)) {
        monochrome = 0xC0C0C0;
      }
      free(szHDFilename);
      szHDFilename = NULL;
    }
  }

  dwTmp = 0;
  if (registry) {
    LOAD(TEXT("Boot at Startup"), &dwTmp);
  }

  if (dwTmp) {
    // autostart
    setAutoBoot();
  }

  dwTmp = 0;
  if (registry) {
    LOAD(TEXT("Slot 6 Autoload"), &dwTmp);  // load autoinsert for Slot 6 flag
  }
  if (dwTmp) {
    // Load floppy disk images and insert it automatically in slot 6 drive 1 and 2
    static char szDiskImage1[] = REGVALUE_DISK_IMAGE1;
    SetDiskImageDirectory(szDiskImage1, 0);
    SetDiskImageDirectory(szDiskImage1, 1);
  } else {
    Asset_InsertMasterDisk();
  }

  // Load hard disk images and insert it automatically in slot 7
  if (registry) {
    if (RegLoadString(TEXT("Configuration"), TEXT(REGVALUE_HDD_IMAGE1), 1, &szHDFilename, MAX_PATH)) {
      HD_InsertDisk2(0, szHDFilename);
      free(szHDFilename);
      szHDFilename = NULL;
    }
  }

  if (registry) {
    if (RegLoadString(TEXT("Configuration"), TEXT(REGVALUE_HDD_IMAGE2), 1, &szHDFilename, MAX_PATH)) {
      HD_InsertDisk2(1, szHDFilename);
      free(szHDFilename);
      szHDFilename = NULL;
    }
  }

  // file name for Parallel Printer
  if (registry) {
    if (RegLoadString(TEXT("Configuration"), TEXT(REGVALUE_PPRINTER_FILENAME), 1, &szHDFilename, MAX_PATH)) {
      if (strlen(szHDFilename) > 1) {
        strncpy(g_sParallelPrinterFile, szHDFilename, MAX_PATH);
      }
      free(szHDFilename);
      szHDFilename = NULL;
    }
  }

  if (registry) {
    if (RegLoadValue(TEXT("Configuration"), TEXT(REGVALUE_PRINTER_IDLE_LIMIT), 1, &dwTmp)) {
      Printer_SetIdleLimit(dwTmp);
    }
  }

  char *szFilename = NULL;
  double scrFactor = 0.0;
  // Define screen sizes
  if (registry) {
    if (RegLoadString(TEXT("Configuration"), TEXT("Screen factor"), 1, &szFilename, 16)) {
      // fix: prevent resolution change, it usually gives graphic problems with the dispmanx driver
      if (strncmp(videoDriverName, "dispmanx", 8) != 0) {
        scrFactor = atof(szFilename);
      }
      if (scrFactor > 0.1) {
        g_ScreenWidth = (unsigned int)(g_ScreenWidth * scrFactor);
        g_ScreenHeight = (unsigned int)(g_ScreenHeight * scrFactor);
      }
      free(szFilename);
      szFilename = NULL;
    }
  }

  if (registry) {
    if (scrFactor <= 0.1) {
      // Try to set Screen Width & Height directly
      dwTmp = 0;
      LOAD(TEXT("Screen Width"), &dwTmp);
      if (dwTmp > 0) {
        g_ScreenWidth = dwTmp;
      }
      dwTmp = 0;
      LOAD(TEXT("Screen Height"), &dwTmp);
      if (dwTmp > 0) {
        g_ScreenHeight = dwTmp;
      }

      // validate resolutions validate for the dispmanx driver.
      if (strncmp(videoDriverName, "dispmanx", 8) == 0) {
        if (!((g_ScreenWidth == 1920 && g_ScreenHeight == 1080) ||
             (g_ScreenWidth == 1280 && g_ScreenHeight ==  720) ||
             (g_ScreenWidth ==  800 && g_ScreenHeight ==  600))) {

          // default
          g_ScreenWidth  = 640;
          g_ScreenHeight = 480;
        }
      }

    }
  }

  if (registry) {
    if (RegLoadString(TEXT("Configuration"), TEXT(REGVALUE_SAVESTATE_FILENAME), 1, &szFilename, MAX_PATH)) {
      Snapshot_SetFilename(szFilename);  // If not in Registry than default will be used
      free(szFilename);
      szFilename = NULL;
    }
  }

  // Current/Starting Dir is the "root" of where the user keeps his disk images
  if (registry) {
    RegLoadString(TEXT("Preferences"), REGVALUE_PREF_START_DIR, 1, &szFilename, MAX_PATH);
  }

  if (szFilename) {
    strcpy(g_sCurrentDir, szFilename);
    free(szFilename);
    szFilename = NULL;
  }
  if (strlen(g_sCurrentDir) == 0 || g_sCurrentDir[0] != '/') {
    char *tmp = getenv("HOME"); /* we don't have HOME?  ^_^  0_0  $_$  */
    if (tmp == NULL) {
      strcpy(g_sCurrentDir, "/");  //begin from the root, then
    } else {
      strcpy(g_sCurrentDir, tmp);
    }
  }

  // Load starting directory for HDV (Apple][ HDD) images
  if (registry) {
    RegLoadString(TEXT("Preferences"), REGVALUE_PREF_HDD_START_DIR, 1, &szFilename, MAX_PATH);
  }

  if (szFilename) {
    strcpy(g_sHDDDir, szFilename);
    free(szFilename);
    szFilename = NULL;
  }

  if (strlen(g_sHDDDir) == 0 || g_sHDDDir[0] != '/') {
    char *tmp = getenv("HOME"); /* we don't have HOME?  ^_^  0_0  $_$  */
    if (tmp == NULL) {
      strcpy(g_sHDDDir, "/");  //begin from the root, then
    } else {
      strcpy(g_sHDDDir, tmp);
    }
  }

  // Load starting directory for saving current states
  if (registry) {
    RegLoadString(TEXT("Preferences"), REGVALUE_PREF_SAVESTATE_DIR, 1, &szFilename, MAX_PATH);
  }
  if (szFilename) {
    strcpy(g_sSaveStateDir, szFilename);
    free(szFilename);
    szFilename = NULL;
  }
  if (strlen(g_sSaveStateDir) == 0 || g_sSaveStateDir[0] != '/') {
    char *tmp = getenv("HOME"); /* we don't have HOME?  ^_^  0_0  $_$  */
    if (tmp == NULL) {
      strcpy(g_sSaveStateDir, "/");  //begin from the root, then
    } else {
      strcpy(g_sSaveStateDir, tmp);
    }
  }

  // Read and fill FTP variables - server, local dir, user name and password
  if (registry) {
    RegLoadString(TEXT("Preferences"), REGVALUE_FTP_DIR, 1, &szFilename, MAX_PATH);
  }

  if (szFilename) {
    strcpy(g_sFTPServer, szFilename);
    free(szFilename);
    szFilename = NULL;
  }

  if (registry) {
    RegLoadString(TEXT("Preferences"), REGVALUE_FTP_HDD_DIR, 1, &szFilename, MAX_PATH);
  }

  if (szFilename) {
    strcpy(g_sFTPServerHDD, szFilename);
    free(szFilename);
    szFilename = NULL;
  }

  if (registry) {
    RegLoadString(TEXT("Preferences"), REGVALUE_FTP_LOCAL_DIR, 1, &szFilename, MAX_PATH);
  }

  if (szFilename) {
    strcpy(g_sFTPLocalDir, szFilename);
    free(szFilename);
    szFilename = NULL;
  }

  if (registry) {
    RegLoadString(TEXT("Preferences"), REGVALUE_FTP_USERPASS, 1, &szFilename, 512);
  }

  if (szFilename) {
    strcpy(g_sFTPUserPass, szFilename);
    free(szFilename);
    szFilename = NULL;
  }
  // Print some debug strings
  printf("Ready login = %s\n", g_sFTPUserPass);
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

  // Default values should be set, but some other globals that depend on them may not be.
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
    RegConfPath(userSpecifiedFilename);
    LoadConfiguration();
    return;
  }

  char *envvar = NULL;

  // Try known system configuration paths.
  envvar = getenv("XDG_CONFIG_DIRS");

  std::string xdgConfigDirs = envvar ? envvar : "";
  if (xdgConfigDirs.length() == 0) {
    xdgConfigDirs = "/etc/xdg";
  }

  std::vector <std::string> sysConfigDirs(split(xdgConfigDirs, ":"));

  // Support the old /etc/linapple/linapple.conf location.
  sysConfigDirs.push_back("/etc");

  for (std::vector<std::string>::reverse_iterator it = sysConfigDirs.rbegin(); it != sysConfigDirs.rend(); it++) {
    std::string config = *it + "/linapple/linapple.conf";
    registry = fopen(config.c_str(), "r");
    if (!registry) {
      continue;
    }
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

  std::vector <std::string> configFiles;
  configFiles.push_back(xdgConfigDirs + "/linapple/linapple.conf");
  if (home) {
    // Suppport old locations under HOME.
    configFiles.push_back(std::string(home) + "/linapple/linapple.conf");
    configFiles.push_back(std::string(home) + "/.linapple/linapple.conf");
  }
  configFiles.push_back(xdgConfigHome + "/linapple/linapple.conf");

  std::string lastSuccessfulUserConfig;
  for (std::vector<std::string>::reverse_iterator it = configFiles.rbegin(); it != configFiles.rend(); it++) {
    registry = fopen((*it).c_str(), "r");
    if (!registry) {
      continue;
    }
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

    RegConfPath(lastSuccessfulUserConfig.c_str());
    registry = fopen(lastSuccessfulUserConfig.c_str(), "r+");
    return;
  }

  if (xdgConfigHome.length() == 0) {
    std::cerr << "WARNING!"
        << " Neither XDG_CONFIG_HOME nor HOME is set and no user config"
        << " files were found."
        << " This can lead to unexpected behavior, even program crashes."
        << std::endl;
    return;
  }

  std::string userDir(home);
  mkdir(xdgConfigHome.c_str(), 0700);
  mkdir((xdgConfigHome + "/linapple").c_str(), 0700);

  RegConfPath((xdgConfigHome + "/linapple/linapple.conf").c_str());
  registry = fopen((xdgConfigHome + "/linapple/linapple.conf").c_str(), "w+");
}

void RegisterExtensions()
{
  // TO DO: register extensions for KDE or GNOME desktops?? Do not know, if it is sane idea. He-he. --bb
}

void PrintHelp()
{
  printf("usage: linapple [options]\n"
         "\n"
         "LinApple is an emulator for Apple ][, Apple ][+, Apple //e, and Enhanced Apple //e computers.\n"
         "\n"
         "  -h|--help      show this help message\n"
         "  --conf <file>  use <file> instead of any default config files\n"
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

  XInitThreads();

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

  if (bLog) {
    LogInitialize();
  }
  if (!Asset_Init()) {
    return 1;
  }

  #if DBG_CALC_FREQ
  g_nPerfFreq = 1000;//milliseconds?
  if(g_fh) fprintf(g_fh, "Performance frequency = %d\n",g_nPerfFreq);
  #endif

  // Initialize COM
  // . NB. DSInit() is done when g_hFrameWindow is created (WM_CREATE)

  if (InitSDL()) {
    return 1;
  } // init SDL subsystems, set icon

  // CURL routines
  curl_global_init(CURL_GLOBAL_DEFAULT);
  g_curl = curl_easy_init();
  if (!g_curl) {
    printf("Could not initialize CURL easy interface");
    return 1;
  }
  /* Set user name and password to access FTP server */
  curl_easy_setopt(g_curl, CURLOPT_USERPWD, g_sFTPUserPass);

  // Do one-time initialization
  MemPreInitialize();    // Call before any of the slot devices are initialized
  ImageInitialize();
  DiskInitialize();
  CreateColorMixMap();  // For tv emulation g_nAppMode

#ifdef VERSIONSTRING
  printf("LinApple %s\n", VERSIONSTRING);
#else
  printf("LinApple\n");
#endif

  SDL_VideoDriverName(&videoDriverName[0], 100);
  printf("Video driver = %s\n", videoDriverName);

  do {
    // Do initialization that must be repeated for a restart
    restart = 0;
    g_nAppMode = MODE_LOGO;
    fullscreen = false;

    // Start with default configuration, which we will override if command line options were specified
    LoadAllConfigurations(szConfigurationFile);

    // Overwrite configuration file's set fullscreen option, if one was specified on the command line
    if (bSetFullScreen) {
      fullscreen = bSetFullScreen;
    }

    // This part of the code inserts disks if any were specified on the command line, overwriting the
    // configuration settings.
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

    if (!DSInit()) {
      soundtype = SOUND_NONE;    // Direct Sound and Stuff
    }

    MB_Initialize();  // Mocking board
    SpkrInitialize();  // Speakers - of Apple][ ...grrrrrrrrrrr, I love them!--bb
    JoyInitialize();
    MemInitialize();
    HD_SetEnabled(hddenabled);
    if (clockslot) {
      Clock_Insert(clockslot);
    }
    VideoInitialize();
    DebugInitialize();
    Snapshot_Startup();    // Do this after everything has been init'ed

    if (szSnapshotFile) {
      Snapshot_SetFilename(szSnapshotFile);
      LOG("[main ] using state file '%s'\n", Snapshot_GetFilename());
      Snapshot_LoadState();
    }

    //Automatically boot from disk if specified on the command line
    if (bBoot) {
      setAutoBoot();
    }

    JoyReset();
    SetUsingCursor(0);

    // trying fullscreen
    if (!fullscreen) {
      SetNormalMode();
    } else {
      SetFullScreenMode();
    }

    DrawFrameWindow();  // we do not have WM_PAINT?

    // ENTER THE MAIN MESSAGE LOOP
    if (bBenchMark) {
      VideoBenchmark(); // start VideoBenchmark and exit
    } else {
      EnterMessageLoop();  // else we just start game
    }

    Snapshot_Shutdown();
    DebugDestroy();
    if (!restart) {
      DiskDestroy();
      ImageDestroy();
      HD_Cleanup();
    }
    PrintDestroy();
    sg_SSC.CommDestroy();
    CpuDestroy();
    SpkrDestroy();
    VideoDestroy();
    MemDestroy();
    MB_Destroy();
    MB_Reset();
    sg_Mouse.Uninitialize(); // Maybe restarting due to switching slot-4 card from mouse to MB
    JoyShutDown();  // close opened (if any) joysticks
  } while (restart);

  // Release COM
  DSUninit();
  SysClk_UninitTimer();

  RiffFinishWriteFile();
  if (registry != NULL) {
    fclose(registry); // close conf file (linapple.conf by default)
  }

  SDL_Quit();
  curl_easy_cleanup(g_curl);
  curl_global_cleanup();
  Asset_Quit();
  LogDestroy();
  printf("Linapple: successfully exited!\n");
  return 0;
}

