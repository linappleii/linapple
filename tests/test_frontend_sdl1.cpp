#include <SDL/SDL.h>
#include <stdlib.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "SDL_events.h"
#include "SDL_keysym.h"
#include "SDL_stdinc.h"
#include "SDL_video.h"
#include "apple2/Video.h"
#include "core/Asset.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/Frontend.h"
#include "frontends/sdl1/DiskChoose.h"
#include "frontends/sdl1/Frame.h"

auto ds_init() -> bool { return true; }
auto ds_shutdown() -> void {}
extern DiskChooseState_t g_diskChooseState;

TEST_CASE("SDL1 Frontend Initialization") {
  // Test that SDL 1.2 initialization completes successfully with dummy video
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  CHECK(init_result == 0);

  // Clean up
  SDL_Quit();
}

TEST_CASE("SDL1 Config Validation") {
  AppConfig_t config;
  config.is_fullscreen = true;
  config.is_benchmark = false;

  CHECK(config.is_fullscreen == true);
  CHECK(config.is_benchmark == false);
}

TEST_CASE("SDL1 Frontend Initialization and Screen Scaling") {
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  // Set scaled screen resolution (e.g. Screen Factor = 2 => 1120x768)
  g_state.screen_width = 1120;
  g_state.screen_height = 768;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  CHECK(g_screen->w == 1120);
  CHECK(g_screen->h == 768);
  CHECK(g_window_resized == true);

  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Fullscreen Toggle Preserves Scaled Dimensions") {
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
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

  // 3. Return to windowed mode (F6)
  set_normal_mode();

  // Windowed mode must restore original configured dimensions and full rect
  CHECK(g_state.screen_width == 1120);
  CHECK(g_state.screen_height == 768);
  CHECK(g_screen->w == 1120);
  CHECK(g_screen->h == 768);
  CHECK(g_new_rect.w == 1120);
  CHECK(g_new_rect.h == 768);
  CHECK(g_new_rect.x == 0);
  CHECK(g_new_rect.y == 0);

  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Help Screen Quit Event Handling") {
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Push an SDL_QUIT event into the event queue (0 is success in SDL 1.2)
  SDL_Event quit_event{};
  quit_event.type = SDL_QUIT;
  int push_result = SDL_PushEvent(&quit_event);
  REQUIRE(push_result == 0);

  // frame_show_help_screen should not hang or discard the quit event
  frame_show_help_screen(static_cast<int>(g_state.screen_width),
                         static_cast<int>(g_state.screen_height));

  // Verify that SDL_QUIT was re-pushed and is available in the event queue
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_ALLEVENTS);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_QUIT);

  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Help Screen Key Down Dismissal") {
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Push an SDL_KEYDOWN event into the event queue (0 is success in SDL 1.2)
  SDL_Event key_event{};
  key_event.type = SDL_KEYDOWN;
  key_event.key.keysym.sym = SDLK_SPACE;
  key_event.key.state = SDL_PRESSED;
  int push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result == 0);

  // frame_show_help_screen should immediately consume the key event and dismiss
  frame_show_help_screen(static_cast<int>(g_state.screen_width),
                         static_cast<int>(g_state.screen_height));

  // Verify that the event queue is drained
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_ALLEVENTS);
  CHECK(count == 0);

  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Help Screen Scaling at High Screen Factors") {
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  // Screen Factor = 3 => 1680x1152
  g_state.screen_width = 1680;
  g_state.screen_height = 1152;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Set distinct test pixel in video output buffer
  uint32_t* output = video_get_output_buffer();
  REQUIRE(output != nullptr);
  output[0] = 0x00FF0000;  // Red

  // Queue a keydown event so frame_show_help_screen exits immediately after
  // rendering
  SDL_Event key_event{};
  key_event.type = SDL_KEYDOWN;
  key_event.key.keysym.sym = SDLK_ESCAPE;
  REQUIRE(SDL_PushEvent(&key_event) == 0);

  frame_show_help_screen(static_cast<int>(g_state.screen_width),
                         static_cast<int>(g_state.screen_height));

  // Verify that after dismissal, g_screen is properly restored with the
  // emulator frame
  const auto* screen_pixels =
      reinterpret_cast<const uint32_t*>(g_screen->pixels);
  CHECK(screen_pixels[0] == 0x00FF0000);

  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE(
    "SDL1 Frontend Help Screen Dismissal Clears Fullscreen Pillarbox Margins") {
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  g_state.screen_width = 1120;
  g_state.screen_height = 768;

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
  key_event.type = SDL_KEYDOWN;
  key_event.key.keysym.sym = SDLK_SPACE;
  REQUIRE(SDL_PushEvent(&key_event) == 0);

  frame_show_help_screen(static_cast<int>(g_state.screen_width),
                         static_cast<int>(g_state.screen_height));

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

  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Disk Chooser Modal Outline Borders Rendered") {
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  g_state.screen_width = 560;
  g_state.screen_height = 384;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Set up disk choose state
  g_diskChooseState.active = true;
  g_diskChooseState.slot = 6;
  g_diskChooseState.bg_screen = SDL_CreateRGBSurface(
      0, 560, 384, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
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
  if (g_diskChooseState.bg_screen != nullptr) {
    SDL_FreeSurface(g_diskChooseState.bg_screen);
    g_diskChooseState.bg_screen = nullptr;
  }
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Help Screen F12 Event Handling") {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  g_state.mode = MODE_RUNNING;

  SDL_Event key_event{};
  key_event.type = SDL_KEYDOWN;
  key_event.key.keysym.sym = SDLK_F12;
  key_event.key.state = SDL_PRESSED;
  int push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result == 0);

  frame_show_help_screen(static_cast<int>(g_state.screen_width),
                         static_cast<int>(g_state.screen_height));

  CHECK(g_state.mode == MODE_EXIT);

  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_ALLEVENTS);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_QUIT);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Disk Choose Quit Event Handling") {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  g_state.mode = MODE_RUNNING;

  // Push an SDL_QUIT event into the event queue
  SDL_Event quit_event{};
  quit_event.type = SDL_QUIT;
  int push_result = SDL_PushEvent(&quit_event);
  REQUIRE(push_result == 0);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(g_state.screen_width),
                                static_cast<int>(g_state.screen_height), ".", 6,
                                filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(g_state.mode == MODE_EXIT);

  // Verify that SDL_QUIT was re-pushed and is available in the event queue
  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_ALLEVENTS);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_QUIT);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Disk Choose Key Down Dismissal") {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  g_state.mode = MODE_RUNNING;

  // Push an ESCAPE key down event into the event queue
  SDL_Event key_event{};
  key_event.type = SDL_KEYDOWN;
  key_event.key.keysym.sym = SDLK_ESCAPE;
  key_event.key.state = SDL_PRESSED;
  int push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result == 0);

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
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_ALLEVENTS);
  CHECK(count == 0);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Disk Choose F12 Event Handling") {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  AppConfig_t config{};
  app_config_default(&config);

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  g_state.mode = MODE_RUNNING;

  SDL_Event key_event{};
  key_event.type = SDL_KEYDOWN;
  key_event.key.keysym.sym = SDLK_F12;
  key_event.key.state = SDL_PRESSED;
  int push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result == 0);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(g_state.screen_width),
                                static_cast<int>(g_state.screen_height), ".", 6,
                                filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(g_state.mode == MODE_EXIT);

  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_ALLEVENTS);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_QUIT);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}
