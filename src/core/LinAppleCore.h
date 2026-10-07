// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/Apple2Types.h"
#include "apple2/peripherals/Peripheral_Types.h"

struct PeripheralAudioInfo_t;

enum CapsLockMode_t {
  caps_mode_host = 0,
  caps_mode_emulated = 1,
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

enum AppMode_t {
  app_mode_logo = 0,
  app_mode_paused,
  app_mode_running,
  app_mode_debug,
  app_mode_stepping,
  app_mode_disk_choose,
  app_mode_exit,
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

extern SystemState_t system_state;

constexpr uint32_t emulation_speed_min = 0;
constexpr uint32_t emulation_speed_normal = 10;
constexpr uint32_t emulation_speed_max = 40;

constexpr uint32_t draw_background = 1;
constexpr uint32_t draw_leds = 2;
constexpr uint32_t draw_title = 4;
constexpr uint32_t draw_button_drives = 8;

constexpr const char* title_apple_2 = "Apple ][ Emulator";
constexpr const char* title_apple_2_plus = "Apple ][+ Emulator";
constexpr const char* title_apple_2e = "Apple //e Emulator";
constexpr const char* title_apple_2e_enhanced = "Enhanced Apple //e Emulator";

#ifdef __cplusplus
extern "C" {
#endif

extern const char* app_title;
extern uint32_t emul_msec;
extern bool full_speed;
extern bool hdd_enabled;
extern double current_clk_6502;

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
auto linapple_get_ticks() noexcept -> uint32_t;
auto linapple_load_program(const char* path) -> int;
auto linapple_get_supported_disk_extensions(int slot, char* out_buffer,
                                            size_t buffer_size) -> size_t;
auto linapple_is_supported_disk_image(const char* path) -> bool;
auto linapple_list_hardware() -> void;
auto linapple_run_frame(uint32_t cycles) -> uint32_t;
auto linapple_reset_hard() -> void;
auto linapple_reset_soft() -> void;

auto linapple_get_speed() noexcept -> uint32_t;
auto linapple_set_speed(uint32_t speed) noexcept -> void;
auto linapple_speed_increase() noexcept -> uint32_t;
auto linapple_speed_decrease() noexcept -> uint32_t;
auto linapple_speed_reset() noexcept -> uint32_t;
auto linapple_get_frame_cycles() noexcept -> uint32_t;

auto linapple_get_turbo() noexcept -> bool;
auto linapple_set_turbo(bool turbo) noexcept -> void;
auto linapple_toggle_turbo() noexcept -> bool;
auto linapple_is_full_speed() noexcept -> bool;
auto linapple_get_app_title() noexcept -> const char*;
auto linapple_get_clock_hz() noexcept -> double;
auto linapple_get_apple2_type() noexcept -> Apple2Type_t;
auto linapple_set_apple2_type(Apple2Type_t type) noexcept -> void;
auto linapple_get_language() noexcept -> Apple2Language_t;
auto linapple_set_language(Apple2Language_t lang) noexcept -> void;

auto peripheral_manager_init() -> void;
auto peripheral_manager_reset() -> void;
auto peripheral_manager_shutdown() -> void;
auto peripheral_manager_think(uint32_t cycles) -> void;
// UINT64_MAX when no card asked to be woken; the pass wakes each due card
// once, in slot order, at every slice boundary.
auto peripheral_next_event_cycle() -> uint64_t;
auto peripheral_service_events(uint64_t now) -> void;
auto peripheral_manager_on_vblank(bool vblank) -> void;
auto peripheral_is_any_active() -> bool;

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

// The keyboard as the motherboard sees it: a seven-bit code under a strobe.
// host_key is the host's identity for the key, so that its release pairs with
// its press whatever the modifiers did in between; linapple_set_key_state is
// the convenience for a caller whose key is the code itself.
auto linapple_set_key(uint32_t host_key, uint8_t apple_code, bool down) -> void;
auto linapple_set_key_state(uint8_t apple_code, bool down) -> void;
// The REPT key of a II or II Plus keyboard; a //e has none.
auto linapple_set_rept(bool down) -> void;
// Every key and switch the host held is let go, as when the window loses
// focus and the releases never arrive.
auto linapple_set_key_release_all() -> void;
auto linapple_set_modifiers(bool shift, bool ctrl, bool open_apple,
                            bool solid_apple) -> void;
auto linapple_get_modifiers(bool* shift, bool* ctrl, bool* open_apple,
                            bool* solid_apple) -> void;
// The //e keyboard's rocker switch selects the local half of the keyboard ROM
// and of the character generator (Apple IIe Technical Reference Manual, p.
// 10), so it is machine state the keyboard translation and the video share.
auto linapple_set_rocker_switch(bool local) -> void;
auto linapple_get_rocker_switch() -> bool;
// The game connector's side of the motherboard's switch inputs: a button on
// line 0-2 (PB0-PB2), the lines a controller plug's resistors pull down, one
// bit per line, and the //e board's shift-key mod jumper. The keyboard's side
// of the same lines is linapple_set_modifiers.
auto linapple_set_game_switch(uint8_t line, bool down) -> void;
auto linapple_set_game_pulldowns(uint8_t mask) -> void;
auto linapple_set_shift_key_mod(bool jumper_in) -> void;

auto linapple_set_video_callback(LinappleVideoCallback_t cb) -> void;
auto linapple_set_audio_channel_callback(FrontendAudioChannelCallback_t cb)
    -> void;
auto linapple_set_audio_source_register_callback(
    FrontendAudioSourceRegisterCallback_t cb) -> void;
auto linapple_set_audio_source_unregister_callback(
    FrontendAudioSourceUnregisterCallback_t cb) -> void;
auto linapple_set_title_callback(LinappleTitleCallback_t cb) -> void;
auto linapple_update_title(const char* title) -> void;

auto get_title_apple_2() noexcept -> const char*;
auto get_title_apple_2_plus() noexcept -> const char*;
auto get_title_apple_2e() noexcept -> const char*;
auto get_title_apple_2e_enhanced() noexcept -> const char*;

#ifdef __cplusplus
}
#endif
