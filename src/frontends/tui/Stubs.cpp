// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "Apple2Types.h"
#if ENABLE_DEBUGGER
#include "Debugger_Display.h"
#else
auto stretch_blt_mem_to_frame_dc() -> void;
#endif
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "frontends/common/Frontend.h"

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
[[gnu::weak]] auto linapple_update_title(const char*) -> void {}
[[gnu::weak]] auto linapple_list_hardware() -> void {}
[[gnu::weak]] auto linapple_cpu_test(const char*, uint16_t) -> void {}
[[gnu::weak]] auto linapple_load_program(const char*) -> int { return 0; }
[[gnu::weak]] auto linapple_shutdown() -> void {}
[[gnu::weak]] auto linapple_init() -> int { return 0; }

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

#include "core/Log.h"
[[gnu::weak]] auto Logger::debug(const char*, ...) -> void {}
[[gnu::weak]] auto Logger::perf(const char*, ...) -> void {}
[[gnu::weak]] auto Logger::info(const char*, ...) -> void {}
[[gnu::weak]] auto Logger::warning(const char*, ...) -> void {}
[[gnu::weak]] auto Logger::error(const char*, ...) -> void {}
[[gnu::weak]] auto Logger::initialize() -> void {}
[[gnu::weak]] auto Logger::destroy() -> void {}
[[gnu::weak]] auto Logger::set_verbosity(LogLevel) noexcept -> void {}
[[gnu::weak]] auto Logger::get_verbosity() noexcept -> LogLevel {
  return LogLevel::info;
}

[[gnu::weak]] uint64_t g_cumulative_cycles = 0;
[[gnu::weak]] SystemState system_state = {};
[[gnu::weak]] Apple2Type current_apple2_type = A2TYPE_APPLE2EENHANCED;
[[gnu::weak]] uint32_t g_videotype = 0;
