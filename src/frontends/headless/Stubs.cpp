// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "Apple2Types.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "frontends/common/Frontend.h"

[[gnu::weak]] auto frontend_update_keyboard_mapping() -> void {}
[[gnu::weak]] auto keyboard_get_caps_mode() -> int { return 0; }
[[gnu::weak]] auto keyboard_set_caps_mode(int /*mode*/) -> void {}
[[gnu::weak]] auto frontend_dispatch_key_event(uint32_t /*scancode*/,
                                               uint32_t /*keycode*/,
                                               uint32_t /*mod*/,
                                               bool /*is_down*/) -> void {}
[[gnu::weak]] auto frontend_to_core_key(int /*key*/, uint32_t /*mod*/)
    -> LinAppleKey_t {
  return linapple_key_unknown;
}

[[gnu::weak]] auto frame_refresh_status(int /*drawflags*/) -> void {}

[[gnu::weak]] auto super_serial_frontend_initialize(const char* /*p*/) -> bool {
  return false;
}
[[gnu::weak]] auto super_serial_frontend_close() -> void {}
[[gnu::weak]] auto super_serial_frontend_is_active() -> bool { return false; }
[[gnu::weak]] auto super_serial_frontend_update_state(uint32_t /*b*/,
                                                      uint32_t /*d*/, int /*p*/,
                                                      int /*s*/) -> void {}
[[gnu::weak]] auto super_serial_frontend_send_byte(uint8_t /*c*/) -> void {}
[[gnu::weak]] auto super_serial_frontend_set_serial_port_path(const char* /*p*/)
    -> void {}
[[gnu::weak]] auto super_serial_frontend_set_loopback(bool /*e*/) -> void {}

// Video/Frontend Stubs needed for Debugger source linkage
[[gnu::weak]] auto stretch_blt_mem_to_frame_dc() -> void {}
[[gnu::weak]] auto video_update_vbl(uint32_t) -> void {}
[[gnu::weak]] auto video_redraw_screen() -> void {}
[[gnu::weak]] auto video_reset_state() -> void {}
[[gnu::weak]] auto video_get_scanner_address(bool*, uint32_t) noexcept
    -> uint16_t {
  return 0;
}
[[gnu::weak]] auto video_choose_color() -> void {}
[[gnu::weak]] auto video_set_border_color(uint8_t) -> void {}
[[gnu::weak]] auto linapple_list_hardware() -> void {}

[[gnu::weak]] auto mem_read_floating_bus(uint32_t) noexcept -> uint8_t {
  return 0;
}
[[gnu::weak]] auto get_mem_ptr(uint16_t) noexcept -> uint8_t* {
  return nullptr;
}
[[gnu::weak]] auto mem_get_cx_rom_peripheral() noexcept -> uint8_t* {
  return nullptr;
}
[[gnu::weak]] auto register_io_handler(uint32_t, iofunction, iofunction,
                                       iofunction, iofunction, void*,
                                       uint8_t*) noexcept -> void {}
[[gnu::weak]] auto register_direct_io_handler(uint16_t, iofunction, iofunction,
                                              void*) noexcept -> void {}

[[gnu::weak]] uint64_t g_cumulative_cycles = 0;
[[gnu::weak]] SystemState_t system_state = {};
[[gnu::weak]] eApple2Type current_apple2_type = A2TYPE_APPLE2EENHANCED;
[[gnu::weak]] uint32_t g_videotype = 0;
