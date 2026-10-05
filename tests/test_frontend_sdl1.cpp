// SPDX-License-Identifier: GPL-2.0-only
#include <SDL/SDL.h>
#include <stdlib.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_keysym.h"
#include "SDL_mouse.h"
#include "SDL_stdinc.h"
#include "SDL_video.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/Asset.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/MouseFrontend.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/common/sdl/MouseInput.h"
#include "frontends/sdl1/DiskChoose.h"
#include "frontends/sdl1/Frame.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

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
  AppConfig_t config{};
  app_config_default(&config);

  CHECK(!config.is_fullscreen);
  CHECK(!config.is_benchmark);
}

TEST_CASE("SDL1 Frontend Initialization and Screen Scaling") {
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  // Set scaled screen resolution (e.g. Screen Factor = 2 => 1120x768)
  system_state.screen_width = 1120;
  system_state.screen_height = 768;

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
  frame_show_help_screen(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height));

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
  frame_show_help_screen(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height));

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
  system_state.screen_width = 1680;
  system_state.screen_height = 1152;

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

  frame_show_help_screen(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height));

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
  key_event.type = SDL_KEYDOWN;
  key_event.key.keysym.sym = SDLK_SPACE;
  REQUIRE(SDL_PushEvent(&key_event) == 0);

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

  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Disk Chooser Modal Outline Borders Rendered") {
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  system_state.screen_width = 560;
  system_state.screen_height = 384;

  int win_result = frame_create_window();
  REQUIRE(win_result == 0);
  REQUIRE(g_screen != nullptr);

  // Set up disk choose state
  g_diskChooseState.active = true;
  g_diskChooseState.slot = 6;
  g_diskChooseState.bg_screen.reset(SDL_CreateRGBSurface(
      0, 560, 384, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0));
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

  system_state.mode = app_mode_running;

  SDL_Event key_event{};
  key_event.type = SDL_KEYDOWN;
  key_event.key.keysym.sym = SDLK_F12;
  key_event.key.state = SDL_PRESSED;
  int push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result == 0);

  frame_show_help_screen(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height));

  CHECK(system_state.mode == app_mode_exit);

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

  system_state.mode = app_mode_running;

  // Push an SDL_QUIT event into the event queue
  SDL_Event quit_event{};
  quit_event.type = SDL_QUIT;
  int push_result = SDL_PushEvent(&quit_event);
  REQUIRE(push_result == 0);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(system_state.screen_width),
                                static_cast<int>(system_state.screen_height),
                                ".", 6, filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(system_state.mode == app_mode_exit);

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

  system_state.mode = app_mode_running;

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
  bool chosen = choose_an_image(static_cast<int>(system_state.screen_width),
                                static_cast<int>(system_state.screen_height),
                                ".", 6, filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(system_state.mode == app_mode_running);

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

  system_state.mode = app_mode_running;

  SDL_Event key_event{};
  key_event.type = SDL_KEYDOWN;
  key_event.key.keysym.sym = SDLK_F12;
  key_event.key.state = SDL_PRESSED;
  int push_result = SDL_PushEvent(&key_event);
  REQUIRE(push_result == 0);

  std::string filename;
  bool isdir = false;
  size_t index_file = 0;
  bool chosen = choose_an_image(static_cast<int>(system_state.screen_width),
                                static_cast<int>(system_state.screen_height),
                                ".", 6, filename, isdir, index_file);
  CHECK(!chosen);
  CHECK(system_state.mode == app_mode_exit);

  SDL_Event polled_event{};
  int count = SDL_PeepEvents(&polled_event, 1, SDL_GETEVENT, SDL_ALLEVENTS);
  CHECK(count == 1);
  CHECK(polled_event.type == SDL_QUIT);

  // Teardown
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Joystick Config Out-of-Range Handling") {
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);

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
  CHECK_NOTHROW(joy_frontend_process_key(SDLK_KP1, false, true, false));

  joy_frontend_shutdown();
  SDL_Quit();
}

namespace {

// An Enhanced //e built and reset as the frontend builds it, its pointer
// released and its window a plain 560 x 384, the machine running.
struct MouseInputMachine_t {
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;
  AppMode_t saved_mode;

  explicit MouseInputMachine_t(
      const TestFixtures::ScopedTestConfig_t::Description_t& description)
      : config(description), core(config), saved_mode(system_state.mode) {
    SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
    REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) == 0);
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
    SDL_SetModState(KMOD_NONE);
  }

  ~MouseInputMachine_t() {
    SDL_SetModState(KMOD_NONE);
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
    event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    event.button.button = button;
    event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    sdl_handle_event(&event);
  }

  static auto move(Sint16 dx, Sint16 dy, Uint16 x, Uint16 y) -> void {
    SDL_Event event{};
    event.type = SDL_MOUSEMOTION;
    event.motion.xrel = dx;
    event.motion.yrel = dy;
    event.motion.x = x;
    event.motion.y = y;
    sdl_handle_event(&event);
  }
};

auto mouse_as_joystick() -> TestFixtures::ScopedTestConfig_t::Description_t {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.extras.push_back({"Configuration", "Joystick 0", "4"});
  return description;
}

}  // namespace

TEST_CASE(
    "SDL1 mouse capture: with no mouse card and no mouse-emulated joystick "
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
    "SDL1 mouse capture: a mouse-emulated joystick is a consumer, so a left "
    "click captures and a Shift-click releases") {
  MouseInputMachine_t machine(mouse_as_joystick());
  REQUIRE_FALSE(mouse_frontend_card_present());
  REQUIRE(joy_frontend_is_mouse_emulation_active());

  MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
  CHECK(mouse_input_is_captured());

  SDL_SetModState(KMOD_LSHIFT);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
  CHECK_FALSE(mouse_input_is_captured());
}

#if defined(ENABLE_PERIPHERAL_MOUSE)

namespace {

constexpr size_t mouse_frame_size = 92;
constexpr size_t mouse_frame_position_x = 8;
constexpr size_t mouse_frame_max_x = 20;
constexpr size_t mouse_frame_max_y = 28;
constexpr size_t mouse_frame_parser_out_len = 52;
constexpr size_t mouse_frame_orb = 57;
constexpr size_t mouse_frame_ddrb = 59;
constexpr size_t mouse_frame_port_b_shadow = 73;
constexpr size_t mouse_frame_mode = 74;

using MouseFrame_t = std::array<uint8_t, mouse_frame_size>;

// The card counts host motion only while its mode byte says so; the loader
// is the one public path that sets that byte without running the firmware.
auto turn_mouse_tracking_on(int slot) -> void {
  MouseFrame_t frame{};
  frame.at(0) = MOUSE_STATE_VERSION;
  frame.at(4) = mouse_frame_size;
  frame.at(mouse_frame_max_x) = 0xFF;
  frame.at(mouse_frame_max_x + 1) = 0x03;
  frame.at(mouse_frame_max_y) = 0xFF;
  frame.at(mouse_frame_max_y + 1) = 0x03;
  frame.at(mouse_frame_parser_out_len) = 1;
  // Port B at rest as a real build saves it: PB6 answers the lowered PB4.
  frame.at(mouse_frame_orb) = 0x40;
  frame.at(mouse_frame_ddrb) = 0x3E;
  frame.at(mouse_frame_port_b_shadow) = 0x40;
  frame.at(mouse_frame_mode) = 1;
  REQUIRE(peripheral_load_state(slot, frame.data(), frame.size()) ==
          peripheral_ok);
}

auto mouse_position_x(int slot) -> int16_t {
  peripheral_manager_think(0);
  MouseFrame_t frame{};
  size_t size = frame.size();
  peripheral_save_state(slot, frame.data(), &size);
  REQUIRE(size == frame.size());
  return static_cast<int16_t>(frame.at(mouse_frame_position_x) |
                              (frame.at(mouse_frame_position_x + 1) << 8));
}

auto mouse_in_slot_4() -> TestFixtures::ScopedTestConfig_t::Description_t {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[3] = "Mouse Interface";
  return description;
}

}  // namespace

TEST_CASE(
    "SDL1 mouse capture: with a mouse card in slot 4 the first left click "
    "captures, a toolbar key or a pause refuses it, the middle button "
    "toggles, and a Shift-click releases") {
  MouseInputMachine_t machine(mouse_in_slot_4());
  REQUIRE(mouse_frontend_card_slot() == 4);

  SUBCASE("the first left click captures and a Shift-click releases") {
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
    MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
    CHECK(mouse_input_is_captured());
    SDL_SetModState(KMOD_LSHIFT);
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
    "SDL1 mouse capture: Mouse Capture = 0 refuses the left and the middle "
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
    "SDL1 mouse motion: a captured motion event reaches the card as counts "
    "scaled to the window") {
  MouseInputMachine_t machine(mouse_in_slot_4());
  turn_mouse_tracking_on(4);
  REQUIRE(mouse_position_x(4) == 0);

  MouseInputMachine_t::move(2, 0, 2, 0);
  CHECK(mouse_position_x(4) == 0);

  MouseInputMachine_t::click(SDL_BUTTON_LEFT, true);
  MouseInputMachine_t::click(SDL_BUTTON_LEFT, false);
  REQUIRE(mouse_input_is_captured());

  MouseInputMachine_t::move(2, 0, 4, 0);
  CHECK(mouse_position_x(4) == 1);
  MouseInputMachine_t::move(1, 0, 5, 0);
  CHECK(mouse_position_x(4) == 1);
  MouseInputMachine_t::move(1, 0, 6, 0);
  CHECK(mouse_position_x(4) == 2);
}
#endif
