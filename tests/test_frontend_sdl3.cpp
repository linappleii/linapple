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
extern void sdl_handle_event(SDL_Event* e);

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
  if (g_texture != nullptr) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_screen != nullptr) {
    SDL_DestroySurface(g_screen);
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

  if (g_texture != nullptr) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_screen != nullptr) {
    SDL_DestroySurface(g_screen);
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

TEST_CASE("SDL3 Frontend Help Screen Quit Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  AppConfig_Default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Push an SDL_EVENT_QUIT event into the event queue
  SDL_Event quit_event{};
  quit_event.type = SDL_EVENT_QUIT;
  bool push_result = SDL_PushEvent(&quit_event);
  REQUIRE(push_result);

  // FrameShowHelpScreen should not hang or discard the quit event
  FrameShowHelpScreen(static_cast<int>(g_state.screen_width),
                      static_cast<int>(g_state.screen_height));

  // Verify that SDL_EVENT_QUIT was re-pushed and is available in the event
  // queue
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_EVENT_QUIT);

  // Teardown
  if (g_texture != nullptr) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_screen != nullptr) {
    SDL_DestroySurface(g_screen);
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

TEST_CASE("SDL3 Frontend Help Screen Key Down Dismissal") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  AppConfig_Default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Push an SDL_EVENT_KEY_DOWN event into the event queue
  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.key = SDLK_SPACE;
  key_event.key.down = true;
  bool push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result);

  // FrameShowHelpScreen should immediately consume the key event and dismiss
  FrameShowHelpScreen(static_cast<int>(g_state.screen_width),
                      static_cast<int>(g_state.screen_height));

  // Verify that the event queue is drained
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 0);

  // Teardown
  if (g_texture != nullptr) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_screen != nullptr) {
    SDL_DestroySurface(g_screen);
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

TEST_CASE("SDL3 Frontend Help Screen Window Close Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  AppConfig_Default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Push an SDL_EVENT_WINDOW_CLOSE_REQUESTED event into the event queue
  SDL_Event close_event{};
  close_event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  bool push_result = SDL_PushEvent(&close_event);
  REQUIRE(push_result);

  // FrameShowHelpScreen should not hang or discard the window close event
  FrameShowHelpScreen(static_cast<int>(g_state.screen_width),
                      static_cast<int>(g_state.screen_height));

  // Verify that SDL_EVENT_WINDOW_CLOSE_REQUESTED was re-pushed and is available
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED);

  // Teardown
  if (g_texture != nullptr) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_screen != nullptr) {
    SDL_DestroySurface(g_screen);
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

TEST_CASE("SDL3 Frontend Main Event Handler Window Close Request") {
  g_state.mode = MODE_RUNNING;
  SDL_Event close_event{};
  close_event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  sdl_handle_event(&close_event);
  CHECK(g_state.mode == MODE_EXIT);
}

