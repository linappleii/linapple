// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include "doctest.h"

#include <cstring>
#include <vector>

#include "apple2/peripherals/mouse/Mouse.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
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

TEST_CASE("Mouse Peripheral ABI") {
  irq_asserted = false;
  auto* descriptor = Mouse_GetDescriptor();
  void* instance = descriptor->init(4, &mock_host);
  REQUIRE(instance != nullptr);

  SUBCASE("Commands - Pos") {
    MousePosPayload_t payload = {100, 1024, 200, 1024};
    PeripheralStatus status = descriptor->command(
        instance, MOUSE_CMD_SET_POS, &payload, sizeof(payload));
    CHECK(status == PERIPHERAL_OK);
  }

  SUBCASE("Commands - Button") {
    MouseButtonPayload_t payload = {0, true};
    PeripheralStatus status = descriptor->command(
        instance, MOUSE_CMD_SET_BUTTON, &payload, sizeof(payload));
    CHECK(status == PERIPHERAL_OK);
  }

  SUBCASE("Lifecycle") {
    descriptor->reset(instance);
  }

  descriptor->shutdown(instance);
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
