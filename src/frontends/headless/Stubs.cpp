#include <cstdint>
#include "core/LinAppleCore.h"
#include "frontends/sdl3/Frontend.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Joystick.h"
#include "apple2/peripherals/SerialComms.h"

// Use weak symbols so that real implementations in unit tests take precedence
#define WEAK __attribute__((weak))

// Stubs for headless/test environments
WEAK void Frontend_UpdateKeyboardMapping() {}
WEAK void Frontend_DispatchKeyEvent(uint32_t scancode, uint32_t keycode, uint32_t mod, bool bDown) {
    (void)scancode; (void)keycode; (void)mod; (void)bDown;
}
WEAK LinAppleKey Frontend_ToCoreKey(int key, uint32_t mod) {
    (void)key; (void)mod;
    return LINAPPLE_KEY_UNKNOWN;
}

WEAK void FrameRefreshStatus(int drawflags) { (void)drawflags; }

// Printer Stubs
WEAK void PrinterFrontend_Reset() {}
WEAK void PrinterFrontend_Destroy() {}
WEAK void PrinterFrontend_Update(uint32_t cycles) { (void)cycles; }
WEAK uint8_t PrinterFrontend_CheckStatus() { return 0; }
WEAK void PrinterFrontend_SendChar(uint8_t c) { (void)c; }

// SSC Stubs
WEAK bool SSCFrontend_IsActive() { return false; }
WEAK void SSCFrontend_UpdateState(uint32_t b, uint32_t d, int p, int s) { (void)b; (void)d; (void)p; (void)s; }
WEAK void SSCFrontend_SendByte(uint8_t c) { (void)c; }
WEAK void SSCFrontend_Update(struct SuperSerialCard* ssc, uint32_t c) { (void)ssc; (void)c; }

// Video/Frontend Stubs needed for Debugger source linkage
WEAK void StretchBltMemToFrameDC(void) {}
WEAK void SoundCore_SetFade(int) {}
WEAK void JoySetTrim(short, bool) {}
WEAK void JoySetButton(eBUTTON, eBUTTONSTATE) {}
WEAK void JoyUpdatePosition(uint32_t) {}
WEAK void VideoUpdateVbl(uint32_t) {}
WEAK void VideoRedrawScreen() {}
WEAK void VideoResetState() {}
WEAK uint16_t VideoGetScannerAddress(bool*, uint32_t) { return 0; }
WEAK void VideoChooseColor() {}
WEAK void VideoSetBorderColor(uint8_t) {}
WEAK void Linapple_UpdateTitle(const char*) {}
WEAK void Linapple_ListHardware() {}
WEAK void Linapple_CpuTest(const char*, uint16_t) {}
WEAK int Linapple_LoadProgram(const char*) { return 0; }
WEAK void Linapple_Shutdown() {}
WEAK void Linapple_Init() {}

WEAK uint8_t MemReadFloatingBus(uint32_t) { return 0; }
WEAK uint8_t* GetMemPtr(uint16_t) { return nullptr; }
WEAK uint8_t* MemGetCxRomPeripheral() { return nullptr; }
WEAK void RegisterIoHandler(uint32_t, iofunction, iofunction, iofunction, iofunction, void*, uint8_t*) {}
WEAK void RegisterDirectIoHandler(uint16_t, iofunction, iofunction, void*) {}

#include <stdarg.h>
#include "core/Log.h"
WEAK void Logger::Perf(const char*, ...) {}
WEAK void Logger::Info(const char*, ...) {}
WEAK void Logger::Warning(const char*, ...) {}
WEAK void Logger::Error(const char*, ...) {}
WEAK void Logger::Initialize() {}
WEAK void Logger::Destroy() {}
WEAK void Logger::SetVerbosity(LogLevel) {}

WEAK uint64_t g_nCumulativeCycles = 0;
WEAK SystemState_t g_state = {};
WEAK eApple2Type g_Apple2Type = A2TYPE_APPLE2EENHANCED;
WEAK uint32_t g_videotype = 0;
WEAK void (*g_frontendAudioCB)(const int16_t*, size_t) = nullptr;
WEAK void DSUploadBuffer(int16_t*, uint32_t) {}
