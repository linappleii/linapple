#include <SDL/SDL.h>
#include <curl/curl.h>
#include <curl/easy.h>


#include "SDL_video.h"
#include "AppConfig.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "frontends/common/AppController.h"
#include "frontends/sdl1/Frame.h"
#include "frontends/sdl1/Frontend.h"
#include "frontends/sdl1/JoystickFrontend.h"

using Logger::error;
using Logger::info;

static bool g_budget_video = false;

void set_budget_video(bool b) { g_budget_video = b; }
auto get_budget_video() -> bool { return g_budget_video; }

void set_current_clk_6502() {
  constexpr double apple2_clock_mhz = 1.023;
  constexpr double mhz_to_hz = 1000000.0;
  g_current_clk_6502 = apple2_clock_mhz * mhz_to_hz;
}

void SingleStep(bool is_reinit) {
  (void)is_reinit;
  linapple_run_frame(1);
}

auto sys_init() -> int {
  if (init_sdl() != 0) {
    return 1;
  }

  curl_global_init(CURL_GLOBAL_DEFAULT);
  g_curl = curl_easy_init();
  if (g_curl == nullptr) {
    error("Could not initialize CURL easy interface\n");
    return 1;
  }
  curl_easy_setopt(g_curl, CURLOPT_USERPWD, g_state.ftp_user_pass.data());

  return 0;
}

void SysShutdown() {
  ds_shutdown();
  frame_destroy_window();
  SDL_Quit();
  if (g_curl != nullptr) {
    curl_easy_cleanup(g_curl);
    curl_global_cleanup();
  }
}

static void Frontend_SetWindowTitle(const char* title) {
  SDL_WM_SetCaption(title, title);
}

auto session_init(AppConfig_t* config) -> int {
  if (app_controller_initialize(config) != 0) {
    return 1;
  }

  linapple_set_title_callback(Frontend_SetWindowTitle);

  if (frame_create_window() != 0) {
    return 1;
  }

  AppController_LoadInitialMedia(config);

  ds_init();
  JoyFrontend_Initialize();
  return 0;
}

void SessionShutdown() {
  JoyFrontend_ShutDown();
  AppController_Shutdown();
}
