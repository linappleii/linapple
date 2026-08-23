// SPDX-License-Identifier: GPL-2.0-only

#include <SDL3/SDL.h>

#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "core/Asset.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/Frontend.h"

auto ds_init() -> bool { return true; }
auto ds_shutdown() -> void {}

TEST_CASE("SDL3 Frontend Initialization and Screen Scaling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result);
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
  if (g_texture) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_screen) {
    SDL_DestroySurface(g_screen);
    g_screen = nullptr;
  }
  if (g_renderer) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }
  if (g_window) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend DrawFrameWindow Scaled Stretching") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  g_state.screen_width = 1120;
  g_state.screen_height = 768;
  g_state.mode = MODE_LOGO;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Set distinct test pixels in video output buffer
  uint32_t* output = video_get_output_buffer();
  REQUIRE(output != nullptr);
  output[0] = 0x00FF0000;                     // Top-left: Red
  output[559] = 0x0000FF00;                   // Top-right: Green
  output[383 * 560] = 0x000000FF;             // Bottom-left: Blue
  output[383 * 560 + 559] = 0x00FFFFFF;       // Bottom-right: White

  g_frame_ready = true;
  DrawFrameWindow();

  // Inspect scaled g_screen pixels (1120x768)
  const auto* screen_pixels =
      reinterpret_cast<const uint32_t*>(g_screen->pixels);
  int pitch_pixels = g_screen->pitch / 4;

  CHECK(screen_pixels[0] == 0x00FF0000);
  CHECK(screen_pixels[1119] == 0x0000FF00);
  CHECK(screen_pixels[767 * pitch_pixels] == 0x000000FF);
  CHECK(screen_pixels[767 * pitch_pixels + 1119] == 0x00FFFFFF);

  if (g_texture) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_screen) {
    SDL_DestroySurface(g_screen);
    g_screen = nullptr;
  }
  if (g_renderer) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }
  if (g_window) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
  asset_quit();
  SDL_Quit();
}
