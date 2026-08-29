// SPDX-License-Identifier: GPL-2.0-only

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <cstdint>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_pixels.h>
#include <string>
#include <cstddef>

#include "apple2/Video.h"
#include "core/Asset.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/sdl3/DiskChoose.h"
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/Frontend.h"

auto ds_init() -> bool { return true; }
auto ds_shutdown() -> void {}
extern void sdl_handle_event(SDL_Event* e);
extern DiskChooseState_t g_diskChooseState;

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
  output[0] = 0x00FF0000;                // Top-left: Red
  output[559] = 0x0000FF00;              // Top-right: Green
  output[383 * 560] = 0x000000FF;        // Bottom-left: Blue
  output[383 * 560 + 559] = 0x00FFFFFF;  // Bottom-right: White

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

TEST_CASE("SDL3 Frontend Fullscreen Toggle Preserves Scaled Dimensions") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result);
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

TEST_CASE("SDL3 Frontend Help Screen Scaling at High Screen Factors") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  // Screen Factor = 3 => 1680x1152
  g_state.screen_width = 1680;
  g_state.screen_height = 1152;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  // Set distinct test pixel in video output buffer
  uint32_t* output = video_get_output_buffer();
  REQUIRE(output != nullptr);
  output[0] = 0x00FF0000;  // Red

  // Queue a keydown event so FrameShowHelpScreen exits immediately after
  // rendering
  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.key = SDLK_ESCAPE;
  REQUIRE(SDL_PushEvent(&key_event));

  FrameShowHelpScreen(static_cast<int>(g_state.screen_width),
                      static_cast<int>(g_state.screen_height));

  // Verify that after dismissal, g_screen is properly restored with the
  // emulator frame
  const auto* screen_pixels =
      reinterpret_cast<const uint32_t*>(g_screen->pixels);
  CHECK(screen_pixels[0] == 0x00FF0000);

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

TEST_CASE(
    "SDL3 Frontend Help Screen Dismissal Clears Fullscreen Pillarbox Margins") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  g_state.screen_width = 1120;
  g_state.screen_height = 768;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Switch to Fullscreen and simulate 1920x1080 resolution
  SetFullScreenMode();
  Frame_OnResize(1920, 1080);
  REQUIRE(g_screen->w == 1920);
  REQUIRE(g_screen->h == 1080);

  // g_new_rect in 1920x1080: x = 172, w = 1575
  // The pillarbox margins are x < 172 and x >= 1747

  // Queue key event so FrameShowHelpScreen dismisses immediately
  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.key = SDLK_SPACE;
  REQUIRE(SDL_PushEvent(&key_event));

  FrameShowHelpScreen(static_cast<int>(g_state.screen_width),
                      static_cast<int>(g_state.screen_height));

  const auto* screen_pixels =
      reinterpret_cast<const uint32_t*>(g_screen->pixels);
  int pitch_pixels = g_screen->pitch / 4;

  // Simulate next emulator frame rendering after help screen was dismissed
  g_frame_ready = true;
  DrawFrameWindow();

  int nonzero_left_margin = 0;
  for (int y = 0; y < 1080; ++y) {
    for (int x = 0; x < 172; ++x) {
      if (screen_pixels[y * pitch_pixels + x] != 0) nonzero_left_margin++;
    }
  }
  CHECK(nonzero_left_margin == 0);

  int nonzero_right_margin = 0;
  for (int y = 0; y < 1080; ++y) {
    for (int x = 1747; x < 1920; ++x) {
      if (screen_pixels[y * pitch_pixels + x] != 0) nonzero_right_margin++;
    }
  }
  CHECK(nonzero_right_margin == 0);

  SetNormalMode();

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

TEST_CASE("SDL3 Frontend Disk Chooser Modal Outline Borders Rendered") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  g_state.screen_width = 560;
  g_state.screen_height = 384;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Set up disk choose state
  g_diskChooseState.active = true;
  g_diskChooseState.slot = 6;
  g_diskChooseState.bg_screen =
      SDL_CreateSurface(560, 384, SDL_PIXELFORMAT_ARGB8888);
  g_diskChooseState.list_handle = nullptr;

  DiskChoose_Draw();

  const auto* screen_pixels =
      reinterpret_cast<const uint32_t*>(g_screen->pixels);
  int pitch_pixels = g_screen->pitch / 4;

  const int sx = 560;
  const int sy = 384;
  const double facy =
      static_cast<double>(sy) / static_cast<double>(SCREEN_HEIGHT);
  const int topx = static_cast<int>(45 * facy);
  const int box_y = topx - 5;
  const int box_h = static_cast<int>(320.0 * facy);

  // 1. Left border at x = 0
  CHECK(screen_pixels[(box_y + 10) * pitch_pixels + 0] == 0x00FFFFFF);

  // 2. Top border at y = box_y
  CHECK(screen_pixels[box_y * pitch_pixels + (sx / 2)] == 0x00FFFFFF);

  // 3. Bottom border at y = box_y + box_h
  CHECK(screen_pixels[(box_y + box_h) * pitch_pixels + (sx / 2)] == 0x00FFFFFF);

  // 4. Right border at x = sx - 1
  CHECK(screen_pixels[(box_y + 10) * pitch_pixels + (sx - 1)] == 0x00FFFFFF);

  // 5. Vertical column separator at x = 480
  CHECK(screen_pixels[(box_y + 10) * pitch_pixels + 480] == 0x00FFFFFF);

  // Teardown
  g_diskChooseState.active = false;
  if (g_diskChooseState.bg_screen != nullptr) {
    SDL_DestroySurface(g_diskChooseState.bg_screen);
    g_diskChooseState.bg_screen = nullptr;
  }
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

TEST_CASE("SDL3 Frontend Help Screen F12 Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  AppConfig_Default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  g_state.mode = MODE_RUNNING;

  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.key = SDLK_F12;
  key_event.key.down = true;
  bool push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result);

  FrameShowHelpScreen(static_cast<int>(g_state.screen_width),
                      static_cast<int>(g_state.screen_height));

  CHECK(g_state.mode == MODE_EXIT);

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

TEST_CASE("SDL3 Frontend Disk Choose Quit Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  AppConfig_Default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  g_state.mode = MODE_RUNNING;

  // Push an SDL_EVENT_QUIT event into the event queue
  SDL_Event quit_event{};
  quit_event.type = SDL_EVENT_QUIT;
  bool push_result = SDL_PushEvent(&quit_event);
  REQUIRE(push_result);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(g_state.screen_width),
                                static_cast<int>(g_state.screen_height), ".", 6,
                                filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(g_state.mode == MODE_EXIT);

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

TEST_CASE("SDL3 Frontend Disk Choose Key Down Dismissal") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  AppConfig_Default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  g_state.mode = MODE_RUNNING;

  // Push an ESCAPE key down event into the event queue
  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.key = SDLK_ESCAPE;
  key_event.key.down = true;
  bool push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(g_state.screen_width),
                                static_cast<int>(g_state.screen_height), ".", 6,
                                filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(g_state.mode == MODE_RUNNING);

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

TEST_CASE("SDL3 Frontend Disk Choose Window Close Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  AppConfig_Default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  g_state.mode = MODE_RUNNING;

  // Push an SDL_EVENT_WINDOW_CLOSE_REQUESTED event into the event queue
  SDL_Event close_event{};
  close_event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  bool push_result = SDL_PushEvent(&close_event);
  REQUIRE(push_result);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(g_state.screen_width),
                                static_cast<int>(g_state.screen_height), ".", 6,
                                filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(g_state.mode == MODE_EXIT);

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

TEST_CASE("SDL3 Frontend Disk Choose F12 Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  AppConfig_Default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  g_state.mode = MODE_RUNNING;

  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.key = SDLK_F12;
  key_event.key.down = true;
  bool push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(g_state.screen_width),
                                static_cast<int>(g_state.screen_height), ".", 6,
                                filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(g_state.mode == MODE_EXIT);

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
