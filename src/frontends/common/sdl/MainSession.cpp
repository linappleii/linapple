// SPDX-License-Identifier: GPL-2.0-only
#if ENABLE_FTP
#include <memory>
#include <new>

#include "core/services/ftp/FtpClient.h"
#endif

#include "Frame.h"
#include "SdlBackend.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "frontends/common/AppController.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/sdl/JoystickFrontend.h"

using Logger::error;

#if ENABLE_FTP
static std::unique_ptr<CurlGlobalGuard_t> g_curl_guard;
#endif

static bool g_budget_video = false;

auto set_budget_video(bool b) -> void { g_budget_video = b; }
auto get_budget_video() noexcept -> bool { return g_budget_video; }

auto single_step(bool is_reinit) -> void {
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

auto sys_shutdown() -> void {
  ds_shutdown();
  frame_destroy_window();
  sdl_compat_quit();
#if ENABLE_FTP
  g_curl_guard.reset();
#endif
}

static auto frontend_set_window_title(const char* title) -> void {
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

auto session_shutdown() -> void {
  ds_shutdown();
  joy_frontend_shutdown();
  app_controller_shutdown();
}
