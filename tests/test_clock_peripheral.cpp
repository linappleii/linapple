// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay)

#include <array>
#include <cstring>
#include <map>
#include <vector>

#include "core/Common_Globals.h"
#include "core/Peripheral.h"
#include "doctest.h"

extern Peripheral_t g_clock_peripheral;

// Mock host interface for testing
struct MockHandler {
  void* instance;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
};

static std::map<uint16_t, MockHandler> g_mock_handlers;

static void Mock_Log(void* instance, PeripheralLogLevel level, const char* fmt,
                     ...) {
  (void)instance;
  (void)level;
  (void)fmt;
}

static void Mock_AssertIrq(int slot, bool assert) {
  (void)slot;
  (void)assert;
}

static void Mock_RegisterIO(int slot, PeripheralIOHandler readC0,
                            PeripheralIOHandler writeC0,
                            PeripheralIOHandler readCx,
                            PeripheralIOHandler writeCx) {
  (void)slot;
  (void)readC0;
  (void)writeC0;
  (void)readCx;
  (void)writeCx;
}

static void Mock_RegisterCxROM(int slot, uint8_t* rom_ptr) {
  (void)slot;
  (void)rom_ptr;
}

static void Mock_RegisterExpansionROM(int slot, uint8_t* rom_ptr) {
  (void)slot;
  (void)rom_ptr;
}

static void Mock_RegisterDirectIO(void* instance, uint16_t addr,
                                  PeripheralIOHandler read,
                                  PeripheralIOHandler write) {
  g_mock_handlers[addr] = {instance, read, write};
}

static HostInterface_t mock_host = {
    /*.Log =*/Mock_Log,
    /*.AssertIrq =*/Mock_AssertIrq,
    /*.RegisterIO =*/Mock_RegisterIO,
    /*.RegisterCxROM =*/Mock_RegisterCxROM,
    /*.RegisterExpansionROM =*/Mock_RegisterExpansionROM,
    /*.RegisterDirectIO =*/Mock_RegisterDirectIO,
    /*.GetMemPtr =*/nullptr,
    /*.GetCycles =*/nullptr,
    /*.GetConfig =*/nullptr,
    /*.SetConfig =*/nullptr,
    /*.NotifyStatusChanged =*/nullptr,
    /*.NotifyActivityChanged =*/nullptr,
    /*.RequestPreciseTiming =*/nullptr,
    /*.RiffInitWriteFile =*/nullptr,
    /*.RiffFinishWriteFile =*/nullptr,
    /*.RiffPutSamples =*/nullptr,
    /*.AudioPushSamples =*/nullptr,
    /*.ResetSystem =*/nullptr};

TEST_CASE("Clock Peripheral: Lifecycle and I/O Registration") {
  g_mock_handlers.clear();
  void* instance = g_clock_peripheral.init(0, &mock_host);
  REQUIRE(instance != nullptr);

  // Verify $C080-$C08F are registered
  for (uint16_t addr = 0xC080; addr <= 0xC08F; ++addr) {
    CHECK(g_mock_handlers.count(addr) > 0);
    CHECK(g_mock_handlers[addr].read != nullptr);
  }

  g_clock_peripheral.shutdown(instance);
}

TEST_CASE("Clock Peripheral: Time Latching Behavior") {
  g_mock_handlers.clear();
  void* instance = g_clock_peripheral.init(0, &mock_host);

  // 1. Trigger latch update by reading $C08F
  g_mock_handlers[0xC08F].read(instance, 0, 0xC08F, 0, 0, 0);

  // 2. Read latches $C080-$C089
  // ProDOS clock latches are BCD: MM W DD HH MM (two digits each)
  // We can't easily predict the exact time, but we can verify they are valid
  // BCD (0-9)
  for (uint16_t addr = 0xC080; addr <= 0xC089; ++addr) {
    uint8_t val = g_mock_handlers[addr].read(instance, 0, addr, 0, 0, 0);
    CHECK(val <= 9);
  }

  g_clock_peripheral.shutdown(instance);
}

TEST_CASE("Clock Peripheral: State Persistence") {
  g_mock_handlers.clear();
  void* instance1 = g_clock_peripheral.init(0, &mock_host);

  // 1. Latch a time
  g_mock_handlers[0xC08F].read(instance1, 0, 0xC08F, 0, 0, 0);

  // 2. Capture latched values
  std::array<uint8_t, 10> latched_values;
  for (uint16_t i = 0; i < 10; ++i) {
    latched_values[i] =
        g_mock_handlers[0xC080 + i].read(instance1, 0, 0xC080 + i, 0, 0, 0);
  }

  // 3. Save state
  size_t state_size = 0;
  g_clock_peripheral.save_state(instance1, nullptr, &state_size);
  REQUIRE(state_size > 0);

  std::vector<uint8_t> buffer(state_size);
  g_clock_peripheral.save_state(instance1, buffer.data(), &state_size);

  // 4. Create a new instance and load state
  void* instance2 = g_clock_peripheral.init(0, &mock_host);

  // Initially latches should be different or zeroed (Reset zeroes them)
  g_clock_peripheral.reset(instance2);
  for (uint16_t i = 0; i < 10; ++i) {
    CHECK(g_mock_handlers[0xC080 + i].read(instance2, 0, 0xC080 + i, 0, 0, 0) ==
          0);
  }

  // Load the saved state
  PeripheralStatus status =
      g_clock_peripheral.load_state(instance2, buffer.data(), state_size);
  CHECK(status == PERIPHERAL_OK);

  // 5. Verify restored values match original latched values
  for (uint16_t i = 0; i < 10; ++i) {
    uint8_t restored =
        g_mock_handlers[0xC080 + i].read(instance2, 0, 0xC080 + i, 0, 0, 0);
    CHECK(restored == latched_values[i]);
  }

  g_clock_peripheral.shutdown(instance1);
  g_clock_peripheral.shutdown(instance2);
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
