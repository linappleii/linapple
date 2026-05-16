// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <map>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/Peripheral.h"
#include "doctest.h"

extern "C" uint64_t g_nCumulativeCycles;
extern "C" double g_fCurrentCLK6502;

constexpr size_t MEMORY_SIZE_64K = 65536;
static std::array<uint8_t, MEMORY_SIZE_64K> dummy_mem{};

extern "C" auto VideoGetScannerAddress(uint32_t*, uint32_t) -> uint16_t {
  return 0;
}

namespace {

constexpr int TEST_SLOT = 0;
constexpr int TEST_SLOT_1 = 4;

constexpr uint16_t ADDR_BUTTON0 = 0xC061;
constexpr uint16_t ADDR_PADDLE0 = 0xC064;
constexpr uint16_t ADDR_PADDLE_RESET = 0xC070;

constexpr int JOYSTICK_BUTTON_COUNT = 3;
constexpr int PADDLES_PER_JOYSTICK = 2;
constexpr int JOYSTICK_COUNT = 2;

constexpr uint8_t HIGH_BIT = 0x80;
constexpr uint8_t MAX_AXIS_VALUE = 255;

constexpr uint64_t INITIAL_CYCLE_COUNT = 1000000;
constexpr uint64_t SMALL_WAIT_CYCLES = 11;
constexpr uint64_t LARGE_WAIT_CYCLES = 2800;
constexpr uint64_t FINAL_WAIT_CYCLES = 20;

constexpr uint64_t THINK_LATCH_CYCLES = 20000;

constexpr uint8_t TRIM_TEST_POS = 100;
constexpr int16_t TRIM_TEST_OFFSET = 50;
constexpr uint64_t TRIM_WAIT_CYCLES = 1600;
constexpr uint64_t TRIM_FINAL_CYCLES = 100;

constexpr uint32_t TEST_JOY_INDEX = 5;
constexpr uint32_t TEST_BUTTON_MAPPING = 10;

struct MockHandler {
  void* instance;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
};

static std::map<uint16_t, MockHandler> g_mock_handlers;
static std::map<int, std::vector<uint8_t>> g_mock_roms;

auto Mock_Log(void* instance, PeripheralLogLevel level, const char* fmt, ...)
    -> void {
  (void)instance;
  (void)level;
  (void)fmt;
}

auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
  (void)slot;
  (void)assert_irq;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
// Justification: Signature is required by HostInterface_t ABI.
auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                     PeripheralIOHandler write_c0, PeripheralIOHandler read_cx,
                     PeripheralIOHandler write_cx) -> void {
  (void)slot;
  (void)read_c0;
  (void)write_c0;
  (void)read_cx;
  (void)write_cx;
}

auto Mock_RegisterCxROM(int slot, uint8_t* rom_ptr) -> void {
  if (rom_ptr != nullptr) {
    std::vector<uint8_t> rom_data(CX_ROM_SIZE);
    std::copy_n(rom_ptr, CX_ROM_SIZE, rom_data.begin());
    g_mock_roms[slot] = rom_data;
  }
}

auto Mock_RegisterExpansionROM(int slot, uint8_t* rom_ptr) -> void {
  (void)slot;
  (void)rom_ptr;
}

auto Mock_RegisterDirectIO(void* instance, uint16_t addr,
                           PeripheralIOHandler read, PeripheralIOHandler write)
    -> void {
  g_mock_handlers[addr] = {instance, read, write};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static HostInterface_t mock_host = {
    .Log = Mock_Log,
    .AssertIrq = Mock_AssertIrq,
    .RegisterIO = Mock_RegisterIO,
    .RegisterCxROM = Mock_RegisterCxROM,
    .RegisterExpansionROM = Mock_RegisterExpansionROM,
    .RegisterDirectIO = Mock_RegisterDirectIO,
    .GetMemPtr = nullptr,
    .GetCycles = nullptr,
    .GetConfig = nullptr,
    .SetConfig = nullptr,
    .NotifyStatusChanged = nullptr,
    .NotifyActivityChanged = nullptr,
    .RequestPreciseTiming = nullptr,
    .RiffInitWriteFile = nullptr,
    .RiffFinishWriteFile = nullptr,
    .RiffPutSamples = nullptr,
    .AudioPushSamples = nullptr,
    .ResetSystem = nullptr,
    .PrinterPutChar = nullptr,
    .PrinterGetStatus = nullptr,
    .SerialTransmitByte = nullptr,
    .SerialUpdateState = nullptr};

static auto Joystick_Init_With_Mock(int slot) -> void* {
  mem = dummy_mem.data();
  void* instance = Joystick_GetDescriptor()->init(slot, &mock_host);
  return instance;
}

TEST_CASE("Joystick Peripheral: Lifecycle and Registration") {
  g_mock_handlers.clear();
  g_mock_roms.clear();
  const int slot = TEST_SLOT_1;
  void* instance = Joystick_Init_With_Mock(slot);
  REQUIRE(instance != nullptr);

  for (uint16_t addr = ADDR_BUTTON0;
       addr < ADDR_BUTTON0 + JOYSTICK_BUTTON_COUNT; ++addr) {
    CHECK(g_mock_handlers.count(addr) > 0);
  }

  for (int addr = static_cast<int>(ADDR_PADDLE0);
       addr <
       static_cast<int>(ADDR_PADDLE0) + (JOYSTICK_COUNT * PADDLES_PER_JOYSTICK);
       ++addr) {
    CHECK(g_mock_handlers.count(static_cast<uint16_t>(addr)) > 0);
  }

  CHECK(g_mock_handlers.count(ADDR_PADDLE_RESET) > 0);
  Joystick_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Joystick Peripheral: Analog Timing Accuracy") {
  g_mock_handlers.clear();
  void* instance = Joystick_Init_With_Mock(TEST_SLOT);
  g_nCumulativeCycles = INITIAL_CYCLE_COUNT;

  JoystickAxisPayload_t px0 = {0, 0, 0};
  Joystick_GetDescriptor()->command(instance, JOY_CMD_SET_AXIS, &px0,
                                    sizeof(px0));

  g_mock_handlers.at(ADDR_PADDLE_RESET)
      .read(instance, 0, ADDR_PADDLE_RESET, 0, 0, 0);
  CHECK((g_mock_handlers.at(ADDR_PADDLE0)
             .read(instance, 0, ADDR_PADDLE0, 0, 0, 0) &
         HIGH_BIT) != 0);
  g_nCumulativeCycles += SMALL_WAIT_CYCLES;
  CHECK((g_mock_handlers.at(ADDR_PADDLE0)
             .read(instance, 0, ADDR_PADDLE0, 0, 0, 0) &
         HIGH_BIT) == 0);

  JoystickAxisPayload_t px255 = {0, 0, MAX_AXIS_VALUE};
  Joystick_GetDescriptor()->command(instance, JOY_CMD_SET_AXIS, &px255,
                                    sizeof(px255));

  g_mock_handlers.at(ADDR_PADDLE_RESET)
      .read(instance, 0, ADDR_PADDLE_RESET, 0, 0, 0);
  g_nCumulativeCycles += LARGE_WAIT_CYCLES;
  CHECK((g_mock_handlers.at(ADDR_PADDLE0)
             .read(instance, 0, ADDR_PADDLE0, 0, 0, 0) &
         HIGH_BIT) != 0);
  g_nCumulativeCycles += FINAL_WAIT_CYCLES;
  CHECK((g_mock_handlers.at(ADDR_PADDLE0)
             .read(instance, 0, ADDR_PADDLE0, 0, 0, 0) &
         HIGH_BIT) == 0);

  Joystick_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Joystick Peripheral: Button Latching and Thinking") {
  g_mock_handlers.clear();
  void* instance = Joystick_Init_With_Mock(TEST_SLOT);

  JoystickButtonPayload_t p_down = {0, true};
  JoystickButtonPayload_t p_up = {0, false};
  Joystick_GetDescriptor()->command(instance, JOY_CMD_SET_BUTTON, &p_down,
                                    sizeof(p_down));
  Joystick_GetDescriptor()->command(instance, JOY_CMD_SET_BUTTON, &p_up,
                                    sizeof(p_up));

  CHECK((g_mock_handlers.at(ADDR_BUTTON0)
             .read(instance, 0, ADDR_BUTTON0, 0, 0, 0) &
         HIGH_BIT) != 0);

  Joystick_GetDescriptor()->think(instance,
                                  static_cast<uint32_t>(THINK_LATCH_CYCLES));
  CHECK((g_mock_handlers.at(ADDR_BUTTON0)
             .read(instance, 0, ADDR_BUTTON0, 0, 0, 0) &
         HIGH_BIT) == 0);

  Joystick_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Joystick Peripheral: Trim Logic") {
  g_mock_handlers.clear();
  void* instance = Joystick_Init_With_Mock(TEST_SLOT);
  g_nCumulativeCycles = INITIAL_CYCLE_COUNT;

  JoystickAxisPayload_t px = {0, 0, TRIM_TEST_POS};
  Joystick_GetDescriptor()->command(instance, JOY_CMD_SET_AXIS, &px,
                                    sizeof(px));

  JoystickTrimPayload_t pt = {true, TRIM_TEST_OFFSET};
  Joystick_GetDescriptor()->command(instance, JOY_CMD_SET_TRIM, &pt,
                                    sizeof(pt));

  g_mock_handlers.at(ADDR_PADDLE_RESET)
      .read(instance, 0, ADDR_PADDLE_RESET, 0, 0, 0);
  g_nCumulativeCycles += TRIM_WAIT_CYCLES;
  CHECK((g_mock_handlers.at(ADDR_PADDLE0)
             .read(instance, 0, ADDR_PADDLE0, 0, 0, 0) &
         HIGH_BIT) != 0);
  g_nCumulativeCycles += TRIM_FINAL_CYCLES;
  CHECK((g_mock_handlers.at(ADDR_PADDLE0)
             .read(instance, 0, ADDR_PADDLE0, 0, 0, 0) &
         HIGH_BIT) == 0);

  Joystick_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Joystick Peripheral: Config and Query") {
  g_mock_handlers.clear();
  void* instance = Joystick_Init_With_Mock(TEST_SLOT);

  JoystickConfig_t cfg{};
  cfg.joy_type[0] = 1;
  cfg.joy_index[0] = TEST_JOY_INDEX;
  cfg.joy0_button_map[0] = TEST_BUTTON_MAPPING;

  Joystick_GetDescriptor()->command(instance, JOY_CMD_SET_CONFIG, &cfg,
                                    sizeof(cfg));

  JoystickConfig_t queried{};
  size_t size = sizeof(queried);
  Joystick_GetDescriptor()->query(instance, JOY_QUERY_CONFIG, &queried, &size);

  CHECK(queried.joy_type[0] == 1);
  CHECK(queried.joy_index[0] == TEST_JOY_INDEX);
  CHECK(queried.joy0_button_map[0] == TEST_BUTTON_MAPPING);

  Joystick_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Joystick Peripheral: Robustness and Multiple Instances") {
  g_mock_handlers.clear();

  void* instance1 = Joystick_Init_With_Mock(TEST_SLOT_1);

  JoystickAxisPayload_t px255 = {0, 0, MAX_AXIS_VALUE};
  Joystick_GetDescriptor()->command(instance1, JOY_CMD_SET_AXIS, &px255,
                                    sizeof(px255));

  g_mock_handlers.at(ADDR_PADDLE_RESET)
      .read(instance1, 0, ADDR_PADDLE_RESET, 0, 0, 0);
  g_nCumulativeCycles += INITIAL_CYCLE_COUNT;

  CHECK((g_mock_handlers.at(ADDR_PADDLE0)
             .read(instance1, 0, ADDR_PADDLE0, 0, 0, 0) &
         HIGH_BIT) == 0);

  Joystick_GetDescriptor()->shutdown(instance1);
}

}  // namespace
