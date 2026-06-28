// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
#include "Peripheral.h"
#include "apple2/SnapshotTypes.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Callback signatures for core outputs
using LinappleVideoCallback_t = void (*)(const uint32_t* pixels, int width,
                                         int height, int pitch);
using LinappleAudioCallback_t = void (*)(const int16_t* samples,
                                         size_t num_samples);
using LinappleTitleCallback_t = void (*)(const char* title);

// Legacy callback aliases for backwards compatibility
using LinappleVideoCallback = LinappleVideoCallback_t;
using LinappleAudioCallback = LinappleAudioCallback_t;
using LinappleTitleCallback = LinappleTitleCallback_t;

// LinApple agnostic key codes
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

// Legacy enum aliases for backwards compatibility
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

// Core Lifecycle API
auto linapple_init() -> void;
auto linapple_register_peripherals() -> void;
auto linapple_shutdown() -> void;
auto linapple_cpu_test(const char* test_file, uint16_t trap_addr) -> void;
auto linapple_get_ticks() -> uint32_t;
auto linapple_load_program(const char* path) -> int;
auto linapple_list_hardware() -> void;
auto linapple_run_frame(uint32_t cycles) -> uint32_t;

// Peripheral Management API (Implemented in Peripheral.cpp and
// Peripheral_Internal.cpp)
auto Peripheral_Manager_Init() -> void;
auto Peripheral_Manager_Reset() -> void;
auto Peripheral_Manager_Shutdown() -> void;
auto Peripheral_Manager_Think(uint32_t cycles) -> void;
auto Peripheral_Manager_OnVBlank(bool vblank) -> void;
auto Peripheral_IsAnyActive() -> bool;
auto Linapple_ListHardware() -> void;

PeripheralStatus Peripheral_Command(int slot, uint32_t cmd_id, const void* data,
                                    size_t size);
PeripheralStatus Peripheral_Query(int slot, uint32_t cmd_id, void* out,
                                  size_t* out_size);

#ifdef __cplusplus
auto Peripheral_Unregister(int slot) -> int;
auto Peripheral_GetManifest(void* manifest) -> void;
auto Peripheral_VerifyManifest(const void* manifest) -> bool;
auto Peripheral_SaveState(int slot, void* buffer, size_t* size) -> void;
auto Peripheral_LoadState(int slot, const void* buffer, size_t size) -> void;
#endif

// Input Handling API
auto linapple_set_key_state(uint8_t apple_code, bool down) -> void;
auto linapple_set_caps_lock_state(bool enabled) -> void;
auto linapple_set_apple_key(int key, bool down) -> void;
auto linapple_set_joystick_axis(int axis, int value) -> void;
auto linapple_set_joystick_button(int button, bool down) -> void;

// Output Callbacks API
auto linapple_set_video_callback(LinappleVideoCallback_t cb) -> void;
auto linapple_set_audio_callback(LinappleAudioCallback_t cb) -> void;
auto linapple_set_mock_audio_callback(LinappleAudioCallback_t cb) -> void;
auto linapple_set_title_callback(LinappleTitleCallback_t cb) -> void;
auto linapple_update_title(const char* title) -> void;

// Legacy C API forwarding inline functions for Linapple_* lifecycle routines
static inline auto Linapple_Init() -> void { linapple_init(); }
static inline auto Linapple_RegisterPeripherals() -> void {
  linapple_register_peripherals();
}
static inline auto Linapple_Shutdown() -> void { linapple_shutdown(); }
static inline auto Linapple_CpuTest(const char* test_file, uint16_t trap_addr)
    -> void {
  linapple_cpu_test(test_file, trap_addr);
}
static inline auto Linapple_GetTicks() -> uint32_t {
  return linapple_get_ticks();
}
static inline auto Linapple_LoadProgram(const char* path) -> int {
  return linapple_load_program(path);
}
static inline auto Linapple_RunFrame(uint32_t cycles) -> uint32_t {
  return linapple_run_frame(cycles);
}
static inline auto Linapple_SetKeyState(uint8_t apple_code, bool down) -> void {
  linapple_set_key_state(apple_code, down);
}
static inline auto Linapple_SetCapsLockState(bool enabled) -> void {
  linapple_set_caps_lock_state(enabled);
}
static inline auto Linapple_SetAppleKey(int key, bool down) -> void {
  linapple_set_apple_key(key, down);
}
static inline auto Linapple_SetJoystickAxis(int axis, int value) -> void {
  linapple_set_joystick_axis(axis, value);
}
static inline auto Linapple_SetJoystickButton(int button, bool down) -> void {
  linapple_set_joystick_button(button, down);
}
static inline auto Linapple_SetVideoCallback(LinappleVideoCallback_t cb)
    -> void {
  linapple_set_video_callback(cb);
}
static inline auto Linapple_SetAudioCallback(LinappleAudioCallback_t cb)
    -> void {
  linapple_set_audio_callback(cb);
}
static inline auto Linapple_SetMockAudioCallback(LinappleAudioCallback_t cb)
    -> void {
  linapple_set_mock_audio_callback(cb);
}
static inline auto Linapple_SetTitleCallback(LinappleTitleCallback_t cb)
    -> void {
  linapple_set_title_callback(cb);
}
static inline auto Linapple_UpdateTitle(const char* title) -> void {
  linapple_update_title(title);
}

#ifdef __cplusplus
}
#endif
