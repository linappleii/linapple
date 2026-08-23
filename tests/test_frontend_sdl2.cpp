// SPDX-License-Identifier: GPL-2.0-only

#include <SDL2/SDL.h>

#include "apple2/Apple2Types.h"
#include "core/Asset.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/sdl2/Frame.h"
#include "frontends/sdl2/Frontend.h"

auto ds_init() -> bool { return true; }
auto ds_shutdown() -> void {}

TEST_CASE("SDL2 Frontend In-Window Session Restart") {
  SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);

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

TEST_CASE("SDL2 Frontend Initialization and Screen Scaling") {
  SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  // Set scaled screen resolution (e.g. Screen Factor = 2 => 1120x768)
  g_state.screen_width = 1120;
  g_state.screen_height = 768;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Verify that g_screen surface matches the configured screen width and height
  CHECK(g_screen->w == 1120);
  CHECK(g_screen->h == 768);
  CHECK(g_window_resized == true);

  // Cleanup
  if (g_texture != nullptr) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_screen != nullptr) {
    SDL_FreeSurface(g_screen);
    g_screen = nullptr;
  }
  if (g_renderer != nullptr) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }
  if (g_window != nullptr) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL2 Frontend Fullscreen Toggle Preserves Scaled Dimensions") {
  SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  // 1. Configure scaled resolution (Screen Factor = 2 => 1120x768)
  g_state.screen_width = 1120;
  g_state.screen_height = 768;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);
  CHECK(g_screen->w == 1120);
  CHECK(g_screen->h == 768);

  // 2. Toggle into fullscreen mode
  SetFullScreenMode();
  // Simulate monitor resolution delivered via SDL resize event in fullscreen
  Frame_OnResize(1920, 1080);
  CHECK(g_screen->w == 1920);
  CHECK(g_screen->h == 1080);

  // In 1920x1080 fullscreen, 4:3 / 560x384 aspect ratio should be preserved
  // target_w = 1575, target_h = 1080, offset_x = (1920 - 1575) / 2 = 172
  CHECK(g_new_rect.w == 1575);
  CHECK(g_new_rect.h == 1080);
  CHECK(g_new_rect.x == 172);
  CHECK(g_new_rect.y == 0);

  // 3. Return to windowed mode (F6)
  SetNormalMode();

  // Windowed mode must restore original configured dimensions and full rect
  CHECK(g_state.screen_width == 1120);
  CHECK(g_state.screen_height == 768);
  CHECK(g_screen->w == 1120);
  CHECK(g_screen->h == 768);
  CHECK(g_new_rect.w == 1120);
  CHECK(g_new_rect.h == 768);
  CHECK(g_new_rect.x == 0);
  CHECK(g_new_rect.y == 0);

  if (g_texture != nullptr) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_screen != nullptr) {
    SDL_FreeSurface(g_screen);
    g_screen = nullptr;
  }
  if (g_renderer != nullptr) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }
  if (g_window != nullptr) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
  asset_quit();
  SDL_Quit();
}

