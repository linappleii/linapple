#include <cstdint>

#include "apple2/Video.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "frontends/sdl3/Frontend.h"

// Use weak symbols so that real implementations in unit tests take precedence
#define WEAK __attribute__((weak))

// Stubs for headless/test environments
WEAK auto Frontend_UpdateKeyboardMapping() -> void {}
WEAK auto Frontend_DispatchKeyEvent(uint32_t scancode, uint32_t keycode,
                                    uint32_t mod, bool bDown)
    -> void {  // NOLINT
  (void)scancode;
  (void)keycode;
  (void)mod;
  (void)bDown;
}
WEAK auto frontend_to_core_key(int key, uint32_t mod) -> LinAppleKey {  // NOLINT
  (void)key;
  (void)mod;
  return LINAPPLE_KEY_UNKNOWN;
}

WEAK auto frame_refresh_status(int drawflags) -> void { (void)drawflags; }

// Printer Stubs
WEAK auto PrinterFrontend_Reset() -> void {}
WEAK auto PrinterFrontend_Destroy() -> void {}
WEAK auto PrinterFrontend_Update(uint32_t cycles) -> void { (void)cycles; }
WEAK auto PrinterFrontend_CheckStatus() -> uint8_t { return 0; }
WEAK auto PrinterFrontend_SendChar(uint8_t c) -> void { (void)c; }

// SSC Stubs
WEAK auto super_serial_frontend_initialize(const char* p) -> bool {
  (void)p;
  return false;
}
WEAK auto super_serial_frontend_close() -> void {}
WEAK auto super_serial_frontend_is_active() -> bool { return false; }
WEAK auto super_serial_frontend_update_state(uint32_t b, uint32_t d, int p, int s)
    -> void {  // NOLINT
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
WEAK auto JoySetTrim(int16_t, bool) -> void {}
WEAK auto JoySetButton(eBUTTON, eBUTTONSTATE) -> void {}
WEAK auto JoyUpdatePosition(uint32_t) -> void {}
WEAK auto VideoUpdateVbl(uint32_t) -> void {}
WEAK auto VideoRedrawScreen() -> void {}
WEAK auto VideoResetState() -> void {}
WEAK auto VideoGetScannerAddress(bool*, uint32_t) -> uint16_t { return 0; }
WEAK auto VideoChooseColor() -> void {}
WEAK auto VideoSetBorderColor(uint8_t) -> void {}
WEAK auto Linapple_UpdateTitle(const char*) -> void {}
WEAK auto linapple_list_hardware() -> void {}
WEAK auto Linapple_CpuTest(const char*, uint16_t) -> void {}
WEAK auto Linapple_LoadProgram(const char*) -> int { return 0; }
WEAK auto Linapple_Shutdown() -> void {}
WEAK auto Linapple_Init() -> void {}

WEAK auto MemReadFloatingBus(uint32_t) -> uint8_t { return 0; }
WEAK auto GetMemPtr(uint16_t) -> uint8_t* { return nullptr; }
WEAK auto MemGetCxRomPeripheral() -> uint8_t* { return nullptr; }
WEAK auto RegisterIoHandler(uint32_t, iofunction, iofunction, iofunction,
                            iofunction, void*, uint8_t*) -> void {}
WEAK auto RegisterDirectIoHandler(uint16_t, iofunction, iofunction, void*)
    -> void {}

#include <cstdarg>

#include "core/Log.h"
WEAK auto Logger::Perf(const char*, ...) -> void {}
WEAK auto Logger::Info(const char*, ...) -> void {}
WEAK auto Logger::Warning(const char*, ...) -> void {}
WEAK auto Logger::Error(const char*, ...) -> void {}
WEAK auto Logger::Initialize() -> void {}
WEAK auto Logger::Destroy() -> void {}
WEAK auto Logger::SetVerbosity(LogLevel) -> void {}

WEAK uint64_t g_cumulative_cycles = 0;
WEAK SystemState_t g_state = {};
WEAK eApple2Type g_Apple2Type = A2TYPE_APPLE2EENHANCED;
WEAK uint32_t g_videotype = 0;
WEAK void (*g_frontendAudioCB)(const int16_t*, size_t) = nullptr;
