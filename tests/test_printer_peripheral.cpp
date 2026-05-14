// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include "doctest.h"

#include <cstring>
#include <vector>

#include "LinAppleCore.h"
#include "core/Peripheral.h"

// Mock Host Interface
static uint8_t last_printed_char = 0;
static void Mock_PrinterPutChar(void* instance, uint8_t c) {
  (void)instance;
  last_printed_char = c;
}

static uint8_t Mock_PrinterGetStatus(void* instance) {
  (void)instance;
  return 0x7F;  // Ready
}

static void Mock_RegisterIO(int slot, PeripheralIOHandler r,
                            PeripheralIOHandler w, PeripheralIOHandler cr,
                            PeripheralIOHandler cw) {
  (void)slot;
  (void)r;
  (void)w;
  (void)cr;
  (void)cw;
}

static void Mock_RegisterCxROM(int slot, uint8_t* rom) {
  (void)slot;
  (void)rom;
}

static HostInterface_t mock_host = {
    nullptr,               // Log
    nullptr,               // AssertIrq
    Mock_RegisterIO,       // RegisterIO
    Mock_RegisterCxROM,    // RegisterCxROM
    nullptr,               // RegisterExpansionROM
    nullptr,               // RegisterDirectIO
    nullptr,               // GetMemPtr
    nullptr,               // GetCycles
    nullptr,               // GetConfig
    nullptr,               // SetConfig
    nullptr,               // NotifyStatusChanged
    nullptr,               // NotifyActivityChanged
    nullptr,               // RequestPreciseTiming
    nullptr,               // RiffInitWriteFile
    nullptr,               // RiffFinishWriteFile
    nullptr,               // RiffPutSamples
    nullptr,               // AudioPushSamples
    nullptr,               // ResetSystem
    Mock_PrinterPutChar,   // PrinterPutChar
    Mock_PrinterGetStatus, // PrinterGetStatus
    nullptr,               // SerialTransmitByte
    nullptr                // SerialUpdateState
};

extern Peripheral_t g_printer_peripheral;

TEST_CASE("Printer Peripheral ABI") {
  last_printed_char = 0;
  void* instance = g_printer_peripheral.init(1, &mock_host);
  REQUIRE(instance != nullptr);

  // We need to find the write function registered.
  // Actually, we can just call it if we knew the internal symbol,
  // but better to test through the ABI if we had a way to retrieve it.
  // For now, just testing lifecycle.

  SUBCASE("Lifecycle") {
    g_printer_peripheral.reset(instance);
    g_printer_peripheral.shutdown(instance);
  }
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
