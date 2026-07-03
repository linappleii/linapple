#include <SDL3/SDL.h>
#include <curl/curl.h>

#include <cinttypes>
#include <cstdio>
#include <string>

#include "apple2/CPU.h"
#include "apple2/Video.h"
#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Log.h"
#include "core/ProgramLoader.h"
#include "core/Registry.h"
#include "frontends/common/AppController.h"
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/Frontend.h"

using Logger::Error;
using Logger::Info;

static bool g_bBudgetVideo = false;

void SetBudgetVideo(bool b) { g_bBudgetVideo = b; }
auto GetBudgetVideo() -> bool { return g_bBudgetVideo; }

void SetCurrentCLK6502() { g_fCurrentCLK6502 = 1.023 * 1000000.0; }


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
  if (g_curl) {
    curl_easy_cleanup(g_curl);
    curl_global_cleanup();
  }
}

static void Frontend_SetWindowTitle(const char* title) {
  extern SDL_Window* g_window;
  if (g_window) {
    SDL_SetWindowTitle(g_window, title);
  }
}

auto SessionInit(AppConfig* config) -> int {
  if (AppController_Initialize(config) != 0) {
    return 1;
  }

  Linapple_SetTitleCallback(Frontend_SetWindowTitle);

  if (FrameCreateWindow() != 0) return 1;

  AppController_LoadInitialMedia(config);

  DSInit();
  return 0;
}

void SessionShutdown() { AppController_Shutdown(); }
