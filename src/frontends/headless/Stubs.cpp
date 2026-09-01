#include <cstddef>
#include <cstdint>

#include "Apple2Types.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "frontends/common/Frontend.h"

// Use weak symbols so that real implementations in unit tests take precedence
#define WEAK __attribute__((weak))

// Stubs for headless/test environments
WEAK auto Frontend_UpdateKeyboardMapping() -> void {}
WEAK auto keyboard_get_caps_mode() -> int { return 0; }
WEAK auto keyboard_set_caps_mode(int mode) -> void { (void)mode; }
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): stub callback signature
WEAK auto Frontend_DispatchKeyEvent(uint32_t scancode, uint32_t keycode,
                                    uint32_t mod, bool is_down) -> void {
  (void)scancode;
  (void)keycode;
  (void)mod;
  (void)is_down;
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): stub callback signature
WEAK auto frontend_to_core_key(int key, uint32_t mod) -> LinAppleKey_t {
  (void)key;
  (void)mod;
  return LINAPPLE_KEY_UNKNOWN;
}

WEAK auto frame_refresh_status(int drawflags) -> void { (void)drawflags; }

// Printer Stubs
WEAK auto PrinterFrontend_Reset() -> void {}
WEAK auto PrinterFrontend_Destroy() -> void {}
WEAK auto printer_frontend_update(uint32_t cycles) -> void { (void)cycles; }
WEAK auto printer_frontend_check_status() -> uint8_t { return 0; }
WEAK auto printer_frontend_send_char(uint8_t c) -> void { (void)c; }

// SSC Stubs
WEAK auto super_serial_frontend_initialize(const char* p) -> bool {
  (void)p;
  return false;
}
WEAK auto super_serial_frontend_close() -> void {}
WEAK auto super_serial_frontend_is_active() -> bool { return false; }
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): stub callback signature
WEAK auto super_serial_frontend_update_state(uint32_t b, uint32_t d, int p,
                                             int s) -> void {
  (void)b;
  (void)d;
  (void)p;
  (void)s;
}
WEAK auto super_serial_frontend_send_byte(uint8_t c) -> void { (void)c; }
WEAK auto super_serial_frontend_set_serial_port_path(const char* p) -> void {
  (void)p;
}
WEAK auto super_serial_frontend_set_loopback(bool e) -> void { (void)e; }

// Video/Frontend Stubs needed for Debugger source linkage
WEAK auto StretchBltMemToFrameDC() -> void {}
WEAK auto joy_set_trim(int16_t, bool) -> void {}
WEAK auto joy_set_button(int button, bool down) -> void {
  (void)button;
  (void)down;
}
WEAK auto joy_update_position(uint32_t) -> void {}
WEAK auto video_update_vbl(uint32_t) -> void {}
WEAK auto video_redraw_screen() -> void {}
WEAK auto video_reset_state() -> void {}
WEAK auto video_get_scanner_address(bool*, uint32_t) -> uint16_t { return 0; }
WEAK auto video_choose_color() -> void {}
WEAK auto VideoSetBorderColor(uint8_t) -> void {}
WEAK auto linapple_list_hardware() -> void {}

WEAK auto mem_read_floating_bus(uint32_t) -> uint8_t { return 0; }
WEAK auto get_mem_ptr(uint16_t) -> uint8_t* { return nullptr; }
WEAK auto mem_get_cx_rom_peripheral() -> uint8_t* { return nullptr; }
WEAK auto register_io_handler(uint32_t, iofunction, iofunction, iofunction,
                              iofunction, void*, uint8_t*) -> void {}
WEAK auto register_direct_io_handler(uint16_t, iofunction, iofunction, void*)
    -> void {}

WEAK uint64_t g_cumulative_cycles = 0;
WEAK SystemState_t g_state = {};
WEAK eApple2Type g_apple2_type = A2TYPE_APPLE2EENHANCED;
WEAK uint32_t g_videotype = 0;
WEAK void (*g_frontendAudioCB)(const int16_t*, size_t) = nullptr;
