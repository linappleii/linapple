// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

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
  linapple_key_menu
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

#include "apple2/Apple2Types.h"
#include "core/Peripheral.h"

enum AppMode_t {
  MODE_LOGO = 0,
  MODE_PAUSED,
  MODE_RUNNING,
  MODE_DEBUG,
  MODE_STEPPING,
  MODE_DISK_CHOOSE,
  MODE_EXIT,
};

constexpr int path_max_len = 260;
constexpr size_t ftp_user_pass_max_len = 512;
constexpr size_t video_driver_name_max_len = 100;

using SystemState_t = struct SystemState_tag {
  AppMode_t mode;
  bool restart;
  bool fullscreen;
  uint32_t speed;
  uint32_t screen_width;
  uint32_t screen_height;
  bool reset_timing;
  uint32_t needsprecision;
  std::array<char, path_max_len> program_dir;
  std::array<char, path_max_len> current_dir;
  std::array<char, path_max_len> hdd_dir;
  std::array<char, path_max_len> save_state_dir;
  std::array<char, path_max_len> parallel_printer_file;
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

constexpr int SPEED_MIN = 0;
constexpr int SPEED_NORMAL = 10;
constexpr int emulation_speed_max = 40;

constexpr uint32_t DRAW_BACKGROUND = 1;
constexpr uint32_t DRAW_LEDS = 2;
constexpr uint32_t DRAW_TITLE = 4;
constexpr uint32_t DRAW_BUTTON_DRIVES = 8;

constexpr const char* TITLE_APPLE_2 = "Apple ][ Emulator";
constexpr const char* TITLE_APPLE_2_PLUS = "Apple ][+ Emulator";
constexpr const char* TITLE_APPLE_2E = "Apple //e Emulator";
constexpr const char* TITLE_APPLE_2E_ENHANCED = "Enhanced Apple //e Emulator";

constexpr const char* TITLE_PAUSED = " Paused ";
constexpr const char* TITLE_STEPPING = "Stepping";

typedef void CURL;

#ifdef __cplusplus
extern "C" {
#endif

extern const char* g_app_title;
extern char videoDriverName[video_driver_name_max_len];
extern uint64_t cumulative_cycles;
extern uint64_t cycle_num;
extern uint32_t emul_msec;
extern bool g_full_speed;
extern bool hdd_enabled;
extern double g_current_clk_6502;
extern int g_cpu_cycles_feedback;
extern uint32_t g_cycles_this_frame;
extern bool g_disable_direct_sound;
extern uint32_t g_slot4;
extern CURL* g_curl;

using LinappleVideoCallback_t = void (*)(const uint32_t* pixels, int width,
                                         int height, int pitch);
using LinappleAudioCallback_t = void (*)(const int16_t* samples,
                                         size_t num_samples);
using LinappleTitleCallback_t = void (*)(const char* title);

using LinappleVideoCallback = LinappleVideoCallback_t;
using LinappleAudioCallback = LinappleAudioCallback_t;
using LinappleTitleCallback = LinappleTitleCallback_t;

auto linapple_init() -> void;
auto linapple_register_peripherals() -> void;
auto linapple_shutdown() -> void;
auto linapple_cpu_test(const char* test_file, uint16_t trap_addr) -> void;
auto linapple_get_ticks() -> uint32_t;
auto linapple_load_program(const char* path) -> int;
auto linapple_list_hardware() -> void;
auto linapple_run_frame(uint32_t cycles) -> uint32_t;

auto peripheral_manager_init() -> void;
auto peripheral_manager_reset() -> void;
auto peripheral_manager_shutdown() -> void;
auto peripheral_manager_think(uint32_t cycles) -> void;
auto peripheral_manager_on_vblank(bool vblank) -> void;
auto peripheral_is_any_active() -> bool;
auto linapple_list_hardware() -> void;

PeripheralStatus_t peripheral_command(int slot, uint32_t cmd_id,
                                      const void* data, size_t size);
PeripheralStatus_t peripheral_query(int slot, uint32_t cmd_id, void* out,
                                    size_t* out_size);

enum CapsLockMode_t { CAPS_MODE_HOST = 0, CAPS_MODE_EMULATED = 1 };

auto linapple_set_key_state(uint8_t apple_code, bool down) -> void;
auto linapple_set_caps_lock_state(bool enabled) -> void;
auto linapple_get_caps_lock_state() -> bool;
auto linapple_toggle_caps_lock_state() -> bool;
auto linapple_set_apple_key(int key, bool down) -> void;
auto linapple_set_joystick_axis(int axis, int value) -> void;
auto linapple_set_joystick_button(int button, bool down) -> void;

auto linapple_set_video_callback(LinappleVideoCallback_t cb) -> void;
auto linapple_set_audio_callback(LinappleAudioCallback_t cb) -> void;
auto linapple_set_mock_audio_callback(LinappleAudioCallback_t cb) -> void;
auto linapple_set_title_callback(LinappleTitleCallback_t cb) -> void;
auto linapple_update_title(const char* title) -> void;

auto get_title_apple_2() -> const char*;
auto get_title_apple_2_plus() -> const char*;
auto get_title_apple_2e() -> const char*;
auto get_title_apple_2e_enhanced() -> const char*;

#ifdef __cplusplus
}
#endif
