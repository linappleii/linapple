// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/Peripheral_Types.h"

struct PeripheralAudioInfo_t;

enum CapsLockMode_t {
  caps_mode_host = 0,
  caps_mode_emulated = 1,

  // Legacy uppercase aliases
  CAPS_MODE_HOST = caps_mode_host,
  CAPS_MODE_EMULATED = caps_mode_emulated,
};

enum LinAppleKey_t {
  linapple_key_unknown = 0,
  linapple_key_return = 0x0D,
  linapple_key_escape = 0x1B,
  linapple_key_backspace = 0x08,
  linapple_key_tab = 0x09,
  linapple_key_space = 0x20,

  linapple_key_up = 0x100,
  linapple_key_down,
  linapple_key_left,
  linapple_key_right,
  linapple_key_pageup,
  linapple_key_pagedown,
  linapple_key_home,
  linapple_key_end,
  linapple_key_insert,
  linapple_key_delete,
  linapple_key_pause,
  linapple_key_scrolllock,
  linapple_key_capslock,
  linapple_key_print,

  linapple_key_f1 = 0x200,
  linapple_key_f2,
  linapple_key_f3,
  linapple_key_f4,
  linapple_key_f5,
  linapple_key_f6,
  linapple_key_f7,
  linapple_key_f8,
  linapple_key_f9,
  linapple_key_f10,
  linapple_key_f11,
  linapple_key_f12,

  linapple_key_kp_0 = 0x300,
  linapple_key_kp_1,
  linapple_key_kp_2,
  linapple_key_kp_3,
  linapple_key_kp_4,
  linapple_key_kp_5,
  linapple_key_kp_6,
  linapple_key_kp_7,
  linapple_key_kp_8,
  linapple_key_kp_9,
  linapple_key_kp_plus,
  linapple_key_kp_minus,
  linapple_key_kp_multiply,
  linapple_key_kp_divide,
  linapple_key_kp_enter,
  linapple_key_kp_period,

  linapple_key_lshift = 0x400,
  linapple_key_rshift,
  linapple_key_lctrl,
  linapple_key_rctrl,
  linapple_key_lalt,
  linapple_key_ralt,
  linapple_key_lgui,
  linapple_key_rgui,
  linapple_key_menu,
};

using LinAppleKey = LinAppleKey_t;

constexpr LinAppleKey_t LINAPPLE_KEY_UNKNOWN = linapple_key_unknown;
constexpr LinAppleKey_t LINAPPLE_KEY_RETURN = linapple_key_return;
constexpr LinAppleKey_t LINAPPLE_KEY_ESCAPE = linapple_key_escape;
constexpr LinAppleKey_t LINAPPLE_KEY_BACKSPACE = linapple_key_backspace;
constexpr LinAppleKey_t LINAPPLE_KEY_TAB = linapple_key_tab;
constexpr LinAppleKey_t LINAPPLE_KEY_SPACE = linapple_key_space;
constexpr LinAppleKey_t LINAPPLE_KEY_UP = linapple_key_up;
constexpr LinAppleKey_t LINAPPLE_KEY_DOWN = linapple_key_down;
constexpr LinAppleKey_t LINAPPLE_KEY_LEFT = linapple_key_left;
constexpr LinAppleKey_t LINAPPLE_KEY_RIGHT = linapple_key_right;
constexpr LinAppleKey_t LINAPPLE_KEY_PAGEUP = linapple_key_pageup;
constexpr LinAppleKey_t LINAPPLE_KEY_PAGEDOWN = linapple_key_pagedown;
constexpr LinAppleKey_t LINAPPLE_KEY_HOME = linapple_key_home;
constexpr LinAppleKey_t LINAPPLE_KEY_END = linapple_key_end;
constexpr LinAppleKey_t LINAPPLE_KEY_INSERT = linapple_key_insert;
constexpr LinAppleKey_t LINAPPLE_KEY_DELETE = linapple_key_delete;
constexpr LinAppleKey_t LINAPPLE_KEY_PAUSE = linapple_key_pause;
constexpr LinAppleKey_t LINAPPLE_KEY_SCROLLLOCK = linapple_key_scrolllock;
constexpr LinAppleKey_t LINAPPLE_KEY_CAPSLOCK = linapple_key_capslock;
constexpr LinAppleKey_t LINAPPLE_KEY_PRINT = linapple_key_print;
constexpr LinAppleKey_t LINAPPLE_KEY_F1 = linapple_key_f1;
constexpr LinAppleKey_t LINAPPLE_KEY_F2 = linapple_key_f2;
constexpr LinAppleKey_t LINAPPLE_KEY_F3 = linapple_key_f3;
constexpr LinAppleKey_t LINAPPLE_KEY_F4 = linapple_key_f4;
constexpr LinAppleKey_t LINAPPLE_KEY_F5 = linapple_key_f5;
constexpr LinAppleKey_t LINAPPLE_KEY_F6 = linapple_key_f6;
constexpr LinAppleKey_t LINAPPLE_KEY_F7 = linapple_key_f7;
constexpr LinAppleKey_t LINAPPLE_KEY_F8 = linapple_key_f8;
constexpr LinAppleKey_t LINAPPLE_KEY_F9 = linapple_key_f9;
constexpr LinAppleKey_t LINAPPLE_KEY_F10 = linapple_key_f10;
constexpr LinAppleKey_t LINAPPLE_KEY_F11 = linapple_key_f11;
constexpr LinAppleKey_t LINAPPLE_KEY_F12 = linapple_key_f12;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_0 = linapple_key_kp_0;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_1 = linapple_key_kp_1;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_2 = linapple_key_kp_2;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_3 = linapple_key_kp_3;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_4 = linapple_key_kp_4;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_5 = linapple_key_kp_5;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_6 = linapple_key_kp_6;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_7 = linapple_key_kp_7;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_8 = linapple_key_kp_8;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_9 = linapple_key_kp_9;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_PLUS = linapple_key_kp_plus;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_MINUS = linapple_key_kp_minus;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_MULTIPLY = linapple_key_kp_multiply;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_DIVIDE = linapple_key_kp_divide;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_ENTER = linapple_key_kp_enter;
constexpr LinAppleKey_t LINAPPLE_KEY_KP_PERIOD = linapple_key_kp_period;
constexpr LinAppleKey_t LINAPPLE_KEY_LSHIFT = linapple_key_lshift;
constexpr LinAppleKey_t LINAPPLE_KEY_RSHIFT = linapple_key_rshift;
constexpr LinAppleKey_t LINAPPLE_KEY_LCTRL = linapple_key_lctrl;
constexpr LinAppleKey_t LINAPPLE_KEY_RCTRL = linapple_key_rctrl;
constexpr LinAppleKey_t LINAPPLE_KEY_LALT = linapple_key_lalt;
constexpr LinAppleKey_t LINAPPLE_KEY_RALT = linapple_key_ralt;
constexpr LinAppleKey_t LINAPPLE_KEY_LGUI = linapple_key_lgui;
constexpr LinAppleKey_t LINAPPLE_KEY_RGUI = linapple_key_rgui;
constexpr LinAppleKey_t LINAPPLE_KEY_MENU = linapple_key_menu;

enum AppMode_t {
  app_mode_logo = 0,
  app_mode_paused,
  app_mode_running,
  app_mode_debug,
  app_mode_stepping,
  app_mode_disk_choose,
  app_mode_exit,

  // Legacy uppercase aliases
  MODE_LOGO = app_mode_logo,
  MODE_PAUSED = app_mode_paused,
  MODE_RUNNING = app_mode_running,
  MODE_DEBUG = app_mode_debug,
  MODE_STEPPING = app_mode_stepping,
  MODE_DISK_CHOOSE = app_mode_disk_choose,
  MODE_EXIT = app_mode_exit,
};

constexpr size_t path_max_len = 260;
constexpr size_t ftp_user_pass_max_len = 512;

struct SystemState_t {
  AppMode_t mode;
  bool restart;
  bool fullscreen;
  uint32_t speed;
  uint32_t screen_width;
  uint32_t screen_height;
  bool reset_timing;
  std::array<char, path_max_len> program_dir;
  std::array<char, path_max_len> current_dir;
  std::array<char, path_max_len> hdd_dir;
  std::array<char, path_max_len> save_state_dir;
  std::array<char, path_max_len> ftp_local_dir;
  std::array<char, path_max_len> ftp_server;
  std::array<char, path_max_len> ftp_server_hdd;
  std::array<char, ftp_user_pass_max_len> ftp_user_pass;
  std::array<char, path_max_len> debugger_script;
  bool video_scanner_ntsc;
  uint32_t clks_per_frame;
  bool disable_debugger;
};

extern SystemState_t g_state;

constexpr uint32_t emulation_speed_min = 0;
constexpr uint32_t emulation_speed_normal = 10;
constexpr uint32_t emulation_speed_max = 40;

// Legacy speed aliases
constexpr uint32_t SPEED_MIN = emulation_speed_min;
constexpr uint32_t SPEED_NORMAL = emulation_speed_normal;

constexpr uint32_t draw_background = 1;
constexpr uint32_t draw_leds = 2;
constexpr uint32_t draw_title = 4;
constexpr uint32_t draw_button_drives = 8;

// Legacy draw aliases
constexpr uint32_t DRAW_BACKGROUND = draw_background;
constexpr uint32_t DRAW_LEDS = draw_leds;
constexpr uint32_t DRAW_TITLE = draw_title;
constexpr uint32_t DRAW_BUTTON_DRIVES = draw_button_drives;

constexpr const char* title_apple_2 = "Apple ][ Emulator";
constexpr const char* title_apple_2_plus = "Apple ][+ Emulator";
constexpr const char* title_apple_2e = "Apple //e Emulator";
constexpr const char* title_apple_2e_enhanced = "Enhanced Apple //e Emulator";

// Legacy title aliases
constexpr const char* TITLE_APPLE_2 = title_apple_2;
constexpr const char* TITLE_APPLE_2_PLUS = title_apple_2_plus;
constexpr const char* TITLE_APPLE_2E = title_apple_2e;
constexpr const char* TITLE_APPLE_2E_ENHANCED = title_apple_2e_enhanced;

#ifdef __cplusplus
extern "C" {
#endif

extern const char* g_app_title;
extern uint64_t cumulative_cycles;
extern uint32_t emul_msec;
extern bool g_full_speed;
extern bool hdd_enabled;
extern double g_current_clk_6502;

using LinappleVideoCallback_t = void (*)(const uint32_t* pixels, int width,
                                         int height, int pitch);
using FrontendAudioChannelCallback_t = void (*)(const char* peripheral_id,
                                                int slot,
                                                const float* const* channels,
                                                size_t num_channels,
                                                size_t num_samples);
using FrontendAudioSourceRegisterCallback_t = void (*)(
    int slot, const char* peripheral_id, const PeripheralAudioInfo_t* info);
using FrontendAudioSourceUnregisterCallback_t = void (*)(int slot);
using LinappleTitleCallback_t = void (*)(const char* title);

auto linapple_init() -> int;
auto linapple_register_peripherals() -> void;
auto linapple_shutdown() -> void;
auto linapple_cpu_test(const char* test_file, uint16_t trap_addr) -> void;
[[nodiscard]] auto linapple_get_ticks() noexcept -> uint32_t;
auto linapple_load_program(const char* path) -> int;
auto linapple_list_hardware() -> void;
auto linapple_run_frame(uint32_t cycles) -> uint32_t;
auto linapple_reset_hard() -> void;
auto linapple_reset_soft() -> void;

[[nodiscard]] auto linapple_get_speed() noexcept -> uint32_t;
auto linapple_set_speed(uint32_t speed) noexcept -> void;
auto linapple_speed_increase() noexcept -> uint32_t;
auto linapple_speed_decrease() noexcept -> uint32_t;
auto linapple_speed_reset() noexcept -> uint32_t;
[[nodiscard]] auto linapple_get_frame_cycles() noexcept -> uint32_t;

[[nodiscard]] auto linapple_get_turbo() noexcept -> bool;
auto linapple_set_turbo(bool turbo) noexcept -> void;
auto linapple_toggle_turbo() noexcept -> bool;

auto peripheral_manager_init() -> void;
auto peripheral_manager_reset() -> void;
auto peripheral_manager_shutdown() -> void;
auto peripheral_manager_think(uint32_t cycles) -> void;
auto peripheral_manager_on_vblank(bool vblank) -> void;
[[nodiscard]] auto peripheral_is_any_active() -> bool;
// Core-internal: it exists so a late subscriber can be handed the state that
// already exists.
auto peripheral_announce_audio_sources() -> void;

auto peripheral_command(int slot, uint32_t cmd_id, const void* data,
                        size_t size) -> PeripheralStatus_t;
auto peripheral_query(int slot, uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus_t;
auto peripheral_command_by_id(int slot, const char* peripheral_id,
                              uint32_t cmd_id, const void* data, size_t size)
    -> PeripheralStatus_t;
auto peripheral_query_by_id(int slot, const char* peripheral_id,
                            uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus_t;

auto linapple_set_key_state(uint8_t apple_code, bool down) -> void;
auto linapple_set_caps_lock_state(bool enabled) -> void;
[[nodiscard]] auto linapple_get_caps_lock_state() -> bool;
auto linapple_toggle_caps_lock_state() -> bool;
auto linapple_set_apple_key(int key, bool down) -> void;
auto linapple_set_joystick_axis(int axis, int value) -> void;
auto linapple_set_joystick_button(int button, bool down) -> void;

auto linapple_set_video_callback(LinappleVideoCallback_t cb) -> void;
auto linapple_set_audio_channel_callback(FrontendAudioChannelCallback_t cb)
    -> void;
auto linapple_set_audio_source_register_callback(
    FrontendAudioSourceRegisterCallback_t cb) -> void;
auto linapple_set_audio_source_unregister_callback(
    FrontendAudioSourceUnregisterCallback_t cb) -> void;
auto linapple_set_title_callback(LinappleTitleCallback_t cb) -> void;
auto linapple_update_title(const char* title) -> void;

[[nodiscard]] auto get_title_apple_2() noexcept -> const char*;
[[nodiscard]] auto get_title_apple_2_plus() noexcept -> const char*;
[[nodiscard]] auto get_title_apple_2e() noexcept -> const char*;
[[nodiscard]] auto get_title_apple_2e_enhanced() noexcept -> const char*;

#ifdef __cplusplus
}
#endif
