#include "Common.h"
#include <cassert>
#include <string>
#include <vector>
#include <ctime>
#include <sys/time.h>
#include <sys/param.h>
#include <unistd.h>
#include <curl/curl.h>
#include <cstdlib>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <cinttypes>
#include <SDL3/SDL.h>

#include "AppleWin.h"
#include "Keyboard.h"
#include "Speaker.h"
#include "Disk.h"
#include "DiskImage.h"
#include "Harddisk.h"
#include "Mockingboard.h"
#include "SoundCore.h"
#include "MouseInterface.h"
#include "Video.h"
#include "Registry.h"
#include "SaveState.h"
#include "Frame.h"
#include "Memory.h"
#include "CPU.h"
#include "Clock.h"
#include "Timer.h"
#include "SerialComms.h"
#include "SerialCommsFrontend.h"
#include "PrinterFrontend.h"
#include "asset.h"
#include "Util_Path.h"
#include "Util_Text.h"
#include "JoystickFrontend.h"
#include "Joystick.h"
#include "Debugger/Debug.h"
#include "Debugger_Cmd_Output.h"
#include "Common_Globals.h"
#include "Log.h"
#include <X11/Xlib.h>
#include "Structs.h"
#include "Riff.h"
#include "DiskChoose.h"
#include "ParallelPrinter.h"

static bool g_bBudgetVideo = false;

static uint8_t g_nRepeatKey = 0;
static uint32_t g_nRepeatDelayCycles = 0;
static bool g_bRepeating = false;

const uint32_t KEY_REPEAT_INITIAL_DELAY = 512000;
const uint32_t KEY_REPEAT_RATE = 68000;

void Linapple_KeyboardThink(uint32_t dwCycles) {
  if (g_nRepeatKey == 0) return;
  g_nRepeatDelayCycles += dwCycles;
  if (!g_bRepeating) {
    if (g_nRepeatDelayCycles >= KEY_REPEAT_INITIAL_DELAY) {
      g_bRepeating = true;
      g_nRepeatDelayCycles = 0;
      KeybPushAppleKey(g_nRepeatKey);
    }
  } else {
    if (g_nRepeatDelayCycles >= KEY_REPEAT_RATE) {
      g_nRepeatDelayCycles = 0;
      KeybPushAppleKey(g_nRepeatKey);
    }
  }
}

void Linapple_SetKeyState(uint8_t apple_code, bool bDown) {
  if (bDown) {
    if (g_nRepeatKey == apple_code) return;
    g_nRepeatKey = apple_code;
    g_nRepeatDelayCycles = 0;
    g_bRepeating = false;
    if (apple_code) KeybPushAppleKey(apple_code);
  } else {
    if (g_nRepeatKey == apple_code) {
      g_nRepeatKey = 0;
      g_bRepeating = false;
    }
  }
}

void Linapple_SetAppleKey(int apple_key, bool bDown) {
  JoySetRawButton(apple_key, bDown);
}

void Linapple_SetCapsLockState(bool bEnabled) {
  KeybSetCapsLock(bEnabled);
}

void SetBudgetVideo(bool b) {
  g_bBudgetVideo = b;
}

bool GetBudgetVideo() {
  return g_bBudgetVideo;
}

void SetCurrentCLK6502() {
  g_fCurrentCLK6502 = 1.023 * 1000000.0;
}

void SoundCore_SetFade(int fade) {
  (void)fade;
}

void SingleStep(bool bReinit) {
  (void)bReinit;
  uint32_t dwExecutedCycles = CpuExecute(1);
  cyclenum += dwExecutedCycles;
  g_nCumulativeCycles += dwExecutedCycles;
  VideoUpdateVbl(dwExecutedCycles);
  UpdateDisplay(UPDATE_ALL);
}

void ContinueExecution() {
  uint32_t dwCyclesThisFrame = CpuGetCyclesThisFrame(cyclenum);
  if (dwCyclesThisFrame == 0) return;

  uint32_t dwExecutedCycles = CpuExecute(dwCyclesThisFrame);
  cyclenum += dwExecutedCycles;
  g_nCumulativeCycles += dwExecutedCycles;

  if (behind) {
    VideoUpdateVbl(dwExecutedCycles);
    JoyUpdatePosition(dwExecutedCycles);
    SSCFrontend_Update(&sg_SSC, dwExecutedCycles);
    PrinterFrontend_Update(dwExecutedCycles);
    MB_UpdateCycles(dwExecutedCycles);
    SpkrUpdate(dwExecutedCycles);
  }
}

int SysInit(bool bLog) {
  if (bLog) {
    std::string logPath = Path::GetUserDataDir() + "/linapple.log";
    g_fh = fopen(logPath.c_str(), "w");
  }

  Logger::Initialize();
  Asset_Init();

  if (InitSDL() != 0) {
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

  return 0;
}

void SysShutdown() {
  SysClk_UninitTimer();
  RiffFinishWriteFile();

  DSShutdown();

  SDL_Quit();
  if (g_curl) {
    curl_easy_cleanup(g_curl);
    curl_global_cleanup();
  }
  Asset_Quit();
  Logger::Destroy();
}

int SessionInit(const char* szConfigurationFile, bool bSetFullScreen,
                const char* szImageName_drive1, const char* szImageName_drive2,
                const char* szSnapshotFile, bool bBoot, bool bPAL) {
  if (szConfigurationFile) {
    Configuration::Instance().Load(szConfigurationFile);
  } else {
    std::string configPath = Path::GetUserDataDir() + "/linapple.conf";
    Configuration::Instance().Load(configPath);
  }

  if (bSetFullScreen) g_state.fullscreen = true;

  if (FrameCreateWindow() != 0) return 1;

  MemInitialize();
  CpuInitialize();
  VideoInitialize();
  SpkrInitialize();
  KeybReset();
  JoyReset();
  MB_Initialize();
  
  uint8_t* pCxRomPeripheral = MemGetAuxPtr(APPLE_SLOT_BEGIN);
  SSC_Initialize(&sg_SSC, pCxRomPeripheral, 2); // Slot 2
  PrintInitialize();

  if (bPAL) g_videotype = VT_COLOR_TVEMU;

  if (szSnapshotFile) {
    Snapshot_SetFilename(szSnapshotFile);
    Snapshot_LoadState();
  }

  if (szImageName_drive1) DiskInsert(DRIVE_1, szImageName_drive1, false, false);
  if (szImageName_drive2) DiskInsert(DRIVE_2, szImageName_drive2, false, false);

  if (bBoot) DiskBoot();

  DSInit();
  return 0;
}

void SessionShutdown() {
  PrintDestroy();
  MB_Destroy();
  DiskDestroy();
  VideoDestroy();
  MemDestroy();
  CpuDestroy();
}

void CpuTestHeadless(const char* szTestFile) {
  if (!szTestFile) return;
  
  MemInitialize();
  CpuInitialize();
  
  FILE* f = fopen(szTestFile, "rb");
  if (!f) return;
  
  fread(mem, 1, 65536, f);
  fclose(f);
  
  regs.pc = 0x0400; // Common start for functional tests
  uint64_t count = 0;
  while (count < 100000000) {
    uint32_t executed = CpuExecute(1);
    count += executed;
    if (regs.pc == 0x3469) { // Success trap
        Logger::Info("CPU trapped at 0x%04X after %" PRIu64 " cycles\n", regs.pc, count);
        break;
    }
  }
}
