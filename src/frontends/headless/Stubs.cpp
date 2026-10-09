// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "Apple2Types.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"

[[gnu::weak]] auto frame_refresh_status(int /*drawflags*/) -> void {}

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
                                       const uint8_t*) noexcept -> void {}
[[gnu::weak]] auto register_direct_io_handler(uint16_t, iofunction, iofunction,
                                              void*) noexcept -> void {}

[[gnu::weak]] uint64_t g_cumulative_cycles = 0;
[[gnu::weak]] SystemState system_state = {};
[[gnu::weak]] Apple2Type current_apple2_type = A2TYPE_APPLE2EENHANCED;
[[gnu::weak]] uint32_t g_videotype = 0;
