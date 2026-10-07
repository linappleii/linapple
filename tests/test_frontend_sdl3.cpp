// SPDX-License-Identifier: GPL-2.0-only

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/Asset.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/HarddiskFrontend.h"
#include "frontends/common/KeyboardMaps.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/MouseFrontend.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/common/sdl/MouseInput.h"
#include "frontends/sdl3/DiskChoose.h"
#include "frontends/sdl3/Frame.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

auto ds_init() -> bool { return true; }
auto ds_shutdown() -> void {}
extern void sdl_handle_event(SDL_Event* e);
extern DiskChooseState_t g_diskChooseState;

TEST_CASE("SDL3 Frontend In-Window Session Restart") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result);

  AppConfig_t config{};
  app_config_default(&config);

  // Initial session startup creates the window
  int res1 = session_init(&config);
  REQUIRE(res1 == 0);
  REQUIRE(g_window != nullptr);
  SDL_Window* orig_window = g_window.get();
  SDL_Renderer* orig_renderer = g_renderer.get();

  // Session shutdown on restart preserves the window for in-window reboot
  session_shutdown();
  CHECK(g_window.get() == orig_window);
  CHECK(g_renderer.get() == orig_renderer);

  // Second session startup reuses the existing window without creating a second
  // window
  int res2 = session_init(&config);
  REQUIRE(res2 == 0);
  CHECK(g_window.get() == orig_window);
  CHECK(g_renderer.get() == orig_renderer);

  session_shutdown();
  sys_shutdown();
  asset_quit();

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
  system_state.screen_width = 1120;
  system_state.screen_height = 768;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Verify that g_screen surface matches the configured screen width and height
  CHECK(g_screen->w == 1120);
  CHECK(g_screen->h == 768);
  CHECK(g_window_resized == true);

  // Cleanup
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend draw_frame_window Scaled Stretching") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  system_state.screen_width = 1120;
  system_state.screen_height = 768;
  system_state.mode = app_mode_logo;

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
  draw_frame_window();

  // Inspect scaled g_screen pixels (1120x768)
  const auto* screen_pixels =
      reinterpret_cast<const uint32_t*>(g_screen->pixels);
  int pitch_pixels = g_screen->pitch / 4;

  CHECK(screen_pixels[0] == 0x00FF0000);
  CHECK(screen_pixels[1119] == 0x0000FF00);
  CHECK(screen_pixels[767 * pitch_pixels] == 0x000000FF);
  CHECK(screen_pixels[767 * pitch_pixels + 1119] == 0x00FFFFFF);

  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Fullscreen Toggle Preserves Scaled Dimensions") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  // 1. Configure scaled resolution (Screen Factor = 2 => 1120x768)
  system_state.screen_width = 1120;
  system_state.screen_height = 768;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);
  CHECK(g_screen->w == 1120);
  CHECK(g_screen->h == 768);

  // 2. Toggle into fullscreen mode
  set_fullscreen_mode();
  // Simulate monitor resolution delivered via SDL resize event in fullscreen
  frame_on_resize(1920, 1080);
  CHECK(g_screen->w == 1920);
  CHECK(g_screen->h == 1080);

  // In 1920x1080 fullscreen, 4:3 / 560x384 aspect ratio should be preserved
  // target_w = 1575, target_h = 1080, offset_x = (1920 - 1575) / 2 = 172
  CHECK(g_new_rect.w == 1575);
  CHECK(g_new_rect.h == 1080);
  CHECK(g_new_rect.x == 172);
  CHECK(g_new_rect.y == 0);

  const MousePictureRect_t letterbox = frame_picture_rect();
  CHECK(letterbox.x == 172);
  CHECK(letterbox.y == 0);
  CHECK(letterbox.w == 1575);
  CHECK(letterbox.h == 1080);

  // 3. Return to windowed mode (F6)
  set_normal_mode();

  // Windowed mode must restore original configured dimensions and full rect
  CHECK(system_state.screen_width == 1120);
  CHECK(system_state.screen_height == 768);
  CHECK(g_screen->w == 1120);
  CHECK(g_screen->h == 768);
  CHECK(g_new_rect.w == 1120);
  CHECK(g_new_rect.h == 768);
  CHECK(g_new_rect.x == 0);
  CHECK(g_new_rect.y == 0);

  const MousePictureRect_t window = frame_picture_rect();
  CHECK(window.x == 0);
  CHECK(window.y == 0);
  CHECK(window.w == 1120);
  CHECK(window.h == 768);

  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Help Screen Quit Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Push an SDL_EVENT_QUIT event into the event queue
  SDL_Event quit_event{};
  quit_event.type = SDL_EVENT_QUIT;
  bool push_result = SDL_PushEvent(&quit_event);
  REQUIRE(push_result);

  // frame_show_help_screen should not hang or discard the quit event
  frame_show_help_screen(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height));

  // Verify that SDL_EVENT_QUIT was re-pushed and is available in the event
  // queue
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_EVENT_QUIT);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Help Screen Key Down Dismissal") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

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

  // frame_show_help_screen should immediately consume the key event and dismiss
  frame_show_help_screen(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height));

  // Verify that the event queue is drained
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 0);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Help Screen Window Close Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Push an SDL_EVENT_WINDOW_CLOSE_REQUESTED event into the event queue
  SDL_Event close_event{};
  close_event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  bool push_result = SDL_PushEvent(&close_event);
  REQUIRE(push_result);

  // frame_show_help_screen should not hang or discard the window close event
  frame_show_help_screen(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height));

  // Verify that SDL_EVENT_WINDOW_CLOSE_REQUESTED was re-pushed and is available
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Main Event Handler Window Close Request") {
  system_state.mode = app_mode_running;
  SDL_Event close_event{};
  close_event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  sdl_handle_event(&close_event);
  CHECK(system_state.mode == app_mode_exit);
}

TEST_CASE("SDL3 Frontend Help Screen Scaling at High Screen Factors") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  // Screen Factor = 3 => 1680x1152
  system_state.screen_width = 1680;
  system_state.screen_height = 1152;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  // Set distinct test pixel in video output buffer
  uint32_t* output = video_get_output_buffer();
  REQUIRE(output != nullptr);
  output[0] = 0x00FF0000;  // Red

  // Queue a keydown event so frame_show_help_screen exits immediately after
  // rendering
  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.key = SDLK_ESCAPE;
  REQUIRE(SDL_PushEvent(&key_event));

  frame_show_help_screen(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height));

  // Verify that after dismissal, g_screen is properly restored with the
  // emulator frame
  const auto* screen_pixels =
      reinterpret_cast<const uint32_t*>(g_screen->pixels);
  CHECK(screen_pixels[0] == 0x00FF0000);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE(
    "SDL3 Frontend Help Screen Dismissal Clears Fullscreen Pillarbox Margins") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  system_state.screen_width = 1120;
  system_state.screen_height = 768;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Switch to Fullscreen and simulate 1920x1080 resolution
  set_fullscreen_mode();
  frame_on_resize(1920, 1080);
  REQUIRE(g_screen->w == 1920);
  REQUIRE(g_screen->h == 1080);

  // g_new_rect in 1920x1080: x = 172, w = 1575
  // The pillarbox margins are x < 172 and x >= 1747

  // Queue key event so frame_show_help_screen dismisses immediately
  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.key = SDLK_SPACE;
  REQUIRE(SDL_PushEvent(&key_event));

  frame_show_help_screen(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height));

  const auto* screen_pixels =
      reinterpret_cast<const uint32_t*>(g_screen->pixels);
  int pitch_pixels = g_screen->pitch / 4;

  // Simulate next emulator frame rendering after help screen was dismissed
  g_frame_ready = true;
  draw_frame_window();

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

  set_normal_mode();

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Disk Chooser Modal Outline Borders Rendered") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  system_state.screen_width = 560;
  system_state.screen_height = 384;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Set up disk choose state
  g_diskChooseState.active = true;
  g_diskChooseState.slot = 6;
  g_diskChooseState.bg_screen.reset(
      SDL_CreateSurface(560, 384, SDL_PIXELFORMAT_ARGB8888));
  g_diskChooseState.list_handle = nullptr;

  disk_choose_draw();

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
  g_diskChooseState.bg_screen.reset();
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Help Screen F12 Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  system_state.mode = app_mode_running;

  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.key = SDLK_F12;
  key_event.key.down = true;
  bool push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result);

  frame_show_help_screen(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height));

  CHECK(system_state.mode == app_mode_exit);

  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_EVENT_QUIT);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Disk Choose Quit Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  system_state.mode = app_mode_running;

  // Push an SDL_EVENT_QUIT event into the event queue
  SDL_Event quit_event{};
  quit_event.type = SDL_EVENT_QUIT;
  bool push_result = SDL_PushEvent(&quit_event);
  REQUIRE(push_result);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(system_state.screen_width),
                                static_cast<int>(system_state.screen_height),
                                ".", 6, filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(system_state.mode == app_mode_exit);

  // Verify that SDL_EVENT_QUIT was re-pushed and is available in the event
  // queue
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_EVENT_QUIT);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Disk Choose Key Down Dismissal") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  system_state.mode = app_mode_running;

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
  bool chosen = choose_an_image(static_cast<int>(system_state.screen_width),
                                static_cast<int>(system_state.screen_height),
                                ".", 6, filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(system_state.mode == app_mode_running);

  // Verify that the event queue is drained
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 0);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Disk Choose Window Close Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  system_state.mode = app_mode_running;

  // Push an SDL_EVENT_WINDOW_CLOSE_REQUESTED event into the event queue
  SDL_Event close_event{};
  close_event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  bool push_result = SDL_PushEvent(&close_event);
  REQUIRE(push_result);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(system_state.screen_width),
                                static_cast<int>(system_state.screen_height),
                                ".", 6, filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(system_state.mode == app_mode_exit);

  // Verify that SDL_EVENT_WINDOW_CLOSE_REQUESTED was re-pushed and is available
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Disk Choose F12 Event Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  REQUIRE(init_result);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  system_state.mode = app_mode_running;

  SDL_Event key_event{};
  key_event.type = SDL_EVENT_KEY_DOWN;
  key_event.key.key = SDLK_F12;
  key_event.key.down = true;
  bool push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(system_state.screen_width),
                                static_cast<int>(system_state.screen_height),
                                ".", 6, filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(system_state.mode == app_mode_exit);

  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                             SDL_EVENT_LAST);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_EVENT_QUIT);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL3 Frontend Joystick Config Out-of-Range Handling") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  bool init_result = SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_VIDEO);
  REQUIRE(init_result);

  // Set out-of-bounds joy_type values in registry
  save(REGVALUE_JOY_TYPE1, 9999);
  save(REGVALUE_JOY_TYPE2, 8888);

  // joy_frontend_initialize should safely clamp / default to 0 without
  // out-of-bounds access
  CHECK_NOTHROW(joy_frontend_initialize());

  // Operations on out-of-range initialized joysticks must be safe
  CHECK_NOTHROW(joy_frontend_update());
  CHECK_NOTHROW(joy_frontend_is_mouse_emulation_active());
  CHECK_NOTHROW(joy_frontend_process_mouse_motion(100, 200, 100, 200));
  CHECK_NOTHROW(joy_frontend_process_mouse_button(0, true));
  CHECK_NOTHROW(joy_frontend_process_key(SDLK_KP_1, false, true, false));

  joy_frontend_shutdown();
  SDL_Quit();
}

#ifdef ENABLE_PERIPHERAL_JOYSTICK
namespace {

constexpr uint16_t addr_switch0 = 0xC061;
constexpr uint8_t switch_bit = 0x80;

// The game port alone in slot 0 of an Enhanced //e, read through the memory
// map as the 6502 would read it. Commands queue until a slice boundary, so
// the fixture settles them the way linapple_run_frame does.
struct GamePortOnly_t {
  Apple2Type_t saved_type{current_apple2_type};
  CpuInstance_t* saved_cpu{cpu_get_active_context()};
  CpuInstance_t cpu{};

  GamePortOnly_t() {
    cpu_set_active_context(&cpu);
    current_apple2_type = A2TYPE_APPLE2EENHANCED;
    // The pre-initialisation installs the motherboard's switch-input handlers.
    mem_pre_initialize();
    REQUIRE(mem_initialize() == 0);
    peripheral_manager_init();
    Peripheral_t* joystick = peripheral_find_internal("linapple.joystick");
    REQUIRE(joystick != nullptr);
    REQUIRE(peripheral_register(joystick, 0) == 0);
    peripheral_manager_reset();
  }

  ~GamePortOnly_t() {
    peripheral_manager_shutdown();
    mem_destroy();
    current_apple2_type = saved_type;
    cpu_set_active_context(saved_cpu);
  }

  GamePortOnly_t(const GamePortOnly_t&) = delete;
  auto operator=(const GamePortOnly_t&) -> GamePortOnly_t& = delete;
  GamePortOnly_t(GamePortOnly_t&&) = delete;
  auto operator=(GamePortOnly_t&&) -> GamePortOnly_t& = delete;

  static auto settle() -> void { peripheral_manager_think(0); }

  static auto pushbutton(uint8_t line) -> int {
    settle();
    const auto addr = static_cast<uint16_t>(addr_switch0 + line);
    return (io_map_dispatch(0, addr, 0, 0, 0) & switch_bit) != 0 ? 1 : 0;
  }
};

}  // namespace

TEST_CASE(
    "A keypad tap and re-press inside one SDL pump reach the game port as "
    "down, up, down across three slices") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_JOYSTICK));

  GamePortOnly_t machine;
  save(REGVALUE_JOY_TYPE1, 2);
  save(REGVALUE_JOY_TYPE2, 0);
  joy_frontend_initialize();
  CHECK(GamePortOnly_t::pushbutton(0) == 0);

  // Three edges with no slice between them: the press goes at once, the
  // release and the second press wait their turn.
  CHECK(joy_frontend_process_key(SDLK_KP_0, false, true, false));
  CHECK(joy_frontend_process_key(SDLK_KP_0, false, false, false));
  CHECK(joy_frontend_process_key(SDLK_KP_0, false, true, false));
  CHECK(GamePortOnly_t::pushbutton(0) == 1);

  // The first slice after the press still shows it: a release the same
  // slice as its press would be invisible to the program.
  joy_frontend_update();
  CHECK(GamePortOnly_t::pushbutton(0) == 1);

  joy_frontend_update();
  CHECK(GamePortOnly_t::pushbutton(0) == 0);

  joy_frontend_update();
  CHECK(GamePortOnly_t::pushbutton(0) == 1);

  joy_frontend_update();
  CHECK(GamePortOnly_t::pushbutton(0) == 1);

  // A release that arrives with the queue empty and a slice behind it goes
  // at once.
  CHECK(joy_frontend_process_key(SDLK_KP_0, false, false, false));
  CHECK(GamePortOnly_t::pushbutton(0) == 0);
  joy_frontend_update();

  // A release still queued at shutdown is delivered by it, so no session
  // leaves a button held.
  CHECK(joy_frontend_process_key(SDLK_KP_0, false, true, false));
  CHECK(joy_frontend_process_key(SDLK_KP_0, false, false, false));
  CHECK(GamePortOnly_t::pushbutton(0) == 1);
  joy_frontend_shutdown();
  CHECK(GamePortOnly_t::pushbutton(0) == 0);

  SDL_Quit();
}
#endif

namespace {

constexpr uint16_t mouse_program_start = 0x0300;

#if defined(ENABLE_PERIPHERAL_JOYSTICK)
constexpr uint16_t rom_pread = 0xFB1E;
constexpr uint32_t pread_cycle_cap = 4000;

// LDX #paddle; JSR $FB1E; NOP: the Monitor's PREAD answers the paddle's
// position in Y.
auto pread(uint8_t paddle) -> uint8_t {
  peripheral_manager_think(0);
  const std::array<uint8_t, 6> caller = {0xA2, paddle, 0x20, 0x1E, 0xFB, 0xEA};
  constexpr uint16_t sentinel = mouse_program_start + 5;
  TestFixtures::ScopedCore_t::poke(mouse_program_start, caller);
  TestFixtures::enter_at({mouse_program_start, 0, 0, 0});
  TestFixtures::step_until_pc(rom_pread, pread_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == rom_pread);
  TestFixtures::step_until_pc(sentinel, pread_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == sentinel);
  return cpu_get_registers()->y;
}
#endif

struct MouseInputMachine_t {
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;
  AppMode_t saved_mode;

  explicit MouseInputMachine_t(
      const TestFixtures::ScopedTestConfig_t::Description_t& description)
      : config(description), core(config), saved_mode(system_state.mode) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK));
    peripheral_manager_init();
    linapple_register_peripherals();
    linapple_reset_hard();
    joy_frontend_initialize();
    mouse_frontend_initialize();
    mouse_input_release();
    system_state.mode = app_mode_running;
    system_state.screen_width = 560;
    system_state.screen_height = 384;
    g_window_resized = false;
    g_buttondown = -1;
    SDL_SetModState(SDL_KMOD_NONE);
  }

  ~MouseInputMachine_t() {
    SDL_SetModState(SDL_KMOD_NONE);
    mouse_input_release();
    g_window_resized = false;
    g_buttondown = -1;
    system_state.mode = saved_mode;
    joy_frontend_shutdown();
    SDL_Quit();
  }

  MouseInputMachine_t(const MouseInputMachine_t&) = delete;
  auto operator=(const MouseInputMachine_t&) -> MouseInputMachine_t& = delete;
  MouseInputMachine_t(MouseInputMachine_t&&) = delete;
  auto operator=(MouseInputMachine_t&&) -> MouseInputMachine_t& = delete;

  static auto click(Uint8 button, bool down) -> void {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = button;
    event.button.down = down;
    sdl_handle_event(&event);
  }

  static auto move(float dx, float dy, float x, float y) -> void {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel = dx;
    event.motion.yrel = dy;
    event.motion.x = x;
    event.motion.y = y;
    sdl_handle_event(&event);
  }

  static auto letterbox(int x, int y, int w, int h) -> void {
    g_window_resized = true;
    g_new_rect = SDL_Rect{x, y, w, h};
  }
};

auto mouse_as_joystick() -> TestFixtures::ScopedTestConfig_t::Description_t {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.extras.push_back({"Configuration", "Joystick 0", "4"});
  return description;
}

}  // namespace

TEST_CASE(
    "SDL3 mouse capture: with no mouse card and no mouse-emulated joystick "
    "neither a left nor a middle click takes the pointer") {
  MouseInputMachine_t machine(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  CHECK_FALSE(mouse_input_consumer_present());

  MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
  CHECK_FALSE(mouse_input_is_captured());
  MouseInputMachine_t::click(SDL_BUTTON_MIDDLE, true);
  MouseInputMachine_t::click(SDL_BUTTON_MIDDLE, false);
  CHECK_FALSE(mouse_input_is_captured());
}

TEST_CASE(
    "SDL3 mouse capture: a mouse-emulated joystick is a consumer, so a left "
    "click captures and a Shift-click releases") {
  MouseInputMachine_t machine(mouse_as_joystick());
  REQUIRE_FALSE(mouse_frontend_card_present());
  REQUIRE(joy_frontend_is_mouse_emulation_active());

  MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
  CHECK(mouse_input_is_captured());

  SDL_SetModState(SDL_KMOD_LSHIFT);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
  CHECK_FALSE(mouse_input_is_captured());
}

#if defined(ENABLE_PERIPHERAL_MOUSE)

namespace {

constexpr uint16_t mouse_indirect_jump = 0x0320;
constexpr uint16_t mouse_entry_table = 0x12;
constexpr int mouse_entry_set_mouse = 0;
constexpr int mouse_entry_read_mouse = 2;
constexpr uint32_t mouse_firmware_cycle_cap = 200000;

// The table at $Cn12 holds the low bytes of the entries, so every call goes
// through it indirectly: LDA $Cn12+k / STA $07 / LDA #$Cn / STA $08 / LDA #a
// / LDX #$Cn / LDY #$n0 / JSR $0320, with JMP ($0007) at $0320, then a spin.
auto call_mouse_firmware(int slot, int entry, uint8_t a) -> void {
  const auto page = static_cast<uint8_t>(0xC0 + slot);
  const auto table =
      static_cast<uint16_t>((page << 8) + mouse_entry_table + entry);
  std::vector<uint8_t> program = {
      0xAD,
      static_cast<uint8_t>(table & 0xFF),
      static_cast<uint8_t>(table >> 8),
      0x85,
      0x07,
      0xA9,
      page,
      0x85,
      0x08,
      0xA9,
      a,
      0xA2,
      page,
      0xA0,
      static_cast<uint8_t>(slot << 4),
      0x20,
      static_cast<uint8_t>(mouse_indirect_jump & 0xFF),
      static_cast<uint8_t>(mouse_indirect_jump >> 8)};
  const auto spin = static_cast<uint16_t>(mouse_program_start + program.size());
  program.push_back(0x4C);
  program.push_back(static_cast<uint8_t>(spin & 0xFF));
  program.push_back(static_cast<uint8_t>(spin >> 8));
  TestFixtures::ScopedCore_t::poke(mouse_program_start, program.data(),
                                   program.size());
  const std::array<uint8_t, 3> jump = {0x6C, 0x07, 0x00};
  TestFixtures::ScopedCore_t::poke(mouse_indirect_jump, jump);
  TestFixtures::enter_at({mouse_program_start, 0, 0, 0});
  TestFixtures::step_until_pc(spin, mouse_firmware_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == spin);
}

// READMOUSE through the table, then the slot's X holes (manual p. 44).
auto read_mouse_x(int slot) -> int16_t {
  peripheral_manager_think(0);
  call_mouse_firmware(slot, mouse_entry_read_mouse, 0);
  const auto n = static_cast<uint16_t>(slot);
  return static_cast<int16_t>(mem[0x478 + n] | (mem[0x578 + n] << 8));
}

auto mouse_in_slot_4() -> TestFixtures::ScopedTestConfig_t::Description_t {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[3] = "Mouse Interface";
  return description;
}

}  // namespace

TEST_CASE(
    "SDL3 mouse capture: with a mouse card in slot 4 the first left click "
    "captures, a toolbar key or a pause refuses it, the middle button "
    "toggles, and Shift- or Ctrl-click releases") {
  MouseInputMachine_t machine(mouse_in_slot_4());
  REQUIRE(mouse_frontend_card_slot() == 4);

  SUBCASE("the first left click captures and is swallowed") {
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
    CHECK(mouse_input_is_captured());
    SDL_SetModState(SDL_KMOD_LSHIFT);
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
    CHECK_FALSE(mouse_input_is_captured());
  }

  SUBCASE("Ctrl-click releases too") {
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
    REQUIRE(mouse_input_is_captured());
    SDL_SetModState(SDL_KMOD_LCTRL);
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
    CHECK_FALSE(mouse_input_is_captured());
  }

  SUBCASE("a left click while a toolbar key is held is ignored") {
    g_buttondown = k_btn_help;
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
    CHECK_FALSE(mouse_input_is_captured());
  }

  SUBCASE("a left click while paused captures nothing") {
    system_state.mode = app_mode_paused;
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
    CHECK_FALSE(mouse_input_is_captured());
  }

  SUBCASE("the middle button captures and releases in turn") {
    MouseInputMachine_t::click(SDL_BUTTON_MIDDLE, true);
    MouseInputMachine_t::click(SDL_BUTTON_MIDDLE, false);
    CHECK(mouse_input_is_captured());
    MouseInputMachine_t::click(SDL_BUTTON_MIDDLE, true);
    MouseInputMachine_t::click(SDL_BUTTON_MIDDLE, false);
    CHECK_FALSE(mouse_input_is_captured());
  }
}

TEST_CASE(
    "SDL3 mouse capture: Mouse Capture = 0 refuses the left and the middle "
    "click even with a card present") {
  TestFixtures::ScopedTestConfig_t::Description_t description =
      mouse_in_slot_4();
  description.extras.push_back({"Configuration", "Mouse Capture", "0"});
  MouseInputMachine_t machine(description);
  REQUIRE(mouse_frontend_card_present());
  REQUIRE_FALSE(mouse_frontend_capture_enabled());

  MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
  CHECK_FALSE(mouse_input_is_captured());
  MouseInputMachine_t::click(SDL_BUTTON_MIDDLE, true);
  MouseInputMachine_t::click(SDL_BUTTON_MIDDLE, false);
  CHECK_FALSE(mouse_input_is_captured());
}

TEST_CASE(
    "SDL3 mouse motion: a captured motion event reaches the card as counts "
    "scaled to the picture the frame draws, letterboxed or not") {
  MouseInputMachine_t machine(mouse_in_slot_4());
  call_mouse_firmware(4, mouse_entry_set_mouse, 0x01);
  REQUIRE(read_mouse_x(4) == 0);

  MouseInputMachine_t::move(2.0F, 0.0F, 2.0F, 0.0F);
  CHECK(read_mouse_x(4) == 0);

  MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
  REQUIRE(mouse_input_is_captured());

  MouseInputMachine_t::move(2.0F, 0.0F, 4.0F, 0.0F);
  CHECK(read_mouse_x(4) == 1);

  MouseInputMachine_t::letterbox(280, 0, 560, 384);
  MouseInputMachine_t::move(2.0F, 0.0F, 286.0F, 0.0F);
  CHECK(read_mouse_x(4) == 2);

  MouseInputMachine_t::letterbox(0, 0, 1120, 768);
  MouseInputMachine_t::move(4.0F, 0.0F, 12.0F, 0.0F);
  CHECK(read_mouse_x(4) == 3);
  MouseInputMachine_t::move(2.0F, 0.0F, 14.0F, 0.0F);
  CHECK(read_mouse_x(4) == 3);
}
#endif

#if defined(ENABLE_PERIPHERAL_JOYSTICK)
TEST_CASE(
    "SDL3 mouse motion: the joystick-as-mouse path maps the picture, not the "
    "window, so the picture's centre reads 128 in a letterbox and at any "
    "zoom") {
  MouseInputMachine_t machine(mouse_as_joystick());
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
  REQUIRE(mouse_input_is_captured());

  MouseInputMachine_t::move(0.0F, 0.0F, 280.0F, 192.0F);
  CHECK(pread(0) == 128);
  CHECK(pread(1) == 128);

  MouseInputMachine_t::letterbox(280, 0, 560, 384);
  MouseInputMachine_t::move(0.0F, 0.0F, 560.0F, 192.0F);
  CHECK(pread(0) == 128);
  CHECK(pread(1) == 128);

  MouseInputMachine_t::letterbox(0, 0, 1120, 768);
  MouseInputMachine_t::move(0.0F, 0.0F, 560.0F, 384.0F);
  CHECK(pread(0) == 128);
  CHECK(pread(1) == 128);
}
#endif

#if defined(ENABLE_PERIPHERAL_KEYBOARD)

namespace {

constexpr uint16_t addr_keyboard_data = 0xC000;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint16_t addr_pushbutton0 = 0xC061;
constexpr uint8_t bit7 = 0x80;
constexpr uint16_t spin_address = 0x0300;
constexpr uint16_t probe_address = 0x0200;
constexpr uint16_t probe_result = 0x0010;
constexpr uint32_t probe_cycle_cap = 1000;
constexpr uint32_t ntsc_frame_cycles = 17030;
// A II Plus REPT period is 68,032 cycles, four frames: ten frames hold two and
// fall short of the //e's 32-frame repeat delay.
constexpr uint32_t rept_frames = 10;

// The model is process-wide and a harness-built machine leaves it behind.
struct Model_t {
  Apple2Type_t saved = current_apple2_type;
  explicit Model_t(Apple2Type_t type) { current_apple2_type = type; }
  ~Model_t() { current_apple2_type = saved; }
  Model_t(const Model_t&) = delete;
  auto operator=(const Model_t&) -> Model_t& = delete;
  Model_t(Model_t&&) = delete;
  auto operator=(Model_t&&) -> Model_t& = delete;
};

struct KeyMachine_t {
  Model_t model;
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;
  AppMode_t saved_mode;

  explicit KeyMachine_t(
      const TestFixtures::ScopedTestConfig_t::Description_t& description,
      Apple2Type_t type = A2TYPE_APPLE2EENHANCED)
      : model(type),
        config(description),
        core(config),
        saved_mode(system_state.mode) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK));
    peripheral_manager_init();
    linapple_register_peripherals();
    linapple_reset_hard();
    joy_frontend_initialize();
    keyboard_set_caps(true);
    keyboard_set_caps_mode(caps_mode_host);
    keyboard_set_mapping_mode(KBD_MODE_SYMBOLIC);
    keyboard_set_layout(keyboard_layout_us);
    linapple_set_rocker_switch(false);
    frontend_update_keyboard_mapping();
    system_state.mode = app_mode_running;
    g_buttondown = -1;
    SDL_SetModState(SDL_KMOD_NONE);
    settle();
  }

  ~KeyMachine_t() {
    keyboard_release_host_modifiers();
    settle();
    Configuration_t::instance().data.erase("Keyboard.Custom");
    keyboard_apply_custom_mappings();
    keyboard_set_caps(true);
    keyboard_set_caps_mode(caps_mode_host);
    SDL_SetModState(SDL_KMOD_NONE);
    g_buttondown = -1;
    system_state.mode = saved_mode;
    joy_frontend_shutdown();
    SDL_Quit();
  }

  KeyMachine_t(const KeyMachine_t&) = delete;
  auto operator=(const KeyMachine_t&) -> KeyMachine_t& = delete;
  KeyMachine_t(KeyMachine_t&&) = delete;
  auto operator=(KeyMachine_t&&) -> KeyMachine_t& = delete;

  // A think drains the command queue, as a running machine does once a frame.
  static auto settle() -> void { peripheral_manager_think(0); }

  static auto key(SDL_Scancode scancode, SDL_Keycode keycode, SDL_Keymod mod,
                  bool down) -> void {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.key = keycode;
    event.key.mod = mod;
    event.key.down = down;
    sdl_handle_event(&event);
    settle();
  }

  static auto focus(bool gained) -> void {
    SDL_Event event{};
    event.type =
        gained ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    sdl_handle_event(&event);
    settle();
  }

  static auto joystick_device(bool added) -> void {
    SDL_Event event{};
    event.type = added ? SDL_EVENT_JOYSTICK_ADDED : SDL_EVENT_JOYSTICK_REMOVED;
    sdl_handle_event(&event);
    settle();
  }

  static auto latch() -> uint8_t {
    return io_map_dispatch(0, addr_keyboard_data, 0, 0, 0);
  }

  static auto any_key_down() -> bool {
    return (io_map_dispatch(0, addr_keyboard_strobe, 0, 0, 0) & bit7) != 0;
  }

  // A read of $C010 clears the strobe on every model.
  static auto clear_strobe() -> void {
    (void)io_map_dispatch(0, addr_keyboard_strobe, 0, 0, 0);
  }

  static auto pushbutton(uint8_t line) -> int {
    const auto addr = static_cast<uint16_t>(addr_pushbutton0 + line);
    return (io_map_dispatch(0, addr, 0, 0, 0) & bit7) != 0 ? 1 : 0;
  }

  // The 6502 spins at $0300 while frames run, so no firmware reads the
  // keyboard behind the test's back.
  static auto run_frames(uint32_t count) -> void {
    const std::array<uint8_t, 3> spin = {0x4C, 0x00, 0x03};
    TestFixtures::ScopedCore_t::poke(spin_address, spin);
    TestFixtures::enter_at({spin_address, 0, 0, 0});
    for (uint32_t i = 0; i < count; ++i) {
      linapple_run_frame(ntsc_frame_cycles);
    }
  }

  // After frames a direct dispatch would hand the bus bridge a stale cycle
  // count, so the 6502 reads the keyboard itself: LDA $C000 / STA $10 / NOP,
  // or BIT $C010 / NOP.
  static auto stepped_latch() -> uint8_t {
    const std::array<uint8_t, 6> probe = {0xAD, 0x00, 0xC0, 0x85, 0x10, 0xEA};
    TestFixtures::ScopedCore_t::poke(probe_address, probe);
    TestFixtures::enter_at({probe_address, 0, 0, 0});
    TestFixtures::step_until_pc(probe_address + 5, probe_cycle_cap);
    REQUIRE(cpu_get_registers()->pc == probe_address + 5);
    return *mem_get_main_ptr(probe_result);
  }

  static auto stepped_clear_strobe() -> void {
    const std::array<uint8_t, 4> probe = {0x2C, 0x10, 0xC0, 0xEA};
    TestFixtures::ScopedCore_t::poke(probe_address, probe);
    TestFixtures::enter_at({probe_address, 0, 0, 0});
    TestFixtures::step_until_pc(probe_address + 3, probe_cycle_cap);
    REQUIRE(cpu_get_registers()->pc == probe_address + 3);
  }
};

auto custom_switches(TestFixtures::ScopedTestConfig_t::MachineType_t model)
    -> TestFixtures::ScopedTestConfig_t::Description_t {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.machine_type = model;
  description.extras.push_back({"Keyboard.Custom", "Tab", "OpenApple"});
  description.extras.push_back({"Keyboard.Custom", "Grave", "Rept"});
  return description;
}

}  // namespace

TEST_CASE(
    "SDL3 keys: A types $C1 with caps on and $E1 with it off, Left Alt is "
    "Open Apple and Right Alt Solid Apple, and a focus loss lets go of the key "
    "and the switch") {
  KeyMachine_t machine(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  REQUIRE(keyboard_get_caps());
  REQUIRE_FALSE(KeyMachine_t::any_key_down());

  KeyMachine_t::key(SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE, true);
  CHECK(KeyMachine_t::latch() == 0xC1);
  CHECK(KeyMachine_t::any_key_down());
  KeyMachine_t::key(SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE, false);
  CHECK_FALSE(KeyMachine_t::any_key_down());
  KeyMachine_t::clear_strobe();

  keyboard_set_caps(false);
  KeyMachine_t::key(SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE, true);
  CHECK(KeyMachine_t::latch() == 0xE1);
  KeyMachine_t::key(SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE, false);
  KeyMachine_t::clear_strobe();

  CHECK(KeyMachine_t::pushbutton(0) == 0);
  CHECK(KeyMachine_t::pushbutton(1) == 0);
  KeyMachine_t::key(SDL_SCANCODE_LALT, SDLK_LALT, SDL_KMOD_LALT, true);
  CHECK(KeyMachine_t::pushbutton(0) == 1);
  CHECK(KeyMachine_t::pushbutton(1) == 0);
  KeyMachine_t::key(SDL_SCANCODE_LALT, SDLK_LALT, SDL_KMOD_NONE, false);
  CHECK(KeyMachine_t::pushbutton(0) == 0);
  KeyMachine_t::key(SDL_SCANCODE_RALT, SDLK_RALT, SDL_KMOD_RALT, true);
  CHECK(KeyMachine_t::pushbutton(0) == 0);
  CHECK(KeyMachine_t::pushbutton(1) == 1);
  KeyMachine_t::key(SDL_SCANCODE_RALT, SDLK_RALT, SDL_KMOD_NONE, false);
  CHECK(KeyMachine_t::pushbutton(1) == 0);
  // An Alt key is a switch, not a matrix key.
  CHECK_FALSE(KeyMachine_t::any_key_down());

  KeyMachine_t::key(SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE, true);
  KeyMachine_t::key(SDL_SCANCODE_LALT, SDLK_LALT, SDL_KMOD_LALT, true);
  REQUIRE(KeyMachine_t::any_key_down());
  REQUIRE(KeyMachine_t::pushbutton(0) == 1);
  KeyMachine_t::focus(false);
  CHECK_FALSE(KeyMachine_t::any_key_down());
  CHECK(KeyMachine_t::pushbutton(0) == 0);
  // The releases the window manager ate change nothing when they arrive.
  KeyMachine_t::key(SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE, false);
  KeyMachine_t::key(SDL_SCANCODE_LALT, SDLK_LALT, SDL_KMOD_NONE, false);
  CHECK_FALSE(KeyMachine_t::any_key_down());
  CHECK(KeyMachine_t::pushbutton(0) == 0);
}

TEST_CASE(
    "SDL3 keys: a custom Tab is Open Apple and types nothing, and a custom "
    "Grave is the REPT key, which repeats a held key on a II Plus and does "
    "nothing on a //e") {
  using TestConfig_t = TestFixtures::ScopedTestConfig_t;

  SUBCASE("Enhanced //e") {
    KeyMachine_t machine(
        custom_switches(TestConfig_t::machine_apple2e_enhanced));
    KeyMachine_t::key(SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE, true);
    REQUIRE(KeyMachine_t::latch() == 0xC1);
    KeyMachine_t::clear_strobe();

    KeyMachine_t::key(SDL_SCANCODE_TAB, SDLK_TAB, SDL_KMOD_NONE, true);
    CHECK(KeyMachine_t::pushbutton(0) == 1);
    CHECK(KeyMachine_t::latch() == 0x41);
    KeyMachine_t::key(SDL_SCANCODE_TAB, SDLK_TAB, SDL_KMOD_NONE, false);
    CHECK(KeyMachine_t::pushbutton(0) == 0);

    KeyMachine_t::key(SDL_SCANCODE_GRAVE, SDLK_GRAVE, SDL_KMOD_NONE, true);
    CHECK(KeyMachine_t::latch() == 0x41);
    KeyMachine_t::run_frames(rept_frames);
    CHECK(KeyMachine_t::stepped_latch() == 0x41);
    KeyMachine_t::key(SDL_SCANCODE_GRAVE, SDLK_GRAVE, SDL_KMOD_NONE, false);
    KeyMachine_t::key(SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE, false);
  }

  SUBCASE("II Plus") {
    KeyMachine_t machine(custom_switches(TestConfig_t::machine_apple2_plus),
                         A2TYPE_APPLE2PLUS);
    KeyMachine_t::key(SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE, true);
    REQUIRE(KeyMachine_t::latch() == 0xC1);
    KeyMachine_t::clear_strobe();
    REQUIRE((KeyMachine_t::latch() & bit7) == 0);

    // The first REPT strobe comes one period after the press, so the press
    // itself latches nothing.
    KeyMachine_t::key(SDL_SCANCODE_GRAVE, SDLK_GRAVE, SDL_KMOD_NONE, true);
    CHECK(KeyMachine_t::latch() == 0x41);
    KeyMachine_t::run_frames(rept_frames);
    CHECK(KeyMachine_t::stepped_latch() == 0xC1);

    KeyMachine_t::stepped_clear_strobe();
    KeyMachine_t::key(SDL_SCANCODE_GRAVE, SDLK_GRAVE, SDL_KMOD_NONE, false);
    KeyMachine_t::run_frames(rept_frames);
    CHECK(KeyMachine_t::stepped_latch() == 0x41);
    KeyMachine_t::key(SDL_SCANCODE_A, SDLK_A, SDL_KMOD_NONE, false);
  }
}

TEST_CASE(
    "SDL3 keys: in host mode caps follows the host's lock state on focus gain "
    "and at the key's edges, and in emulated mode a press toggles it") {
  KeyMachine_t machine(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  REQUIRE(keyboard_get_caps_mode() == caps_mode_host);

  keyboard_set_caps(false);
  SDL_SetModState(SDL_KMOD_CAPS);
  KeyMachine_t::focus(true);
  CHECK(keyboard_get_caps());
  SDL_SetModState(SDL_KMOD_NONE);
  KeyMachine_t::focus(true);
  CHECK_FALSE(keyboard_get_caps());

  // SDL reports the lock's new state in the event's modifiers.
  KeyMachine_t::key(SDL_SCANCODE_CAPSLOCK, SDLK_CAPSLOCK, SDL_KMOD_CAPS, true);
  CHECK(keyboard_get_caps());
  KeyMachine_t::key(SDL_SCANCODE_CAPSLOCK, SDLK_CAPSLOCK, SDL_KMOD_NONE, false);
  CHECK_FALSE(keyboard_get_caps());

  keyboard_set_caps_mode(caps_mode_emulated);
  KeyMachine_t::key(SDL_SCANCODE_CAPSLOCK, SDLK_CAPSLOCK, SDL_KMOD_NONE, true);
  CHECK(keyboard_get_caps());
  KeyMachine_t::key(SDL_SCANCODE_CAPSLOCK, SDLK_CAPSLOCK, SDL_KMOD_NONE, false);
  CHECK(keyboard_get_caps());
  KeyMachine_t::key(SDL_SCANCODE_CAPSLOCK, SDLK_CAPSLOCK, SDL_KMOD_NONE, true);
  CHECK_FALSE(keyboard_get_caps());
  // The host's lock state is not the emulated key's business.
  SDL_SetModState(SDL_KMOD_CAPS);
  KeyMachine_t::focus(true);
  CHECK_FALSE(keyboard_get_caps());
}

TEST_CASE(
    "SDL3 keys: a configured second joystick that is not plugged in leaves "
    "PB2 open at rest and after a joystick added and removed event") {
  TestFixtures::ScopedTestConfig_t::Description_t description =
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only();
  description.extras.push_back({"Configuration", "Joystick 1", "1"});
  KeyMachine_t machine(description);
  CHECK(KeyMachine_t::pushbutton(0) == 0);
  CHECK(KeyMachine_t::pushbutton(1) == 0);
  CHECK(KeyMachine_t::pushbutton(2) == 1);
  KeyMachine_t::joystick_device(true);
  CHECK(KeyMachine_t::pushbutton(2) == 1);
  KeyMachine_t::joystick_device(false);
  CHECK(KeyMachine_t::pushbutton(2) == 1);
  CHECK(KeyMachine_t::pushbutton(0) == 0);
}

TEST_CASE(
    "SDL3 keys: Alt+1 with the shipped quick-save modifier names a snapshot "
    "slot and reaches no card") {
  KeyMachine_t machine(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  KeyMachine_t::key(SDL_SCANCODE_1, SDLK_1, SDL_KMOD_LALT, true);
  CHECK((KeyMachine_t::latch() & bit7) == 0);
  CHECK_FALSE(KeyMachine_t::any_key_down());
  CHECK(std::string(save_state_get_filename()).find("SaveState1.aws") !=
        std::string::npos);
  KeyMachine_t::key(SDL_SCANCODE_1, SDLK_1, SDL_KMOD_LALT, false);
  CHECK_FALSE(KeyMachine_t::any_key_down());
}

#endif

#if defined(ENABLE_PERIPHERAL_HARDDISK)

namespace {

constexpr int harddisk_test_slot = 5;
constexpr const char* harddisk_image_key = "Harddisk Image 1";

auto harddisk_in_slot_5() -> TestFixtures::ScopedTestConfig_t::Description_t {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[harddisk_test_slot - 1] = "Harddisk";
  return description;
}

auto harddisk_status() -> HarddiskStatus_t {
  HarddiskStatus_t out{};
  size_t size = sizeof(out);
  REQUIRE(peripheral_query(harddisk_test_slot, harddisk_query_status, &out,
                           &size) == peripheral_ok);
  return out;
}

auto harddisk_saved_key() -> std::string {
  return Configuration_t::instance().get_string("Preferences",
                                                harddisk_image_key);
}

constexpr uint16_t harddisk_io_base = 0xC080 + (harddisk_test_slot << 4);

// READ block 1 through the registers, as the firmware drives them.
auto harddisk_read_block_1() -> void {
  io_map_dispatch(0, harddisk_io_base + 1, 1, harddisk_test_slot << 4, 0);
  io_map_dispatch(0, harddisk_io_base + 2, 1, 0x01, 0);
  io_map_dispatch(0, harddisk_io_base + 3, 1, 0x00, 0);
  io_map_dispatch(0, harddisk_io_base + 0, 1, 0x01, 0);
  REQUIRE(io_map_dispatch(0, harddisk_io_base + 0, 0, 0, 0) == 0x00);
}

auto harddisk_drain_data_port() -> void {
  for (int i = 0; i < 512; ++i) {
    io_map_dispatch(0, harddisk_io_base + 4, 0, 0, 0);
  }
}

constexpr char lamp_base = 1;
constexpr int lamp_harddisk = 2;

}  // namespace

TEST_CASE(
    "SDL3 hard disk: Ctrl+Shift+F3 ejects drive 1 of the card wherever the "
    "manager finds it and the key is persisted empty; with no card the "
    "chooser does not open and the log says so") {
  SUBCASE("the card in slot 5") {
    KeyMachine_t machine(harddisk_in_slot_5());
    harddisk_frontend_initialize();
    REQUIRE(harddisk_frontend_slot() == harddisk_test_slot);
    const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
    REQUIRE(harddisk_frontend_insert(0, image.c_str(), false) == 0);
    REQUIRE(harddisk_status().drive0_loaded == 1);
    REQUIRE(harddisk_saved_key() == image.path());

    const SDL_Keymod chord = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
    KeyMachine_t::key(SDL_SCANCODE_F3, SDLK_F3, chord, true);
    KeyMachine_t::key(SDL_SCANCODE_F3, SDLK_F3, chord, false);
    CHECK(harddisk_status().drive0_loaded == 0);
    CHECK(harddisk_saved_key().empty());
  }

  SUBCASE("no card anywhere") {
    KeyMachine_t machine(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
    harddisk_frontend_initialize();
    REQUIRE(harddisk_frontend_slot() == harddisk_frontend_no_card);
    TestFixtures::ScopedLogCapture_t log;
    KeyMachine_t::key(SDL_SCANCODE_F3, SDLK_F3, SDL_KMOD_SHIFT, true);
    KeyMachine_t::key(SDL_SCANCODE_F3, SDLK_F3, SDL_KMOD_SHIFT, false);
    CHECK(log.count_containing("no hard disk is installed") == 1);
  }
}

TEST_CASE(
    "SDL3 hard disk: the lamp lights on the frame after a read, shows prot "
    "for a loaded protected drive, and goes out once the hold has run") {
  KeyMachine_t machine(harddisk_in_slot_5());
  harddisk_frontend_initialize();
  REQUIRE(harddisk_frontend_slot() == harddisk_test_slot);
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  REQUIRE(harddisk_frontend_insert(0, image.c_str(), false) == 0);
  frame_refresh_status(draw_leds);
  REQUIRE(frame_status_led(lamp_harddisk) == lamp_base + harddisk_status_off);

  harddisk_read_block_1();
  harddisk_drain_data_port();
  frame_refresh();
  CHECK(frame_status_led(lamp_harddisk) == lamp_base + harddisk_status_read);
  CHECK(g_status_cycle == k_show_cycles);

  // Nothing happened, so the frame leaves the lamp as it was.
  frame_refresh();
  CHECK(frame_status_led(lamp_harddisk) == lamp_base + harddisk_status_read);

  g_status_cycle = 0;
  frame_refresh();
  CHECK(frame_status_led(lamp_harddisk) == lamp_base + harddisk_status_off);

  HarddiskSetProtectCmd_t protect{};
  protect.drive = harddisk_drive_0;
  protect.write_protected = 1;
  REQUIRE(peripheral_command(harddisk_test_slot, harddisk_cmd_set_protect,
                             &protect, sizeof(protect)) == peripheral_ok);
  KeyMachine_t::settle();
  CHECK(frame_status_led(lamp_harddisk) == lamp_base + harddisk_status_prot);

  harddisk_read_block_1();
  harddisk_drain_data_port();
  frame_refresh();
  CHECK(frame_status_led(lamp_harddisk) == lamp_base + harddisk_status_read);
  g_status_cycle = 0;
  frame_refresh();
  CHECK(frame_status_led(lamp_harddisk) == lamp_base + harddisk_status_prot);
}

#endif
