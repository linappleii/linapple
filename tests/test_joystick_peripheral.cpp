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
#include "apple2/peripherals/JoystickCommands.h"
#include "core/Peripheral.h"

// Mock Host Interface
static std::vector<uint16_t> registered_io;
static void Mock_RegisterDirectIO(void* instance, uint16_t addr,
                                  PeripheralIOHandler read,
                                  PeripheralIOHandler write) {
  (void)instance;
  (void)read;
  (void)write;
  registered_io.push_back(addr);
}

static HostInterface_t mock_host = {
    nullptr,                // Log
    nullptr,                // AssertIrq
    nullptr,                // RegisterIO
    nullptr,                // RegisterCxROM
    nullptr,                // RegisterExpansionROM
    Mock_RegisterDirectIO,  // RegisterDirectIO
    nullptr,                // GetMemPtr
    nullptr,                // GetCycles
    nullptr,                // GetConfig
    nullptr,                // SetConfig
    nullptr,                // NotifyStatusChanged
    nullptr,                // NotifyActivityChanged
    nullptr,                // RequestPreciseTiming
    nullptr,                // RiffInitWriteFile
    nullptr,                // RiffFinishWriteFile
    nullptr,                // RiffPutSamples
    nullptr,                // AudioPushSamples
    nullptr,                // ResetSystem
    nullptr,                // PrinterPutChar
    nullptr,                // PrinterGetStatus
    nullptr,                // SerialTransmitByte
    nullptr                 // SerialUpdateState
};

extern Peripheral_t g_joystick_peripheral;

TEST_CASE("Joystick Peripheral ABI") {
  registered_io.clear();
  void* instance = g_joystick_peripheral.init(0, &mock_host);
  REQUIRE(instance != nullptr);

  SUBCASE("Registration") {
    // Should register $C061-$C063, $C064-$C067, $C070
    bool found_c070 = false;
    int buttons = 0;
    int paddles = 0;
    for (auto addr : registered_io) {
      if (addr >= 0xC061 && addr <= 0xC063) buttons++;
      if (addr >= 0xC064 && addr <= 0xC067) paddles++;
      if (addr == 0xC070) found_c070 = true;
    }
    CHECK(buttons == 3);
    CHECK(paddles == 4);
    CHECK(found_c070);
  }

  SUBCASE("Commands - Axis") {
    JoystickAxisPayload_t payload = {0, 0, 200};  // Joy 0, Axis X, Val 200
    PeripheralStatus status = g_joystick_peripheral.command(
        instance, JOY_CMD_SET_AXIS, &payload, sizeof(payload));
    CHECK(status == PERIPHERAL_OK);
  }

  SUBCASE("Commands - Button") {
    JoystickButtonPayload_t payload = {1, true};  // Button 1 Down
    PeripheralStatus status = g_joystick_peripheral.command(
        instance, JOY_CMD_SET_BUTTON, &payload, sizeof(payload));
    CHECK(status == PERIPHERAL_OK);
  }

  SUBCASE("Lifecycle") {
    g_joystick_peripheral.reset(instance);
    g_joystick_peripheral.shutdown(instance);
  }
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
