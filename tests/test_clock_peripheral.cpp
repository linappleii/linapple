// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <ctime>
#include <map>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/peripherals/clock/Clock.h"
#include "core/Peripheral.h"
#include "doctest.h"

namespace {

constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;
constexpr int REGISTERS_PER_SLOT = 16;

constexpr int TEST_SLOT_1 = 4;
constexpr int TEST_SLOT_2 = 5;

constexpr uint8_t LATCH_TRIGGER_OFFSET = 0x0F;

constexpr size_t SIG_OFFSET_0 = 0x00;
constexpr size_t SIG_OFFSET_2 = 0x02;
constexpr size_t SIG_OFFSET_4 = 0x04;
constexpr size_t SIG_OFFSET_6 = 0x06;

constexpr uint8_t PRODOS_SIG_0 = 0x08;
constexpr uint8_t PRODOS_SIG_2 = 0x28;
constexpr uint8_t PRODOS_SIG_4 = 0x58;
constexpr uint8_t PRODOS_SIG_6 = 0x70;

constexpr size_t INVALID_STATE_SIZE = 5;
constexpr size_t TINY_BUFFER_SIZE = 1;

struct MockHandler {
  void* instance;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
};

static std::map<uint16_t, MockHandler> g_mock_handlers;
static std::map<int, std::vector<uint8_t>> g_mock_roms;

auto Mock_Log(void* instance, PeripheralLogLevel_t level, const char* fmt, ...)
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
  (void)read_cx;
  (void)write_cx;
  if (read_c0 != nullptr || write_c0 != nullptr) {
    const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
    for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
      g_mock_handlers[base + i] = {nullptr, read_c0, write_c0};
    }
  }
}
// NOLINTEND(bugprone-easily-swappable-parameters)

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

static HostInterface_t mock_host = [] {
  HostInterface_t h{};
  h.Log = Mock_Log;
  h.AssertIrq = Mock_AssertIrq;
  h.RegisterIO = Mock_RegisterIO;
  h.RegisterCxROM = Mock_RegisterCxROM;
  h.RegisterExpansionROM = Mock_RegisterExpansionROM;
  h.RegisterDirectIO = Mock_RegisterDirectIO;
  h.get_mem_ptr = nullptr;
  h.GetCycles = nullptr;
  h.GetConfig = nullptr;
  h.SetConfig = nullptr;
  h.NotifyStatusChanged = nullptr;
  h.NotifyActivityChanged = nullptr;
  h.RequestPreciseTiming = nullptr;
  h.AudioPushSamples = nullptr;
  h.ResetSystem = nullptr;
  h.PrinterPutChar = nullptr;
  h.PrinterGetStatus = nullptr;
  h.SerialTransmitByte = nullptr;
  h.SerialUpdateState = nullptr;
  return h;
}();

static auto Clock_Init_With_Mock(int slot) -> void* {
  void* instance = clock_get_descriptor()->init(slot, &mock_host);
  const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
  for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
    if (g_mock_handlers.count(base + i)) {
      g_mock_handlers.at(base + i).instance = instance;
    }
  }
  return instance;
}

TEST_CASE("Clock Peripheral: Lifecycle and Registration") {
  g_mock_handlers.clear();
  g_mock_roms.clear();
  const int slot = TEST_SLOT_1;
  void* instance = Clock_Init_With_Mock(slot);
  REQUIRE(instance != nullptr);

  const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
  for (uint16_t addr = base; addr < base + REGISTERS_PER_SLOT; ++addr) {
    CHECK(g_mock_handlers.count(addr) > 0);
    CHECK(g_mock_handlers.at(addr).read != nullptr);
  }

  REQUIRE(g_mock_roms.count(slot) > 0);
  const auto& rom = g_mock_roms.at(slot);
  CHECK(rom.at(SIG_OFFSET_0) == PRODOS_SIG_0);
  CHECK(rom.at(SIG_OFFSET_2) == PRODOS_SIG_2);
  CHECK(rom.at(SIG_OFFSET_4) == PRODOS_SIG_4);
  CHECK(rom.at(SIG_OFFSET_6) == PRODOS_SIG_6);

  clock_get_descriptor()->shutdown(instance);
}

TEST_CASE("Clock Peripheral: Time Accuracy") {
  g_mock_handlers.clear();
  const int slot = TEST_SLOT_1;
  void* instance = Clock_Init_With_Mock(slot);
  const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);

  time_t now = time(nullptr);
  struct tm local_time{};
  localtime_r(&now, &local_time);

  g_mock_handlers.at(base + LATCH_TRIGGER_OFFSET)
      .read(instance, 0, base + LATCH_TRIGGER_OFFSET, 0, 0, 0);

  auto read_pair = [&](int offset) -> int {
    const int radix = 10;
    uint8_t tens = g_mock_handlers.at(base + offset)
                       .read(instance, 0, base + offset, 0, 0, 0);
    uint8_t units = g_mock_handlers.at(base + offset + 1)
                        .read(instance, 0, base + offset + 1, 0, 0, 0);
    return (tens * radix) + units;
  };

  CHECK(read_pair(LATCH_MONTH) == (local_time.tm_mon + 1));
  CHECK(read_pair(LATCH_WEEKDAY) == local_time.tm_wday);
  CHECK(read_pair(LATCH_DAY) == local_time.tm_mday);
  CHECK(read_pair(LATCH_HOUR) == local_time.tm_hour);
  CHECK(read_pair(LATCH_MINUTE) == local_time.tm_min);

  clock_get_descriptor()->shutdown(instance);
}

TEST_CASE("Clock Peripheral: Reset Behavior") {
  g_mock_handlers.clear();
  const int slot = TEST_SLOT_1;
  void* instance = Clock_Init_With_Mock(slot);
  const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);

  g_mock_handlers.at(base + LATCH_TRIGGER_OFFSET)
      .read(instance, 0, base + LATCH_TRIGGER_OFFSET, 0, 0, 0);
  clock_get_descriptor()->reset(instance);

  for (uint16_t i = 0; i < CLOCK_LATCHES_COUNT; ++i) {
    CHECK(g_mock_handlers.at(base + i).read(instance, 0, base + i, 0, 0, 0) ==
          0);
  }

  clock_get_descriptor()->shutdown(instance);
}

TEST_CASE("Clock Peripheral: State Persistence") {
  g_mock_handlers.clear();
  const int slot1 = TEST_SLOT_1;
  void* instance1 = Clock_Init_With_Mock(slot1);
  const uint16_t base1 = IO_BASE_ADDRESS + (slot1 << IO_SLOT_OFFSET);

  g_mock_handlers.at(base1 + LATCH_TRIGGER_OFFSET)
      .read(instance1, 0, base1 + LATCH_TRIGGER_OFFSET, 0, 0, 0);

  std::array<uint8_t, CLOCK_LATCHES_COUNT> original_latches{};
  for (uint16_t i = 0; i < CLOCK_LATCHES_COUNT; ++i) {
    original_latches.at(i) =
        g_mock_handlers.at(base1 + i).read(instance1, 0, base1 + i, 0, 0, 0);
  }

  size_t state_size = 0;
  clock_get_descriptor()->save_state(instance1, nullptr, &state_size);
  REQUIRE(state_size == CLOCK_LATCHES_COUNT);

  std::vector<uint8_t> buffer(state_size);
  clock_get_descriptor()->save_state(instance1, buffer.data(), &state_size);

  const int slot2 = TEST_SLOT_2;
  void* instance2 = Clock_Init_With_Mock(slot2);
  const uint16_t base2 = IO_BASE_ADDRESS + (slot2 << IO_SLOT_OFFSET);

  clock_get_descriptor()->load_state(instance2, buffer.data(), state_size);

  for (uint16_t i = 0; i < CLOCK_LATCHES_COUNT; ++i) {
    CHECK(g_mock_handlers.at(base2 + i).read(instance2, 0, base2 + i, 0, 0,
                                             0) == original_latches.at(i));
  }

  clock_get_descriptor()->shutdown(instance1);
  clock_get_descriptor()->shutdown(instance2);
}

TEST_CASE("Clock Peripheral: Robustness and Edge Cases") {
  g_mock_handlers.clear();

  CHECK(clock_get_descriptor()->init(TEST_SLOT_1, nullptr) == nullptr);

  const int slot = TEST_SLOT_1;
  void* instance = Clock_Init_With_Mock(slot);
  const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);

  size_t too_small = TINY_BUFFER_SIZE;
  std::array<uint8_t, TINY_BUFFER_SIZE> small_buf{};
  CHECK(clock_get_descriptor()->save_state(instance, small_buf.data(),
                                           &too_small) == peripheral_error);

  std::array<uint8_t, INVALID_STATE_SIZE> wrong_buf{};
  CHECK(clock_get_descriptor()->load_state(instance, wrong_buf.data(),
                                           INVALID_STATE_SIZE) ==
        peripheral_error);

  const int slot2 = TEST_SLOT_2;
  void* instance2 = Clock_Init_With_Mock(slot2);
  const uint16_t base2 = IO_BASE_ADDRESS + (slot2 << IO_SLOT_OFFSET);

  g_mock_handlers.at(base + LATCH_TRIGGER_OFFSET)
      .read(instance, 0, base + LATCH_TRIGGER_OFFSET, 0, 0, 0);

  CHECK(g_mock_handlers.at(base + 1).read(instance, 0, base + 1, 0, 0, 0) > 0);
  CHECK(g_mock_handlers.at(base2 + 1).read(instance2, 0, base2 + 1, 0, 0, 0) ==
        0);

  clock_get_descriptor()->shutdown(instance);
  clock_get_descriptor()->shutdown(instance2);
}

}  // namespace
