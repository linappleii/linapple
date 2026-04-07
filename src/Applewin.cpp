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

#include "stdafx.h"
#include <cassert>
#include <string>
#include <vector>
#include <X11/Xlib.h>

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

#include "SerialComms.h"
#include "SerialCommsFrontend.h"
#include "PrinterFrontend.h"
#include "asset.h"
#include "Util_Path.h"
#include "JoystickFrontend.h"
#include "Debug.h"
#include "Debugger_Cmd_Output.h"

static unsigned int emulmsec_frac = 0;
static bool g_bBudgetVideo = false;
static bool g_uMouseInSlot4 = false;

// Default screen sizes
static double g_fMHz = 1.0;

static unsigned int g_uModeStepping_Cycles = 0;
static bool g_uModeStepping_LastGetKey_ScrollLock = false;

// SDL Audio Stream for Frontend
bool g_bDSAvailable = false;
SDL_AudioStream *g_audioStream = NULL;
static double g_fClksPerSpkrSample = 0;
static bool g_bLastSpkrState = false;
static double g_nextSampleCycle = 0;
static int16_t g_monoBuffer[4096];
static int16_t g_stereoBuffer[8192];

static void SDLCALL sdl3AudioCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
  (void)userdata;
  (void)total_amount;
  if (additional_amount <= 0) return;

  int16_t *temp_buf = (int16_t *)SDL_malloc(additional_amount);
  if (!temp_buf) return;

  int num_samples = additional_amount / (sizeof(int16_t) * 2); // stereo
  SoundCore_GetSamples(temp_buf, num_samples * 2);

  SDL_PutAudioStreamData(stream, temp_buf, additional_amount);
  SDL_free(temp_buf);
}

bool DSInit() {
  if (g_audioStream) return true;

  SDL_AudioSpec desired;
  desired.freq = SPKR_SAMPLE_RATE;
  desired.channels = 2;
  desired.format = SDL_AUDIO_S16;

  g_audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, sdl3AudioCallback, NULL);
  if (g_audioStream == NULL) {
    printf("Unable to open SDL audio: %s\n", SDL_GetError());
    return false;
  }

  SoundCore_Initialize();
  SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(g_audioStream));
  g_bDSAvailable = true;
  return true;
}

void DSUninit() {
  if (g_audioStream) {
    SDL_DestroyAudioStream(g_audioStream);
    g_audioStream = NULL;
  }
  SoundCore_Destroy();
  g_bDSAvailable = false;
}

void SoundCore_SetFade(int how) {
  if (g_audioStream) {
    if (how == FADE_OUT) {
      SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(g_audioStream));
    } else {
      SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(g_audioStream));
    }
  }
}

void Frontend_UpdateSpeaker() {
  if (soundtype == SOUND_NONE || g_bFullSpeed) {
    SpkrGetEvents(NULL, 0);
    return;
  }

  if (g_fClksPerSpkrSample == 0) {
    g_fClksPerSpkrSample = g_fCurrentCLK6502 / (double)SPKR_SAMPLE_RATE;
  }

  if (g_nextSampleCycle == 0) {
    g_nextSampleCycle = (double)g_nCumulativeCycles;
    g_bLastSpkrState = SpkrGetCurrentState();
  }

  SpkrEvent events[1024];
  int num_events = SpkrGetEvents(events, 1024);
  int event_idx = 0;
  int samples_to_gen = 0;

  while (g_nextSampleCycle <= (double)g_nCumulativeCycles && samples_to_gen < 4096) {
    while (event_idx < num_events && (double)events[event_idx].cycle <= g_nextSampleCycle) {
      g_bLastSpkrState = events[event_idx].state;
      event_idx++;
    }
    g_monoBuffer[samples_to_gen++] = g_bLastSpkrState ? 0x2000 : 0x0000;
    g_nextSampleCycle += g_fClksPerSpkrSample;
  }

  if (samples_to_gen > 0) {
    for (int i = 0; i < samples_to_gen; ++i) {
      g_stereoBuffer[i * 2] = g_stereoBuffer[i * 2 + 1] = g_monoBuffer[i];
    }
    DSUploadBuffer(g_stereoBuffer, samples_to_gen * 2);
  }
}

void ContinueExecution()
{
  const double fUsecPerSec = 1.e6;
  const unsigned int nExecutionPeriodUsec = 1000;    // 1.0ms
  const double fExecutionPeriodClks = g_fCurrentCLK6502 * ((double) nExecutionPeriodUsec / fUsecPerSec);

  bool bScrollLock_FullSpeed = g_bScrollLock_FullSpeed;

  g_bFullSpeed = ((g_state.dwSpeed == SPEED_MAX) || bScrollLock_FullSpeed || IsDebugSteppingAtFullSpeed() ||
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
  const uint32_t uCyclesToExecuteWithFeedback = (nCyclesWithFeedback >= 0) ? nCyclesWithFeedback
                                       : 0;

  const uint32_t uCyclesToExecute = (g_state.mode == MODE_RUNNING)   ? uCyclesToExecuteWithFeedback
                          /* MODE_STEPPING */ : 0;

  uint32_t uActualCyclesExecuted = CpuExecute(uCyclesToExecute);
  g_dwCyclesThisFrame += uActualCyclesExecuted;

  cyclenum = uActualCyclesExecuted;

  DiskUpdatePosition(uActualCyclesExecuted);
  JoyUpdatePosition();
  VideoUpdateVbl(g_dwCyclesThisFrame);

  bool bModeStepping_WaitTimer = false;
  if (g_state.mode == MODE_STEPPING && !IsDebugSteppingAtFullSpeed())
  {
    g_uModeStepping_Cycles += uActualCyclesExecuted;
    if (g_uModeStepping_Cycles >= uCyclesToExecuteWithFeedback)
    {
      g_uModeStepping_Cycles -= uCyclesToExecuteWithFeedback;
      bModeStepping_WaitTimer = true;
    }
  }

  // For MODE_STEPPING: do this speaker update periodically
  // - Otherwise kills performance due to sound-buffer lock/unlock for every 6502 opcode!
  if (g_state.mode == MODE_RUNNING || bModeStepping_WaitTimer)
    Frontend_UpdateSpeaker();

  SSCFrontend_Update(&sg_SSC, cyclenum);
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
    VideoCheckPage(false);
  }
  bool screenupdated = VideoHasRefreshed();
  screenupdated |= (!g_singlethreaded);
  bool systemidle = false;

  if (g_dwCyclesThisFrame >= g_state.dwClksPerFrame) {
    g_dwCyclesThisFrame -= g_state.dwClksPerFrame;

    if (g_state.mode != MODE_LOGO) {
      VideoUpdateFlash();

      static bool anyupdates = false;
      static bool lastupdates[2] = {false, false};

      anyupdates |= screenupdated;
      bool update_clause = ((!anyupdates) && (!lastupdates[0]) && (!lastupdates[1])) || (!g_singlethreaded);
      if (update_clause && VideoApparentlyDirty()) {
        VideoCheckPage(true);
        static unsigned int lasttime = 0;
        unsigned int currtime = SDL_GetTicks();
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
        screenupdated = true;
      }

      lastupdates[1] = lastupdates[0];
      lastupdates[0] = anyupdates;
      anyupdates = false;
    }
    MB_EndOfVideoFrame();
  }

  if ((g_state.mode == MODE_RUNNING && !g_bFullSpeed) || bModeStepping_WaitTimer)
  {
    SysClk_WaitTimer();
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

void SetBudgetVideo(bool budgetVideo)
{
  g_bBudgetVideo = budgetVideo;
}

bool GetBudgetVideo()
{
  return g_bBudgetVideo;
}

void SetCurrentCLK6502()
{
  static unsigned int dwPrevSpeed = (unsigned int) - 1;

  if (dwPrevSpeed == g_state.dwSpeed) {
    return;
  }

  dwPrevSpeed = g_state.dwSpeed;

  // SPEED_MIN    =  0 = 0.50 MHz
  // SPEED_NORMAL = 10 = 1.00 MHz
  //                20 = 2.00 MHz
  // SPEED_MAX-1  = 39 = 3.90 MHz
  // SPEED_MAX    = 40 = ???? MHz (run full-speed, /g_fCurrentCLK6502/ is ignored)

  if (g_state.dwSpeed < SPEED_NORMAL) {
    g_fMHz = 0.5 + (double) g_state.dwSpeed * 0.05;
  } else {
    g_fMHz = (double) g_state.dwSpeed / 10.0;
  }

  g_fCurrentCLK6502 = CLOCK_6502 * g_fMHz;

  // Now re-init modules that are dependent on /g_fCurrentCLK6502/
  SpkrReinitialize();
  MB_Reinitialize();
}

void Sys_Input()
{
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) {
      g_state.mode = MODE_EXIT;
      return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
      SDL_Keymod mod = SDL_GetModState();
      KeybSetModifiers((mod & SDL_KMOD_SHIFT) != 0,
                       (mod & SDL_KMOD_CTRL) != 0,
                       (mod & SDL_KMOD_ALT) != 0);
    }

    if (g_state.mode == MODE_DISK_CHOOSE) {
      DiskChoose_Tick(&event);
    } else {
      FrameDispatchMessage(&event);
    }
  }
}

void Sys_Think()
{
  if (g_state.mode == MODE_DISK_CHOOSE) {
    SDL_Delay(10);
    return;
  }

  JoyFrontend_Update();

  if (g_state.mode == MODE_DEBUG) {
    DebuggerUpdate();
    SDL_Delay(10);
  } else if (g_state.mode == MODE_LOGO || g_state.mode == MODE_PAUSED) {
    SDL_Delay(10);
  } else if (g_state.mode == MODE_RUNNING || g_state.mode == MODE_STEPPING) {
    if (g_state.mode == MODE_STEPPING) {
      DebugContinueStepping();
    } else {
      ContinueExecution();
      if (g_state.mode != MODE_DEBUG) {
        if (joyexitenable) {
          JoyFrontend_CheckExit();
          if (joyquitevent) {
            g_state.mode = MODE_EXIT;
            return;
          }
        }
        if ((g_bFullSpeed) || (IsDebugSteppingAtFullSpeed())) {
          ContinueExecution();
        }
      }
    }
  } else {
    SDL_Delay(1);
  }
}
void Sys_Draw()
{
  static uint64_t last_flash_time = 0;
  uint64_t current_time = SDL_GetTicks();

  if (current_time - last_flash_time >= 16) {
    VideoUpdateFlash();
    last_flash_time = current_time;
  }

  if (g_state.mode == MODE_DISK_CHOOSE) {
    DiskChoose_Draw();
    return;
  }

  if (g_state.mode == MODE_DEBUG || g_state.mode == MODE_LOGO || g_state.mode == MODE_PAUSED) {
    DrawAppleContent();
    DrawFrameWindow();
  } else if (g_state.mode == MODE_RUNNING || g_state.mode == MODE_STEPPING) {
    if (VideoHasRefreshed()) {
      DrawFrameWindow();
    }
  }
}
void EnterMessageLoop()
{
  static bool bScriptRan = false;

  while (g_state.mode != MODE_EXIT && !g_state.restart) {
    Sys_Input();
    if (g_state.mode == MODE_EXIT) break;
    Sys_Think();
    Sys_Draw();

    if (g_state.sDebuggerScript[0] && !bScriptRan && CanDrawDebugger()) {
      DebuggerRunScript(g_state.sDebuggerScript);
      bScriptRan = true;
    }
  }
}

bool ValidateDirectory(const char *dir)
{
  bool ret = false;
  if (dir && *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) {
      if ((st.st_mode & S_IFDIR) != 0) {
        ret = true;
      }
    }
  }
  printf(" ---> %s is dir? %d\n", dir ? dir : "(null)", ret);
  return ret;
}

void SetDiskImageDirectory(char *regKey, int driveNumber)
{
  std::string sHDFilename = Configuration::Instance().GetString("Configuration", regKey);
  if (!sHDFilename.empty()) {
    if (!ValidateDirectory(sHDFilename.c_str())) {
      Configuration::Instance().SetString("Configuration", regKey, "/");
      Configuration::Instance().Save();
      sHDFilename = "/";
    }
    DiskInsert(driveNumber, sHDFilename.c_str(), false, false);
  }
}

//Sets the emulator to automatically boot, rather than load the flash screen on startup
void setAutoBoot()
{
  SDL_Event user_ev;
  user_ev.type = SDL_EVENT_USER;
  user_ev.user.code = 1;  //restart
  SDL_PushEvent(&user_ev);
}

static const char* GetJoystickNameByIndex(int index) {
  int count;
  SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
  const char* name = "Unknown";
  if (joysticks && index >= 0 && index < count) {
    name = SDL_GetJoystickNameForID(joysticks[index]);
  }
  SDL_free(joysticks);
  return name;
}

void SetVideoStandard(bool bNTSC)
{
  g_state.bVideoScannerNTSC = bNTSC;
  if (bNTSC) {
    g_state.dwClksPerFrame = 65 * 262;
  } else {
    g_state.dwClksPerFrame = 65 * 312;
  }
}

// Load configuration from config file
void LoadConfiguration()
{
    unsigned int dwComputerType = g_Apple2Type;
    LOAD("Computer Emulation", &dwComputerType);

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
        g_Apple2Type = A2TYPE_APPLE2EENHANCED;
        break;
    }

  switch (g_Apple2Type) {
    case A2TYPE_APPLE2:
      g_pAppTitle = GetTitleApple2();
      break;
    case A2TYPE_APPLE2PLUS:
      g_pAppTitle = GetTitleApple2Plus();
      break;
    case A2TYPE_APPLE2E:
      g_pAppTitle = GetTitleApple2e();
      break;
    case A2TYPE_APPLE2EENHANCED:
      g_pAppTitle = GetTitleApple2eEnhanced();
      break;
    default:
      break;
  }

  Logger::Info("Selected machine type: %s\n", g_pAppTitle);

    LOAD("Joystick 0", (unsigned int*)&joytype[0]);
    LOAD("Joystick 1", (unsigned int*)&joytype[1]);
    LOAD("Joy0Index", (unsigned int*)&joy1index);
    LOAD("Joy1Index", (unsigned int*)&joy2index);

    LOAD("Joy0Button1", (unsigned int*)&joy1button1);
    LOAD("Joy0Button2", (unsigned int*)&joy1button2);
    LOAD("Joy1Button1", (unsigned int*)&joy2button1);

    LOAD("Joy0Axis0", (unsigned int*)&joy1axis0);
    LOAD("Joy0Axis1", (unsigned int*)&joy1axis1);
    LOAD("Joy1Axis0", (unsigned int*)&joy2axis0);
    LOAD("Joy1Axis1", (unsigned int*)&joy2axis1);
    LOAD("JoyExitEnable", (unsigned int*)&joyexitenable);
    LOAD("JoyExitButton0", (unsigned int*)&joyexitbutton0);
    LOAD("JoyExitButton1", (unsigned int*)&joyexitbutton1);

  if (joytype[0] == 1) {
    Logger::Info("Joystick 1 Index # = %i, Name = %s \nButton 1 = %i, Button 2 = %i \nAxis 0 = %i,Axis 1 = %i\n", joy1index,
           GetJoystickNameByIndex(joy1index), joy1button1, joy1button2, joy1axis0, joy1axis1);
  }
  if (joytype[1] == 1) {
    Logger::Info("Joystick 2 Index # = %i, Name = %s \nButton 1 = %i \nAxis 0 = %i,Axis 1 = %i\n", joy2index,
           GetJoystickNameByIndex(joy2index), joy2button1, joy2axis0, joy2axis1);
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
  {
    unsigned int Language = 0;
    if (LOAD(REGVALUE_KEYB_TYPE, &Language)) {
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
        case 5:
          g_KeyboardLanguage = Spanish_ES;
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
      case Spanish_ES:
        printf("Spanish\n");
        break;
      default:
        printf("US\n");
        break;
    }

    unsigned int ToggleSwitch = 0;
    if (LOAD(REGVALUE_KEYB_CHARSET_SWITCH, &ToggleSwitch)) {
      g_KeyboardRockerSwitch = (ToggleSwitch>=1);
      printf("Keyboard rocker switch: %s\n", (g_KeyboardRockerSwitch) ? "local charset" : "standard/US charset");
    }
  }

  LOAD("Sound Emulation", &soundtype);
  unsigned int dwSerialPort = 0;
  LOAD("Serial Port", &dwSerialPort);
  SSCFrontend_SetSerialPort(dwSerialPort);

  LOAD("Emulation Speed", &g_state.dwSpeed);
  LOAD("Enhance Disk Speed", (unsigned int * ) & enhancedisk);
  LOAD("Video Emulation", &g_videotype);
  unsigned int videoStandard = 0;
  LOAD("Video Standard", &videoStandard);
  SetVideoStandard(videoStandard == 0);

  LOAD("Singlethreaded", (unsigned int*)&g_singlethreaded);

  unsigned int dwTmp = 0;

  LOAD("Fullscreen", &dwTmp);
  g_state.fullscreen = (bool) dwTmp;
  dwTmp = 1;
  LOAD(REGVALUE_SHOW_LEDS, &dwTmp);
  g_ShowLeds = (bool) dwTmp;

  SetCurrentCLK6502();

  if (LOAD(REGVALUE_MOUSE_IN_SLOT4, &dwTmp)) {
    g_uMouseInSlot4 = (bool)dwTmp;
  }
  g_Slot4 = g_uMouseInSlot4 ? CT_MouseInterface : CT_Mockingboard;

  if (LOAD(REGVALUE_SOUNDCARD_TYPE, &dwTmp)) {
    MB_SetSoundcardType((eSOUNDCARDTYPE) dwTmp);
  }

  if (LOAD(REGVALUE_SAVE_STATE_ON_EXIT, &dwTmp)) {
    g_bSaveStateOnExit = (dwTmp != 0);
  }

  if (LOAD(REGVALUE_PRINTER_APPEND, &dwTmp)) {
    g_bPrinterAppend = dwTmp != 0;
  }

  if (LOAD(REGVALUE_HDD_ENABLED, &dwTmp)) {
    hddenabled = (bool) dwTmp;
  }

  if (LOAD(REGVALUE_CLOCK_SLOT, &dwTmp)) {
    if (dwTmp < 1 || dwTmp > 7)
      dwTmp = 0;
    clockslot = (char)dwTmp;
  }

  std::string sHDFilename;

  sHDFilename = Configuration::Instance().GetString("Configuration", "Monochrome Color");
  if (!sHDFilename.empty()) {
    if (!sscanf(sHDFilename.c_str(), "#%X", &monochrome)) {
      monochrome = 0xC0C0C0;
    }
  }

  dwTmp = 0;
  LOAD("Boot at Startup", &dwTmp);

  if (dwTmp) {
    setAutoBoot();
  }

  dwTmp = 0;
  LOAD("Slot 6 Autoload", &dwTmp);
  if (dwTmp) {
    static char szDiskImage1[] = REGVALUE_DISK_IMAGE1;
    SetDiskImageDirectory(szDiskImage1, 0);
    SetDiskImageDirectory(szDiskImage1, 1);
  } else {
    Asset_InsertMasterDisk();
  }

  sHDFilename = Configuration::Instance().GetString("Configuration", REGVALUE_HDD_IMAGE1);
  if (!sHDFilename.empty()) {
    HD_InsertDisk2(0, sHDFilename.c_str());
  }

  sHDFilename = Configuration::Instance().GetString("Configuration", REGVALUE_HDD_IMAGE2);
  if (!sHDFilename.empty()) {
    HD_InsertDisk2(1, sHDFilename.c_str());
  }

  sHDFilename = Configuration::Instance().GetString("Configuration", REGVALUE_PPRINTER_FILENAME);
  if (sHDFilename.length() > 1) {
    Util_SafeStrCpy(g_state.sParallelPrinterFile, sHDFilename.c_str(), MAX_PATH);
  }

  Printer_SetIdleLimit(Configuration::Instance().GetInt("Configuration", REGVALUE_PRINTER_IDLE_LIMIT, 0));

  std::string sFilename;
  double scrFactor = 0.0;

  // Reset to base dimensions before applying factor
  g_state.ScreenWidth = 560;
  g_state.ScreenHeight = 384;

  sFilename = Configuration::Instance().GetString("Configuration", "Screen factor");
  if (!sFilename.empty()) {
    // fix: prevent resolution change, it usually gives graphic problems with the dispmanx driver
    if (strncmp(videoDriverName, "dispmanx", 8) != 0) {
      scrFactor = atof(sFilename.c_str());
    }
    if (scrFactor > 0.1) {
      g_state.ScreenWidth = (unsigned int)(g_state.ScreenWidth * scrFactor);
      g_state.ScreenHeight = (unsigned int)(g_state.ScreenHeight * scrFactor);
    }
  }

  if (scrFactor <= 0.1) {
    dwTmp = 0;
    LOAD("Screen Width", &dwTmp);
    if (dwTmp > 0) {
      g_state.ScreenWidth = dwTmp;
    }
    dwTmp = 0;
    LOAD("Screen Height", &dwTmp);
    if (dwTmp > 0) {
      g_state.ScreenHeight = dwTmp;
    }

    if (strncmp(videoDriverName, "dispmanx", 8) == 0) {
      if ((g_state.ScreenWidth != 1920 || g_state.ScreenHeight != 1080) &&
           (g_state.ScreenWidth != 1280 || g_state.ScreenHeight !=  720) &&
           (g_state.ScreenWidth !=  800 || g_state.ScreenHeight !=  600)) {

        g_state.ScreenWidth  = 640;
        g_state.ScreenHeight = 480;
      }
    }
  }

  sFilename = Configuration::Instance().GetString("Configuration", REGVALUE_SAVESTATE_FILENAME);
  if (!sFilename.empty()) {
    Snapshot_SetFilename(sFilename.c_str());
  }

  // Current/Starting Dir is the "root" of where the user keeps his disk images
  sFilename = Configuration::Instance().GetString("Preferences", REGVALUE_PREF_START_DIR);
  if (!sFilename.empty()) {
    Util_SafeStrCpy(g_state.sCurrentDir, sFilename.c_str(), MAX_PATH);
  }
  if (strlen(g_state.sCurrentDir) == 0 || g_state.sCurrentDir[0] != '/') {
    char *tmp = getenv("HOME");
    if (tmp == NULL) {
      strcpy(g_state.sCurrentDir, "/");
    } else {
      Util_SafeStrCpy(g_state.sCurrentDir, tmp, MAX_PATH);
    }
  }

  sFilename = Configuration::Instance().GetString("Preferences", REGVALUE_PREF_HDD_START_DIR);
  if (!sFilename.empty()) {
    Util_SafeStrCpy(g_state.sHDDDir, sFilename.c_str(), MAX_PATH);
  }

  if (strlen(g_state.sHDDDir) == 0 || g_state.sHDDDir[0] != '/') {
    char *tmp = getenv("HOME");
    if (tmp == NULL) {
      strcpy(g_state.sHDDDir, "/");
    } else {
      Util_SafeStrCpy(g_state.sHDDDir, tmp, MAX_PATH);
    }
  }

  sFilename = Configuration::Instance().GetString("Preferences", REGVALUE_PREF_SAVESTATE_DIR);
  if (!sFilename.empty()) {
    Util_SafeStrCpy(g_state.sSaveStateDir, sFilename.c_str(), MAX_PATH);
  }
  if (strlen(g_state.sSaveStateDir) == 0 || g_state.sSaveStateDir[0] != '/') {
    char *tmp = getenv("HOME");
    if (tmp == NULL) {
      strcpy(g_state.sSaveStateDir, "/");
    } else {
      Util_SafeStrCpy(g_state.sSaveStateDir, tmp, MAX_PATH);
    }
  }

  sFilename = Configuration::Instance().GetString("Preferences", REGVALUE_FTP_DIR);
  if (!sFilename.empty()) Util_SafeStrCpy(g_state.sFTPServer, sFilename.c_str(), MAX_PATH);

  sFilename = Configuration::Instance().GetString("Preferences", REGVALUE_FTP_HDD_DIR);
  if (!sFilename.empty()) Util_SafeStrCpy(g_state.sFTPServerHDD, sFilename.c_str(), MAX_PATH);

  sFilename = Configuration::Instance().GetString("Preferences", REGVALUE_FTP_LOCAL_DIR);
  if (!sFilename.empty()) Util_SafeStrCpy(g_state.sFTPLocalDir, sFilename.c_str(), MAX_PATH);

  sFilename = Configuration::Instance().GetString("Preferences", REGVALUE_FTP_USERPASS);
  if (!sFilename.empty()) Util_SafeStrCpy(g_state.sFTPUserPass, sFilename.c_str(), 512);

  Logger::Info("Ready login = %s\n", g_state.sFTPUserPass);
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
  LoadConfiguration();

  if (userSpecifiedFilename) {
    if (Configuration::Instance().Load(userSpecifiedFilename)) {
        LoadConfiguration();
        return;
    } else {
        std::cerr << "WARNING! Failed to load config file: " << userSpecifiedFilename << std::endl;
        exit(EXIT_SUCCESS);
    }
  }

  std::vector<std::string> configSearchPaths;
  configSearchPaths.push_back("./linapple.conf");
  configSearchPaths.push_back(Path::GetUserConfigDir() + "linapple.conf");
  configSearchPaths.push_back(Path::GetUserDataDir() + "linapple.conf");

  char *envvar = getenv("XDG_CONFIG_DIRS");
  std::string xdgConfigDirs = envvar ? envvar : "/etc/xdg";
  std::vector<std::string> sysConfigDirs(split(xdgConfigDirs, ":"));
  sysConfigDirs.push_back("/etc");

  for (const auto& dir : sysConfigDirs) {
      configSearchPaths.push_back(dir + "/linapple/linapple.conf");
  }

  std::string lastSuccessfulConfig;
  for (const auto& configPath : configSearchPaths) {
      if (Configuration::Instance().Load(configPath)) {
          lastSuccessfulConfig = configPath;
          LoadConfiguration();
          break;
      }
  }

  if (!lastSuccessfulConfig.empty()) {
      Configuration::Instance().SetPath(lastSuccessfulConfig);
  } else {
      std::string userPath = Path::GetUserConfigDir() + "linapple.conf";
      std::string templatePath = Path::FindDataFile("linapple.conf");

      if (!templatePath.empty()) {
          if (Path::CopyFile(templatePath, userPath)) {
              Configuration::Instance().Load(userPath);
          } else {
              Configuration::Instance().Load(templatePath);
          }
      } else {
          Configuration::Instance().LoadDefaults();
          Configuration::Instance().SetPath(userPath);
#ifdef REGISTRY_WRITEABLE
          Configuration::Instance().Save();
#endif
      }
      LoadConfiguration();
  }
}

void RegisterExtensions()
{
}

void PrintHelp() {
    printf("Usage: linapple [OPTIONS]\n\n");

    printf("Disk & State Management:\n");
    printf("  -1, --d1 <file>          Load disk image into Drive 1\n");
    printf("  -2, --d2 <file>          Load disk image into Drive 2\n");
    printf("  -s, --state <file>       Load a specific snapshot/state file\n");
    printf("  -c, --conf <file>        Use a custom configuration file\n\n");

    printf("Emulation Controls:\n");
    printf("  -b, --autoboot           Boot the system immediately on launch\n");
    printf("  -p, --pal                Set video mode to PAL (default is NTSC)\n");
    printf("  -r, --ramworks <1-128>   Set RamWorks expansion size in pages\n");
    printf("  -6, --test-6502          Emulate original Apple II+ (6502)\n");
    printf("  -C, --test-65c02         Emulate Apple IIe Enhanced (65c02)\n\n");

    printf("Debugging & Performance:\n");
    printf("  -m, --benchmark          Run in benchmark mode (uncapped speed)\n");
    printf("  -T, --test-cpu <file>    Run CPU test suite from file\n");
    printf("  -x, --debug-script <f>   Execute a debugger script on startup\n");
    printf("  -v, --verbose            Enable high-verbosity performance logging\n\n");

    printf("General:\n");
    printf("  -f, --fullscreen         Start in fullscreen mode\n");
    printf("  -l, --log                Enable general system logging\n");
    printf("  -h, --help               Display this help message and exit\n");
}

int SysInit(bool bLog)
{
  if (bLog) {
    Logger::Initialize();
  }
  if (!Asset_Init()) {
    return 1;
  }

  if (InitSDL()) {
    return 1;
  }

  curl_global_init(CURL_GLOBAL_DEFAULT);
  g_curl = curl_easy_init();
  if (!g_curl) {
    Logger::Error("Could not initialize CURL easy interface\n");
    return 1;
  }
  curl_easy_setopt(g_curl, CURLOPT_USERPWD, g_state.sFTPUserPass);

  MemPreInitialize();
  ImageInitialize();
  DiskInitialize();
  CreateColorMixMap();

#ifdef VERSIONSTRING
  Logger::Info("LinApple %s\n", VERSIONSTRING);
#else
  Logger::Info("LinApple\n");
#endif

  const char* driver = SDL_GetCurrentVideoDriver();
  if (driver) Util_SafeStrCpy(videoDriverName, driver, 100);
  Logger::Info("Video driver = %s\n", videoDriverName);

  return 0;
}

void SysShutdown()
{
  DSUninit();
  SysClk_UninitTimer();

  RiffFinishWriteFile();

  SDL_Quit();
  curl_easy_cleanup(g_curl);
  curl_global_cleanup();
  Asset_Quit();
  Logger::Destroy();
  Logger::Info("Linapple: successfully exited!\n");
}

int SessionInit(const char* szConfigurationFile, bool bSetFullScreen,
                const char* szImageName_drive1, const char* szImageName_drive2,
                const char* szSnapshotFile, bool bBoot, bool bPAL)
{
  g_state.mode = MODE_LOGO;
  if (g_state.sDebuggerScript[0]) {
    g_state.mode = MODE_DEBUG;
  }

  g_state.fullscreen = false;

  LoadAllConfigurations(szConfigurationFile);

  if (bSetFullScreen) {
    g_state.fullscreen = bSetFullScreen;
  }

  if (bPAL) {
    SetVideoStandard(false);
  }

  int nError = 0;
  if (szImageName_drive1) {
    DiskInsert(0, szImageName_drive1, false, false);
    if (nError) {
      Logger::Error("Cannot insert image %s into drive 1.\n", szImageName_drive1);
      return 1;
    }
  }
  if (szImageName_drive2) {
    DiskInsert(1, szImageName_drive2, false, false);
    if (nError) {
      Logger::Error("Cannot insert image %s into drive 2.\n", szImageName_drive2);
      return 1;
    }
  }

  FrameCreateWindow();

  if (!DSInit()) {
    soundtype = SOUND_NONE;
  }

  MB_Initialize();
  SpkrInitialize();
  JoyInitialize();
  JoyFrontend_Initialize();
  MemInitialize();
  HD_SetEnabled(hddenabled);
  if (clockslot) {
    Clock_Insert(clockslot);
  }
  VideoInitialize();
  DebugInitialize();

  Snapshot_Startup();

  if (szSnapshotFile) {
    Snapshot_SetFilename(szSnapshotFile);
    Snapshot_LoadState();
  }

  if (bBoot && !g_state.sDebuggerScript[0]) {
    setAutoBoot();
  }

  JoyReset();
  SetUsingCursor(false);

  if (!g_state.fullscreen) {
    SetNormalMode();
  } else {
    SetFullScreenMode();
  }

  DrawFrameWindow();
  return 0;
}

void SessionShutdown()
{
  Snapshot_Shutdown();
  DebugDestroy();
  if (!g_state.restart) {
    DiskDestroy();
    ImageDestroy();
    HD_Cleanup();
  }
  PrintDestroy();
  SSC_Destroy(&sg_SSC);
  CpuDestroy();
  SpkrDestroy();
  VideoDestroy();
  MemDestroy();
  MB_Destroy();
  MB_Reset();
  Mouse_Uninitialize();
  JoyShutDown();
  JoyFrontend_ShutDown();
}

void CpuTestHeadless(const char* filename) {
    if (MemInitialize() != 0) {
        Logger::Error("Failed to initialize memory\n");
        return;
    }
    CpuInitialize();

    FILE* f = fopen(filename, "rb");
    if (!f) {
        Logger::Error("Failed to open test file: %s\n", filename);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 65536) size = 65536;
    if (fread(mem, 1, size, f) != (size_t)size) {
        Logger::Error("Failed to read test file\n");
        fclose(f);
        return;
    }
    fclose(f);

    regs.pc = 0x400;
    regs.ps = 0;
    uint16_t last_pc = 0;
    uint64_t count = 0;

    while (true) {
        last_pc = regs.pc;
        CpuExecute(1000);
        count += 100;

        if (regs.pc == last_pc) {
            Logger::Info("CPU trapped at 0x%04X after %" PRIu64 " cycles\n", regs.pc, count);
            break;
        }

        if (count > 1000000000ULL) {
            Logger::Info("Test timed out at 0x%04X after %" PRIu64 " cycles\n", regs.pc, count);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
  bool bLog = false;
  bool bSetFullScreen = false;
  bool bBoot = false;
  bool bBenchMark = false;
  bool bPAL = false;
  bool bTestCpu = false;
  char* szConfigurationFile = NULL;
  char* szImageName_drive1 = NULL;
  char* szImageName_drive2 = NULL;
  char* szSnapshotFile = NULL;
  char* szTestFile = NULL;

  int opt;
  int optind = 0;
  const char *optname;
  static struct option OptionTable[] = {
    {"autoboot",     no_argument,       0, 'b'},
    {"benchmark",    no_argument,       0, 'm'},
    {"conf",         required_argument, 0, 'c'},
    {"d1",           required_argument, 0, '1'},
    {"d2",           required_argument, 0, '2'},
    {"debug-script", required_argument, 0, 'x'},
    {"help",         no_argument,       0, 'h'},
    {"pal",          no_argument,       0, 'p'},
    {"state",        required_argument, 0, 's'},
    {"test-cpu",     required_argument, 0, 'T'},
    {"test-6502",    no_argument,       0, '6'},
    {"test-65c02",   no_argument,       0, 'C'},
    {"verbose",      no_argument,       0, 'v'},
    {0, 0, 0, 0}
  };

  XInitThreads();

  while ((opt = getopt_long(argc, argv, "1:2:abc:fhlmpr:s:vx:T:6C", OptionTable, &optind)) != -1) {
    switch (opt) {
      case '1':
        szImageName_drive1 = optarg;
        break;
      case '2':
        szImageName_drive2 = optarg;
        break;
      case 's':
        szSnapshotFile = optarg;
        break;
      case 'c':
        szConfigurationFile = optarg;
        break;

      case 'b':
        bBoot = true;
        break;
      case 'm':
        bBenchMark = true;
        break;
      case 'f':
        bSetFullScreen = true;
        break;
      case 'l':
        bLog = true;
        break;
      case 'p':
        bPAL = true;
        break;
      case 'v':
        Logger::SetVerbosity(LogLevel::Perf);
        break;

      case 'x':
        Util_SafeStrCpy(g_state.sDebuggerScript, optarg, MAX_PATH);
        break;

      case 'T':
        bTestCpu = true;
        szTestFile = optarg;
        break;

      case '6':
        g_Apple2Type = A2TYPE_APPLE2PLUS;
        break;
      case 'C':
        g_Apple2Type = A2TYPE_APPLE2EENHANCED;
        break;

#ifdef RAMWORKS
      case 'r':
        g_uMaxExPages = atoi(optarg);
        if (g_uMaxExPages > 127) g_uMaxExPages = 128;
        else if (g_uMaxExPages < 1) g_uMaxExPages = 1;
        break;
#endif

      case 'h':
        PrintHelp();
        return 0;

      default:
        fprintf(stderr, "Check --help for proper usage.\n");
        return 255;
    }
  }

  if (SysInit(bLog) != 0) {
    return 1;
  }

  if (bTestCpu) {
    CpuTestHeadless(szTestFile);
    SysShutdown();
    return 0;
  }

  do {
    g_state.restart = false;

    if (SessionInit(szConfigurationFile, bSetFullScreen,
                    szImageName_drive1, szImageName_drive2,
                    szSnapshotFile, bBoot, bPAL) != 0) {
      break;
    }

    if (bBenchMark) {
      VideoBenchmark();
    } else {
      EnterMessageLoop();
    }

    SessionShutdown();
  } while (g_state.restart);

  SysShutdown();
  return 0;
}

