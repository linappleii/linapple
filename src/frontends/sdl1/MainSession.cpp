#include <SDL/SDL.h>
#include <curl/curl.h>

#include <cinttypes>
#include <cstdio>
#include <string>

#include "apple2/CPU.h"
#include "apple2/Video.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/ProgramLoader.h"
#include "core/Registry.h"
#include "frontends/common/AppController.h"
#include "frontends/sdl1/Frame.h"
#include "frontends/sdl1/Frontend.h"

using Logger::Error;
using Logger::Info;

static bool g_bBudgetVideo = false;

void SetBudgetVideo(bool b) { g_bBudgetVideo = b; }
auto GetBudgetVideo() -> bool { return g_bBudgetVideo; }

void SetCurrentCLK6502() {
  constexpr double APPLE2_CLOCK_MHZ = 1.023;
  constexpr double MHZ_TO_HZ = 1000000.0;
  g_fCurrentCLK6502 = APPLE2_CLOCK_MHZ * MHZ_TO_HZ;
}

void SoundCore_SetFade(int fade) { (void)fade; }

void SingleStep(bool bReinit) {
  (void)bReinit;
  Linapple_RunFrame(1);
}

auto SysInit() -> int {
  if (InitSDL() != 0) {
    return 1;
  }

  curl_global_init(CURL_GLOBAL_DEFAULT);
  g_curl = curl_easy_init();
  if (g_curl == nullptr) {
    Error("Could not initialize CURL easy interface\n");
    return 1;
  }
  curl_easy_setopt(g_curl, CURLOPT_USERPWD, g_state.sFTPUserPass.data());

  return 0;
}

void SysShutdown() {
  DSShutdown();

  SDL_Quit();
  if (g_curl != nullptr) {
    curl_easy_cleanup(g_curl);
    curl_global_cleanup();
  }
}

static void Frontend_SetWindowTitle(const char* title) {
  SDL_WM_SetCaption(title, title);
}

auto SessionInit(AppConfig* config) -> int {
  if (AppController_Initialize(config) != 0) {
    return 1;
  }

  Linapple_SetTitleCallback(Frontend_SetWindowTitle);

  if (FrameCreateWindow() != 0) {
    return 1;
  }

  AppController_LoadInitialMedia(config);

  DSInit();
  return 0;
}

void SessionShutdown() { AppController_Shutdown(); }
