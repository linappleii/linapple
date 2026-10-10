// SPDX-License-Identifier: GPL-2.0-only
#include <SDL/SDL.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "SDL_active.h"
#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_keysym.h"
#include "SDL_mouse.h"
#include "SDL_stdinc.h"
#include "SDL_video.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
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
#include "frontends/sdl1/DiskChoose.h"
#include "frontends/sdl1/Frame.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

auto ds_init() -> bool { return true; }
auto ds_shutdown() -> void {}

TEST_CASE("SDL1 Frontend Initialization") {
  // Test that SDL 1.2 initialization completes successfully with dummy video
  SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  CHECK(init_result == 0);

  // Clean up
  SDL_Quit();
}

TEST_CASE("SDL1 Config Validation") {
  AppConfig config{};
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

  AppConfig config{};
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

  AppConfig config{};
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
      if (screen_pixels[y * pitch_pixels + x] != 0) {
        nonzero_left_margin++;
      }
    }
  }
  CHECK(nonzero_left_margin == 0);

  int nonzero_right_margin = 0;
  for (int y = 0; y < 1080; ++y) {
    for (int x = 1747; x < 1920; ++x) {
      if (screen_pixels[y * pitch_pixels + x] != 0) {
        nonzero_right_margin++;
      }
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
  g_disk_choose_state.active = true;
  g_disk_choose_state.slot = 6;
  g_disk_choose_state.bg_screen.reset(SDL_CreateRGBSurface(
      0, 560, 384, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0));
  g_disk_choose_state.list_handle = nullptr;

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
  g_disk_choose_state.active = false;
  g_disk_choose_state.bg_screen.reset();
  frame_destroy_window();
  asset_quit();
  SDL_Quit();
}

TEST_CASE("SDL1 Frontend Help Screen F12 Event Handling") {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  int init_result = SDL_Init(SDL_INIT_VIDEO);
  REQUIRE(init_result == 0);
  REQUIRE(asset_init());

  AppConfig config{};
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

  AppConfig config{};
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

  AppConfig config{};
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

  AppConfig config{};
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
  save(cfg_joy_type1, 9999);
  save(cfg_joy_type2, 8888);

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

struct MouseInputMachine_t {
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;
  AppMode saved_mode;

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

// The loader is the one public path that turns tracking on without running
// the firmware.
auto turn_mouse_tracking_on(int slot) -> void {
  MouseFrame_t frame{};
  frame.at(0) = MOUSE_STATE_VERSION;
  frame.at(4) = mouse_frame_size;
  frame.at(mouse_frame_max_x) = 0xFF;
  frame.at(mouse_frame_max_x + 1) = 0x03;
  frame.at(mouse_frame_max_y) = 0xFF;
  frame.at(mouse_frame_max_y + 1) = 0x03;
  frame.at(mouse_frame_parser_out_len) = 1;
  // Port B at rest: PB6 answers the lowered PB4.
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
    g_buttondown = btn_help;
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

#if defined(ENABLE_PERIPHERAL_KEYBOARD)

extern auto sdl1_x11_keycode_to_hid(uint8_t keycode) -> uint8_t;

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
// X11 reports a key as its evdev code plus the server's minimum keycode.
constexpr int x11_min_keycode = 8;

// The model is process-wide and a harness-built machine leaves it behind.
struct Model_t {
  Apple2Type saved = current_apple2_type;
  explicit Model_t(Apple2Type type) { current_apple2_type = type; }
  ~Model_t() { current_apple2_type = saved; }
  Model_t(const Model_t&) = delete;
  auto operator=(const Model_t&) -> Model_t& = delete;
  Model_t(Model_t&&) = delete;
  auto operator=(Model_t&&) -> Model_t& = delete;
};

// Under the dummy video driver the usage comes from the keysym, so a
// constructed event's scancode is whatever the case says.
struct KeyMachine_t {
  Model_t model;
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;
  AppMode saved_mode;

  explicit KeyMachine_t(
      const TestFixtures::ScopedTestConfig_t::Description_t& description,
      Apple2Type type = A2TYPE_APPLE2EENHANCED)
      : model(type),
        config(description),
        core(config),
        saved_mode(system_state.mode) {
    SDL_putenv(const_cast<char*>("SDL_VIDEODRIVER=dummy"));
    REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) == 0);
    peripheral_manager_init();
    linapple_register_peripherals();
    linapple_reset_hard();
    joy_frontend_initialize();
    keyboard_set_caps(true);
    keyboard_set_caps_mode(caps_mode_host);
    keyboard_set_mapping_mode(kbd_mode_symbolic);
    keyboard_set_layout(keyboard_layout_us);
    linapple_set_rocker_switch(false);
    frontend_update_keyboard_mapping();
    system_state.mode = app_mode_running;
    g_buttondown = -1;
    SDL_SetModState(KMOD_NONE);
    settle();
  }

  ~KeyMachine_t() {
    keyboard_release_host_modifiers();
    settle();
    Configuration::instance().data.erase("Keyboard.Custom");
    keyboard_apply_custom_mappings();
    keyboard_set_caps(true);
    keyboard_set_caps_mode(caps_mode_host);
    keyboard_set_mapping_mode(kbd_mode_symbolic);
    SDL_SetModState(KMOD_NONE);
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

  static auto key(uint8_t scancode, SDLKey keycode, SDLMod mod, bool down)
      -> void {
    SDL_Event event{};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.scancode = scancode;
    event.key.keysym.sym = keycode;
    event.key.keysym.mod = mod;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    sdl_handle_event(&event);
    settle();
  }

  static auto focus(bool gained) -> void {
    SDL_Event event{};
    event.type = SDL_ACTIVEEVENT;
    event.active.state = SDL_APPINPUTFOCUS;
    event.active.gain = gained ? 1 : 0;
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

auto positional() -> TestFixtures::ScopedTestConfig_t::Description_t {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.extras.push_back({"Keyboard", "Mapping Mode", "1"});
  return description;
}

// The kernel's evdev code and the usage its HID driver assigns it
// (drivers/hid/hid-input.c, hid_keyboard[]).
struct KernelUsage_t {
  int evdev;
  int usage;
};

constexpr int usage_non_us_hash = 50;
constexpr int usage_sysrq = 70;
constexpr int usage_scroll_lock = 71;
constexpr int usage_pause = 72;
constexpr int usage_insert = 73;
constexpr int usage_home = 74;
constexpr int usage_page_up = 75;
constexpr int usage_delete = 76;
constexpr int usage_end = 77;
constexpr int usage_page_down = 78;

constexpr std::array<KernelUsage_t, 78> kernel_usages = {{
    {KEY_A, keyb_idx_a},
    {KEY_B, keyb_idx_b},
    {KEY_C, keyb_idx_c},
    {KEY_D, keyb_idx_d},
    {KEY_E, keyb_idx_e},
    {KEY_F, keyb_idx_f},
    {KEY_G, keyb_idx_g},
    {KEY_H, keyb_idx_h},
    {KEY_I, keyb_idx_i},
    {KEY_J, keyb_idx_j},
    {KEY_K, keyb_idx_k},
    {KEY_L, keyb_idx_l},
    {KEY_M, keyb_idx_m},
    {KEY_N, keyb_idx_n},
    {KEY_O, keyb_idx_o},
    {KEY_P, keyb_idx_p},
    {KEY_Q, keyb_idx_q},
    {KEY_R, keyb_idx_r},
    {KEY_S, keyb_idx_s},
    {KEY_T, keyb_idx_t},
    {KEY_U, keyb_idx_u},
    {KEY_V, keyb_idx_v},
    {KEY_W, keyb_idx_w},
    {KEY_X, keyb_idx_x},
    {KEY_Y, keyb_idx_y},
    {KEY_Z, keyb_idx_z},
    {KEY_1, keyb_idx_1},
    {KEY_2, keyb_idx_2},
    {KEY_3, keyb_idx_3},
    {KEY_4, keyb_idx_4},
    {KEY_5, keyb_idx_5},
    {KEY_6, keyb_idx_6},
    {KEY_7, keyb_idx_7},
    {KEY_8, keyb_idx_8},
    {KEY_9, keyb_idx_9},
    {KEY_0, keyb_idx_0},
    {KEY_ENTER, keyb_idx_return},
    {KEY_ESC, keyb_idx_escape},
    {KEY_BACKSPACE, keyb_idx_backspace},
    {KEY_TAB, keyb_idx_tab},
    {KEY_SPACE, keyb_idx_space},
    {KEY_MINUS, keyb_idx_minus},
    {KEY_EQUAL, keyb_idx_equals},
    {KEY_LEFTBRACE, keyb_idx_leftbracket},
    {KEY_RIGHTBRACE, keyb_idx_rightbracket},
    {KEY_BACKSLASH, keyb_idx_backslash},
    {KEY_SEMICOLON, keyb_idx_semicolon},
    {KEY_APOSTROPHE, keyb_idx_apostrophe},
    {KEY_GRAVE, keyb_idx_grave},
    {KEY_COMMA, keyb_idx_comma},
    {KEY_DOT, keyb_idx_period},
    {KEY_SLASH, keyb_idx_slash},
    {KEY_CAPSLOCK, keyb_idx_capslock},
    {KEY_F1, keyb_idx_f1},
    {KEY_F2, keyb_idx_f2},
    {KEY_F3, keyb_idx_f3},
    {KEY_F4, keyb_idx_f4},
    {KEY_F5, keyb_idx_f5},
    {KEY_F6, keyb_idx_f6},
    {KEY_F7, keyb_idx_f7},
    {KEY_F8, keyb_idx_f8},
    {KEY_F9, keyb_idx_f9},
    {KEY_F10, keyb_idx_f10},
    {KEY_F11, keyb_idx_f11},
    {KEY_F12, keyb_idx_f12},
    {KEY_SYSRQ, usage_sysrq},
    {KEY_SCROLLLOCK, usage_scroll_lock},
    {KEY_PAUSE, usage_pause},
    {KEY_INSERT, usage_insert},
    {KEY_HOME, usage_home},
    {KEY_PAGEUP, usage_page_up},
    {KEY_DELETE, usage_delete},
    {KEY_END, usage_end},
    {KEY_PAGEDOWN, usage_page_down},
    {KEY_RIGHT, keyb_idx_right},
    {KEY_LEFT, keyb_idx_left},
    {KEY_DOWN, keyb_idx_down},
    {KEY_UP, keyb_idx_up},
}};

}  // namespace

TEST_CASE(
    "SDL1 keys: the X11 keycode table gives every usage from 4 to 82 the "
    "kernel's own key code plus 8, the non-US number sign reading as the "
    "backslash, and any other keycode no usage") {
  CHECK(sdl1_x11_keycode_to_hid(KEY_A + x11_min_keycode) == keyb_idx_a);
  CHECK(sdl1_x11_keycode_to_hid(KEY_1 + x11_min_keycode) == keyb_idx_1);
  CHECK(sdl1_x11_keycode_to_hid(KEY_ENTER + x11_min_keycode) ==
        keyb_idx_return);
  CHECK(sdl1_x11_keycode_to_hid(KEY_A + x11_min_keycode) == 4);
  CHECK(sdl1_x11_keycode_to_hid(KEY_1 + x11_min_keycode) == 30);
  CHECK(sdl1_x11_keycode_to_hid(KEY_ENTER + x11_min_keycode) == 40);

  for (const KernelUsage_t& row : kernel_usages) {
    CAPTURE(row.evdev);
    CAPTURE(row.usage);
    CHECK(sdl1_x11_keycode_to_hid(
              static_cast<uint8_t>(row.evdev + x11_min_keycode)) == row.usage);
  }

  // Usage 50 shares its key code with the backslash, so no keycode reaches it.
  std::array<bool, keyb_map_size> reached{};
  for (int keycode = 0; keycode < 256; ++keycode) {
    reached.at(sdl1_x11_keycode_to_hid(static_cast<uint8_t>(keycode))) = true;
  }
  for (int usage = keyb_idx_a; usage <= keyb_idx_up; ++usage) {
    CAPTURE(usage);
    CHECK(reached.at(usage) == (usage != usage_non_us_hash));
  }
  CHECK(sdl1_x11_keycode_to_hid(0) == keyb_idx_unknown);
  CHECK(sdl1_x11_keycode_to_hid(KEY_LEFTSHIFT + x11_min_keycode) ==
        keyb_idx_unknown);
  CHECK(sdl1_x11_keycode_to_hid(255) == keyb_idx_unknown);
}

TEST_CASE(
    "SDL1 keys: under a driver other than X11 a key's usage comes from its "
    "keysym, so positional mode types the keysym's key whatever the scancode") {
  KeyMachine_t machine(positional());
  REQUIRE(keyboard_get_mapping_mode() == kbd_mode_positional);
  // The A key's X11 keycode with the A keysym: both paths agree.
  KeyMachine_t::key(KEY_A + x11_min_keycode, SDLK_a, KMOD_NONE, true);
  CHECK(KeyMachine_t::latch() == 0xC1);
  KeyMachine_t::key(KEY_A + x11_min_keycode, SDLK_a, KMOD_NONE, false);
  KeyMachine_t::clear_strobe();
  // The B key's X11 keycode with the A keysym: the keysym wins here.
  KeyMachine_t::key(KEY_B + x11_min_keycode, SDLK_a, KMOD_NONE, true);
  CHECK(KeyMachine_t::latch() == 0xC1);
  KeyMachine_t::key(KEY_B + x11_min_keycode, SDLK_a, KMOD_NONE, false);
  KeyMachine_t::clear_strobe();
  KeyMachine_t::key(0, SDLK_q, KMOD_NONE, true);
  CHECK(KeyMachine_t::latch() == 0xD1);
  KeyMachine_t::key(0, SDLK_q, KMOD_NONE, false);
}

TEST_CASE(
    "SDL1 keys: A types $C1 with caps on and $E1 with it off, Left Alt is "
    "Open Apple and Right Alt Solid Apple, and a focus loss lets go of the key "
    "and the switch") {
  KeyMachine_t machine(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  REQUIRE(keyboard_get_caps());
  REQUIRE_FALSE(KeyMachine_t::any_key_down());

  KeyMachine_t::key(0, SDLK_a, KMOD_NONE, true);
  CHECK(KeyMachine_t::latch() == 0xC1);
  CHECK(KeyMachine_t::any_key_down());
  KeyMachine_t::key(0, SDLK_a, KMOD_NONE, false);
  CHECK_FALSE(KeyMachine_t::any_key_down());
  KeyMachine_t::clear_strobe();

  keyboard_set_caps(false);
  KeyMachine_t::key(0, SDLK_a, KMOD_NONE, true);
  CHECK(KeyMachine_t::latch() == 0xE1);
  KeyMachine_t::key(0, SDLK_a, KMOD_NONE, false);
  KeyMachine_t::clear_strobe();

  CHECK(KeyMachine_t::pushbutton(0) == 0);
  CHECK(KeyMachine_t::pushbutton(1) == 0);
  KeyMachine_t::key(0, SDLK_LALT, KMOD_LALT, true);
  CHECK(KeyMachine_t::pushbutton(0) == 1);
  CHECK(KeyMachine_t::pushbutton(1) == 0);
  KeyMachine_t::key(0, SDLK_LALT, KMOD_NONE, false);
  CHECK(KeyMachine_t::pushbutton(0) == 0);
  KeyMachine_t::key(0, SDLK_RALT, KMOD_RALT, true);
  CHECK(KeyMachine_t::pushbutton(0) == 0);
  CHECK(KeyMachine_t::pushbutton(1) == 1);
  KeyMachine_t::key(0, SDLK_RALT, KMOD_NONE, false);
  CHECK(KeyMachine_t::pushbutton(1) == 0);
  // An Alt key is a switch, not a matrix key.
  CHECK_FALSE(KeyMachine_t::any_key_down());

  KeyMachine_t::key(0, SDLK_a, KMOD_NONE, true);
  KeyMachine_t::key(0, SDLK_LALT, KMOD_LALT, true);
  REQUIRE(KeyMachine_t::any_key_down());
  REQUIRE(KeyMachine_t::pushbutton(0) == 1);
  KeyMachine_t::focus(false);
  CHECK_FALSE(KeyMachine_t::any_key_down());
  CHECK(KeyMachine_t::pushbutton(0) == 0);
  // The releases the window manager ate change nothing when they arrive.
  KeyMachine_t::key(0, SDLK_a, KMOD_NONE, false);
  KeyMachine_t::key(0, SDLK_LALT, KMOD_NONE, false);
  CHECK_FALSE(KeyMachine_t::any_key_down());
  CHECK(KeyMachine_t::pushbutton(0) == 0);
}

TEST_CASE(
    "SDL1 keys: a custom Tab is Open Apple and types nothing, and a custom "
    "Grave is the REPT key, which repeats a held key on a II Plus and does "
    "nothing on a //e") {
  using TestConfig_t = TestFixtures::ScopedTestConfig_t;

  SUBCASE("Enhanced //e") {
    KeyMachine_t machine(
        custom_switches(TestConfig_t::machine_apple2e_enhanced));
    KeyMachine_t::key(0, SDLK_a, KMOD_NONE, true);
    REQUIRE(KeyMachine_t::latch() == 0xC1);
    KeyMachine_t::clear_strobe();

    KeyMachine_t::key(0, SDLK_TAB, KMOD_NONE, true);
    CHECK(KeyMachine_t::pushbutton(0) == 1);
    CHECK(KeyMachine_t::latch() == 0x41);
    KeyMachine_t::key(0, SDLK_TAB, KMOD_NONE, false);
    CHECK(KeyMachine_t::pushbutton(0) == 0);

    KeyMachine_t::key(0, SDLK_BACKQUOTE, KMOD_NONE, true);
    CHECK(KeyMachine_t::latch() == 0x41);
    KeyMachine_t::run_frames(rept_frames);
    CHECK(KeyMachine_t::stepped_latch() == 0x41);
    KeyMachine_t::key(0, SDLK_BACKQUOTE, KMOD_NONE, false);
    KeyMachine_t::key(0, SDLK_a, KMOD_NONE, false);
  }

  SUBCASE("II Plus") {
    KeyMachine_t machine(custom_switches(TestConfig_t::machine_apple2_plus),
                         A2TYPE_APPLE2PLUS);
    KeyMachine_t::key(0, SDLK_a, KMOD_NONE, true);
    REQUIRE(KeyMachine_t::latch() == 0xC1);
    KeyMachine_t::clear_strobe();
    REQUIRE((KeyMachine_t::latch() & bit7) == 0);

    // The first REPT strobe comes one period after the press, so the press
    // itself latches nothing.
    KeyMachine_t::key(0, SDLK_BACKQUOTE, KMOD_NONE, true);
    CHECK(KeyMachine_t::latch() == 0x41);
    KeyMachine_t::run_frames(rept_frames);
    CHECK(KeyMachine_t::stepped_latch() == 0xC1);

    KeyMachine_t::stepped_clear_strobe();
    KeyMachine_t::key(0, SDLK_BACKQUOTE, KMOD_NONE, false);
    KeyMachine_t::run_frames(rept_frames);
    CHECK(KeyMachine_t::stepped_latch() == 0x41);
    KeyMachine_t::key(0, SDLK_a, KMOD_NONE, false);
  }
}

TEST_CASE(
    "SDL1 keys: in host mode caps follows the host's lock state on focus gain "
    "and at the key's edges, and in emulated mode a press toggles it") {
  KeyMachine_t machine(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  REQUIRE(keyboard_get_caps_mode() == caps_mode_host);

  keyboard_set_caps(false);
  SDL_SetModState(KMOD_CAPS);
  KeyMachine_t::focus(true);
  CHECK(keyboard_get_caps());
  SDL_SetModState(KMOD_NONE);
  KeyMachine_t::focus(true);
  CHECK_FALSE(keyboard_get_caps());

  // SDL reports the lock's new state in the event's modifiers.
  KeyMachine_t::key(0, SDLK_CAPSLOCK, KMOD_CAPS, true);
  CHECK(keyboard_get_caps());
  KeyMachine_t::key(0, SDLK_CAPSLOCK, KMOD_NONE, false);
  CHECK_FALSE(keyboard_get_caps());

  keyboard_set_caps_mode(caps_mode_emulated);
  KeyMachine_t::key(0, SDLK_CAPSLOCK, KMOD_NONE, true);
  CHECK(keyboard_get_caps());
  KeyMachine_t::key(0, SDLK_CAPSLOCK, KMOD_NONE, false);
  CHECK(keyboard_get_caps());
  KeyMachine_t::key(0, SDLK_CAPSLOCK, KMOD_NONE, true);
  CHECK_FALSE(keyboard_get_caps());
  // The host's lock state is not the emulated key's business.
  SDL_SetModState(KMOD_CAPS);
  KeyMachine_t::focus(true);
  CHECK_FALSE(keyboard_get_caps());
}

TEST_CASE(
    "SDL1 keys: a configured second joystick that is not plugged in leaves "
    "PB2 open at rest") {
  TestFixtures::ScopedTestConfig_t::Description_t description =
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only();
  description.extras.push_back({"Configuration", "Joystick 1", "1"});
  KeyMachine_t machine(description);
  CHECK(KeyMachine_t::pushbutton(0) == 0);
  CHECK(KeyMachine_t::pushbutton(1) == 0);
  CHECK(KeyMachine_t::pushbutton(2) == 1);
}

TEST_CASE(
    "SDL1 keys: Alt+1 with the shipped quick-save modifier names a snapshot "
    "slot and reaches no card") {
  KeyMachine_t machine(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  KeyMachine_t::key(0, SDLK_1, KMOD_LALT, true);
  CHECK((KeyMachine_t::latch() & bit7) == 0);
  CHECK_FALSE(KeyMachine_t::any_key_down());
  CHECK(std::string(save_state_get_filename()).find("SaveState1.aws") !=
        std::string::npos);
  KeyMachine_t::key(0, SDLK_1, KMOD_LALT, false);
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
  return Configuration::instance().get_string("Preferences",
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
    "SDL1 hard disk: Ctrl+Shift+F3 ejects drive 1 of the card wherever the "
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

    const auto chord = static_cast<SDLMod>(KMOD_CTRL | KMOD_SHIFT);
    KeyMachine_t::key(KEY_F3 + x11_min_keycode, SDLK_F3, chord, true);
    KeyMachine_t::key(KEY_F3 + x11_min_keycode, SDLK_F3, chord, false);
    CHECK(harddisk_status().drive0_loaded == 0);
    CHECK(harddisk_saved_key().empty());
  }

  SUBCASE("no card anywhere") {
    KeyMachine_t machine(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
    harddisk_frontend_initialize();
    REQUIRE(harddisk_frontend_slot() == harddisk_frontend_no_card);
    TestFixtures::ScopedLogCapture_t log;
    KeyMachine_t::key(KEY_F3 + x11_min_keycode, SDLK_F3,
                      static_cast<SDLMod>(KMOD_SHIFT), true);
    KeyMachine_t::key(KEY_F3 + x11_min_keycode, SDLK_F3,
                      static_cast<SDLMod>(KMOD_SHIFT), false);
    CHECK(log.count_containing("no hard disk is installed") == 1);
  }
}

TEST_CASE(
    "SDL1 hard disk: the lamp lights on the frame after a read, shows prot "
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
  CHECK(g_status_cycle == show_cycles);

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
