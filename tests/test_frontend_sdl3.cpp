// SPDX-License-Identifier: GPL-2.0-only

#include <SDL3/SDL.h>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/Frontend.h"

auto ds_init() -> bool { return true; }
auto ds_shutdown() -> void {}

TEST_CASE("SDL3 Frontend In-Window Session Restart") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result);

  AppConfig_t config{};
  AppConfig_Default(&config);

  // Initial session startup creates the window
  int res1 = session_init(&config);
  REQUIRE(res1 == 0);
  REQUIRE(g_window != nullptr);
  SDL_Window* orig_window = g_window;
  SDL_Renderer* orig_renderer = g_renderer;

  // Session shutdown on restart preserves the window for in-window reboot
  SessionShutdown();
  CHECK(g_window == orig_window);
  CHECK(g_renderer == orig_renderer);

  // Second session startup reuses the existing window without creating a second
  // window
  int res2 = session_init(&config);
  REQUIRE(res2 == 0);
  CHECK(g_window == orig_window);
  CHECK(g_renderer == orig_renderer);

  SessionShutdown();
  SysShutdown();

  // Complete system shutdown destroys all window and rendering resources
  CHECK(g_window == nullptr);
  CHECK(g_renderer == nullptr);
  CHECK(g_screen == nullptr);
  CHECK(g_texture == nullptr);
}
