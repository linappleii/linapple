// SPDX-License-Identifier: GPL-2.0-only
#include <memory>
#include <new>

#include "AppConfig.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/services/ftp/FtpClient.h"
#include "frontends/common/AppController.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/common/sdl/SdlCompat.h"

using Logger::error;
using Logger::info;

#if ENABLE_FTP
static std::unique_ptr<CurlGlobalGuard_t> g_curl_guard;
#endif

static bool g_budget_video = false;

void set_budget_video(bool b) { g_budget_video = b; }
auto get_budget_video() -> bool { return g_budget_video; }

void single_step(bool is_reinit) {
  (void)is_reinit;
  linapple_run_frame(1);
}

auto sys_init() -> int {
  if (init_sdl() != 0) {
    return 1;
  }

#if ENABLE_FTP
  g_curl_guard = std::unique_ptr<CurlGlobalGuard_t>(new (std::nothrow)
                                                        CurlGlobalGuard_t());
  if (!g_curl_guard) {
    error("Could not initialize CURL global environment\n");
    return 1;
  }
#endif

  return 0;
}

void sys_shutdown() {
  ds_shutdown();
  frame_destroy_window();
  SDL_Quit();
#if ENABLE_FTP
  g_curl_guard.reset();
#endif
}

static void frontend_set_window_title(const char* title) {
  sdl_compat_set_window_title(title);
}

auto session_init(AppConfig_t* config) -> int {
  if (app_controller_initialize(config) != 0) {
    return 1;
  }

  linapple_set_title_callback(frontend_set_window_title);

  if (frame_create_window() != 0) {
    return 1;
  }

  app_controller_load_initial_media(config);

  ds_init();
  joy_frontend_initialize();
  return 0;
}

void session_shutdown() {
  ds_shutdown();
  joy_frontend_shutdown();
  app_controller_shutdown();
}
