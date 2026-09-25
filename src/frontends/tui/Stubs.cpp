// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstdint>

#include "Apple2Types.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "frontends/common/Frontend.h"

// Stubs for headless/test environments
[[gnu::weak]] auto frontend_update_keyboard_mapping() -> void {}
[[gnu::weak]] auto keyboard_get_caps_mode() -> int { return 0; }
[[gnu::weak]] auto keyboard_set_caps_mode(int mode) -> void { (void)mode; }
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): stub callback signature
[[gnu::weak]] auto frontend_dispatch_key_event(uint32_t scancode,
                                               uint32_t keycode, uint32_t mod,
                                               bool is_down) -> void {
  (void)scancode;
  (void)keycode;
  (void)mod;
  (void)is_down;
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): stub callback signature
[[gnu::weak]] auto frontend_to_core_key(int key, uint32_t mod) -> LinAppleKey {
  (void)key;
  (void)mod;
  return LINAPPLE_KEY_UNKNOWN;
}

[[gnu::weak]] auto frame_refresh_status(int drawflags) -> void {
  (void)drawflags;
}

// Printer Stubs
[[gnu::weak]] auto printer_frontend_reset() -> void {}
[[gnu::weak]] auto printer_frontend_destroy() -> void {}
[[gnu::weak]] auto printer_frontend_update(uint32_t cycles) -> void {
  (void)cycles;
}
[[gnu::weak]] auto printer_frontend_check_status() -> uint8_t { return 0; }
[[gnu::weak]] auto printer_frontend_send_char(uint8_t c) -> void { (void)c; }

// SSC Stubs
[[gnu::weak]] auto super_serial_frontend_initialize(const char* p) -> bool {
  (void)p;
  return false;
}
[[gnu::weak]] auto super_serial_frontend_close() -> void {}
[[gnu::weak]] auto super_serial_frontend_is_active() -> bool { return false; }
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): stub callback signature
[[gnu::weak]] auto super_serial_frontend_update_state(uint32_t b, uint32_t d,
                                                      int p, int s) -> void {
  (void)b;
  (void)d;
  (void)p;
  (void)s;
}
[[gnu::weak]] auto super_serial_frontend_send_byte(uint8_t c) -> void {
  (void)c;
}
[[gnu::weak]] auto super_serial_frontend_set_serial_port_path(const char* p)
    -> void {
  (void)p;
}
[[gnu::weak]] auto super_serial_frontend_set_loopback(bool e) -> void {
  (void)e;
}

// Video/Frontend Stubs needed for Debugger source linkage
[[gnu::weak]] auto stretch_blt_mem_to_frame_dc() -> void {}
[[gnu::weak]] auto joy_set_trim(int16_t, bool) -> void {}
[[gnu::weak]] auto joy_set_button(int button, bool down) -> void {
  (void)button;
  (void)down;
}
[[gnu::weak]] auto joy_update_position(uint32_t) -> void {}
[[gnu::weak]] auto video_update_vbl(uint32_t) -> void {}
[[gnu::weak]] auto video_redraw_screen() -> void {}
[[gnu::weak]] auto video_reset_state() -> void {}
[[gnu::weak]] auto video_get_scanner_address(bool*, uint32_t) -> uint16_t {
  return 0;
}
[[gnu::weak]] auto video_choose_color() -> void {}
[[gnu::weak]] auto video_set_border_color(uint8_t) -> void {}
[[gnu::weak]] auto linapple_update_title(const char*) -> void {}
[[gnu::weak]] auto linapple_list_hardware() -> void {}
[[gnu::weak]] auto linapple_cpu_test(const char*, uint16_t) -> void {}
[[gnu::weak]] auto linapple_load_program(const char*) -> int { return 0; }
[[gnu::weak]] auto linapple_shutdown() -> void {}
[[gnu::weak]] auto linapple_init() -> int { return 0; }

[[gnu::weak]] auto mem_read_floating_bus(uint32_t) -> uint8_t { return 0; }
[[gnu::weak]] auto get_mem_ptr(uint16_t) -> uint8_t* { return nullptr; }
[[gnu::weak]] auto mem_get_cx_rom_peripheral() -> uint8_t* { return nullptr; }
[[gnu::weak]] auto register_io_handler(uint32_t, iofunction, iofunction,
                                       iofunction, iofunction, void*, uint8_t*)
    -> void {}
[[gnu::weak]] auto register_direct_io_handler(uint16_t, iofunction, iofunction,
                                              void*) -> void {}

#include "core/Log.h"
[[gnu::weak]] auto Logger::perf(const char*, ...) -> void {}
[[gnu::weak]] auto Logger::info(const char*, ...) -> void {}
[[gnu::weak]] auto Logger::warning(const char*, ...) -> void {}
[[gnu::weak]] auto Logger::error(const char*, ...) -> void {}
[[gnu::weak]] auto Logger::initialize() -> void {}
[[gnu::weak]] auto Logger::destroy() -> void {}
[[gnu::weak]] auto Logger::set_verbosity(LogLevel_t) -> void {}
[[gnu::weak]] auto Logger::get_verbosity() -> LogLevel_t {
  return LogLevel_t::k_info;
}

[[gnu::weak]] uint64_t g_cumulative_cycles = 0;
[[gnu::weak]] SystemState_t g_state = {};
[[gnu::weak]] eApple2Type g_apple2_type = A2TYPE_APPLE2EENHANCED;
[[gnu::weak]] uint32_t g_videotype = 0;
[[gnu::weak]] void (*g_frontendAudioCB)(const int16_t*, size_t) = nullptr;
