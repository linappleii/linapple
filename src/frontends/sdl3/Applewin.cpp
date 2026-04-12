#include "frontends/sdl3/AppleWin.h"

#include <SDL3/SDL.h>
#include <X11/Xlib.h>
#include <curl/curl.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cinttypes>
#include <string>
#include <vector>

#include "core/Common.h"
#include "core/Common_Globals.h"
#include "Debugger/Debug.h"
#include "frontends/sdl3/Frame.h"
#include "core/Log.h"
#include "apple2/ParallelPrinter.h"
#include "core/Registry.h"
#include "apple2/SaveState.h"
#include "apple2/CPU.h"
#include "apple2/Clock.h"
#include "apple2/Disk.h"
#include "apple2/DiskImage.h"
#include "apple2/Joystick.h"
#include "apple2/Keyboard.h"
#include "apple2/Memory.h"
#include "apple2/Mockingboard.h"
#include "apple2/Riff.h"
#include "apple2/SerialComms.h"
#include "apple2/SoundCore.h"
#include "apple2/Speaker.h"
#include "apple2/Timer.h"
#include "apple2/Video.h"
#include "core/asset.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

using Logger::Error;
using Logger::Info;
using std::string;
using std::vector;

// Tier 3: Core Application Logic (Applewin.cpp)

static bool g_bBudgetVideo = false;

void SetBudgetVideo(bool b) { g_bBudgetVideo = b; }

auto GetBudgetVideo() -> bool { return g_bBudgetVideo; }

void SetCurrentCLK6502() {
  g_fCurrentCLK6502 = 1.023 * 1000000.0;
}

void SoundCore_SetFade(int fade) { (void)fade; }

void SingleStep(bool bReinit) {
  (void)bReinit;
  uint32_t dwExecutedCycles = CpuExecute(1);
  cyclenum += dwExecutedCycles;
  g_nCumulativeCycles += dwExecutedCycles;
  VideoUpdateVbl(dwExecutedCycles);
  UpdateDisplay(UPDATE_ALL);
}

void ContinueExecution(uint32_t dwCycles) { Linapple_RunFrame(dwCycles); }

int SysInit(bool bLog) {
  if (bLog) {
    std::string logPath = Path::GetUserDataDir() + "/linapple.log";
    g_fh = fopen(logPath.c_str(), "w");
  }

  Logger::Initialize();
  Asset_Init();
  SoundCore_Initialize();

  if (InitSDL() != 0) {
    return 1;
  }

  curl_global_init(CURL_GLOBAL_DEFAULT);
  g_curl = curl_easy_init();
  if (!g_curl) {
    Error("Could not initialize CURL easy interface\n");
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
  SoundCore_Destroy();
  Asset_Quit();
  Logger::Destroy();
}

static void Frontend_SetWindowTitle(const char* title) {
  extern SDL_Window* g_window;
  if (g_window) {
    SDL_SetWindowTitle(g_window, title);
  }
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

  Linapple_SetTitleCallback(Frontend_SetWindowTitle);

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
  SSC_Initialize(&sg_SSC, pCxRomPeripheral, 2);
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

  regs.pc = 0x0400;
  uint64_t count = 0;
  while (count < 100000000) {
    uint32_t executed = CpuExecute(1);
    count += executed;
    if (regs.pc == 0x3469) {
      Info("CPU trapped at 0x%04X after %" PRIu64 " cycles\n", regs.pc, count);
      break;
    }
  }
}
