// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "LinAppleCore.h"
#include "apple2/peripherals/MouseCommands.h"
#include "core/Peripheral.h"

// Mock Host Interface
static bool irq_asserted = false;
static void Mock_AssertIrq(int slot, bool assert) {
  (void)slot;
  irq_asserted = assert;
}

static bool Mock_GetConfig(const char* section, const char* key, char* buffer,
                           size_t buffer_size) {
  (void)section;
  (void)key;
  (void)buffer;
  (void)buffer_size;
  return false;
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
    nullptr,             // Log
    Mock_AssertIrq,      // AssertIrq
    Mock_RegisterIO,     // RegisterIO
    Mock_RegisterCxROM,  // RegisterCxROM
    nullptr,             // RegisterExpansionROM
    nullptr,             // RegisterDirectIO
    nullptr,             // GetMemPtr
    nullptr,             // GetCycles
    Mock_GetConfig,      // GetConfig
    nullptr,             // SetConfig
    nullptr,             // NotifyStatusChanged
    nullptr,             // NotifyActivityChanged
    nullptr,             // RequestPreciseTiming
    nullptr,             // RiffInitWriteFile
    nullptr,             // RiffFinishWriteFile
    nullptr,             // RiffPutSamples
    nullptr,             // AudioPushSamples
    nullptr,             // ResetSystem
    nullptr,             // PrinterPutChar
    nullptr,             // PrinterGetStatus
    nullptr,             // SerialTransmitByte
    nullptr              // SerialUpdateState
};

extern Peripheral_t g_mouse_peripheral;

TEST_CASE("Mouse Peripheral ABI") {
  irq_asserted = false;
  void* instance = g_mouse_peripheral.init(4, &mock_host);
  REQUIRE(instance != nullptr);

  SUBCASE("Commands - Pos") {
    MousePosPayload_t payload = {100, 1024, 200, 1024};
    PeripheralStatus status = g_mouse_peripheral.command(
        instance, MOUSE_CMD_SET_POS, &payload, sizeof(payload));
    CHECK(status == PERIPHERAL_OK);
  }

  SUBCASE("Commands - Button") {
    MouseButtonPayload_t payload = {0, true};
    PeripheralStatus status = g_mouse_peripheral.command(
        instance, MOUSE_CMD_SET_BUTTON, &payload, sizeof(payload));
    CHECK(status == PERIPHERAL_OK);
  }

  SUBCASE("Lifecycle") {
    g_mouse_peripheral.reset(instance);
    g_mouse_peripheral.shutdown(instance);
  }
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
