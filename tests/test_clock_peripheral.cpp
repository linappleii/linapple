// SPDX-License-Identifier: GPL-2.0-only
#include <stdlib.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Peripheral_Types.h"
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

// Golden timestamp 1: 2026-03-12 14:30:00 UTC (Thursday)
constexpr uint64_t GOLDEN_EPOCH_1 = 1773325800ULL;
constexpr int GOLDEN_MONTH_1 = 3;
constexpr int GOLDEN_WEEKDAY_1 = 4;
constexpr int GOLDEN_DAY_1 = 12;
constexpr int GOLDEN_HOUR_1 = 14;
constexpr int GOLDEN_MINUTE_1 = 30;

// Golden timestamp 2: 2026-11-28 23:59:00 UTC (Saturday)
constexpr uint64_t GOLDEN_EPOCH_2 = 1795910340ULL;
constexpr int GOLDEN_MONTH_2 = 11;
constexpr int GOLDEN_WEEKDAY_2 = 6;
constexpr int GOLDEN_DAY_2 = 28;
constexpr int GOLDEN_HOUR_2 = 23;
constexpr int GOLDEN_MINUTE_2 = 59;

class ScopedUtcTimezone {
 public:
  ScopedUtcTimezone() {
    const char* prev = std::getenv("TZ");
    if (prev != nullptr) {
      prev_tz_ = prev;
      has_prev_ = true;
    }
    setenv("TZ", "UTC", 1);
    tzset();
  }

  ~ScopedUtcTimezone() {
    if (has_prev_) {
      setenv("TZ", prev_tz_.c_str(), 1);
    } else {
      unsetenv("TZ");
    }
    tzset();
  }

  ScopedUtcTimezone(const ScopedUtcTimezone&) = delete;
  auto operator=(const ScopedUtcTimezone&) -> ScopedUtcTimezone& = delete;
  ScopedUtcTimezone(ScopedUtcTimezone&&) = delete;
  auto operator=(ScopedUtcTimezone&&) -> ScopedUtcTimezone& = delete;

 private:
  bool has_prev_ = false;
  std::string prev_tz_;
};

struct MockHandler {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockHandler() = default;
  MockHandler(void* inst, PeripheralIOHandler r, PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

class ClockHarness {
 public:
  ClockHarness() {
    s_active_harness = this;
    host_.Log = Mock_Log;
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.RegisterCxROM = Mock_RegisterCxROM;
    host_.RegisterExpansionROM = Mock_RegisterExpansionROM;
    host_.RegisterDirectIO = Mock_RegisterDirectIO;
  }

  ~ClockHarness() {
    for (auto& slot_inst : instances_) {
      if (slot_inst.second != nullptr) {
        clock_get_descriptor()->shutdown(slot_inst.second);
      }
    }
    instances_.clear();
    s_active_harness = nullptr;
  }

  ClockHarness(const ClockHarness&) = delete;
  auto operator=(const ClockHarness&) -> ClockHarness& = delete;
  ClockHarness(ClockHarness&&) = delete;
  auto operator=(ClockHarness&&) -> ClockHarness& = delete;

  auto host() -> HostInterface_t* { return &host_; }

  auto create_clock(int slot) -> void* {
    void* instance = clock_get_descriptor()->init(slot, &host_);
    if (instance != nullptr) {
      instances_[slot] = instance;
      const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
      for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
        auto it = handlers_.find(base + i);
        if (it != handlers_.end()) {
          it->second.instance = instance;
        }
      }
    }
    return instance;
  }

  auto get_instance(int slot) const -> void* {
    auto it = instances_.find(slot);
    return (it != instances_.end()) ? it->second : nullptr;
  }

  auto set_epoch(int slot, uint64_t epoch) -> PeripheralStatus_t {
    void* inst = get_instance(slot);
    if (inst == nullptr || clock_get_descriptor()->command == nullptr) {
      return peripheral_error;
    }
    return clock_get_descriptor()->command(inst, clock_cmd_set_epoch, &epoch,
                                           sizeof(epoch));
  }

  auto clear_epoch(int slot) -> PeripheralStatus_t {
    void* inst = get_instance(slot);
    if (inst == nullptr || clock_get_descriptor()->command == nullptr) {
      return peripheral_error;
    }
    return clock_get_descriptor()->command(inst, clock_cmd_clear_epoch, nullptr,
                                           0);
  }

  auto trigger_latch(int slot) -> uint8_t {
    const uint16_t addr =
        IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET) + LATCH_TRIGGER_OFFSET;
    return read_io(addr);
  }

  auto read_io(uint16_t addr) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      return it->second.read(it->second.instance, 0, addr, 0, 0, 0);
    }
    return 0;
  }

  auto read_reg(int slot, uint8_t offset) -> uint8_t {
    const uint16_t addr = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET) + offset;
    return read_io(addr);
  }

  auto read_pair(int slot, int offset) -> int {
    constexpr int radix = 10;
    const uint8_t tens = read_reg(slot, static_cast<uint8_t>(offset));
    const uint8_t units = read_reg(slot, static_cast<uint8_t>(offset + 1));
    return (static_cast<int>(tens) * radix) + static_cast<int>(units);
  }

  auto has_handler(uint16_t addr) const -> bool {
    return handlers_.find(addr) != handlers_.end();
  }

  auto get_handler(uint16_t addr) const -> const MockHandler& {
    return handlers_.at(addr);
  }

  auto has_rom(int slot) const -> bool {
    return roms_.find(slot) != roms_.end();
  }

  auto get_rom(int slot) const -> const std::vector<uint8_t>& {
    return roms_.at(slot);
  }

 private:
  ScopedUtcTimezone tz_guard_{};
  HostInterface_t host_{};
  std::map<uint16_t, MockHandler> handlers_;
  std::map<int, std::vector<uint8_t>> roms_;
  std::map<int, void*> instances_;

  static ClockHarness* s_active_harness;

  static auto Mock_Log(void* instance, PeripheralLogLevel_t level,
                       const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    (void)fmt;
  }

  static auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
    (void)slot;
    (void)assert_irq;
  }

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  // Justification: Signature is required by HostInterface_t ABI.
  static auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                              PeripheralIOHandler write_c0,
                              PeripheralIOHandler read_cx,
                              PeripheralIOHandler write_cx) -> void {
    (void)read_cx;
    (void)write_cx;
    if (s_active_harness != nullptr &&
        (read_c0 != nullptr || write_c0 != nullptr)) {
      const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
      for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
        s_active_harness->handlers_[base + i] = {nullptr, read_c0, write_c0};
      }
    }
  }
  // NOLINTEND(bugprone-easily-swappable-parameters)

  static auto Mock_RegisterCxROM(int slot, uint8_t* rom_ptr) -> void {
    if (s_active_harness != nullptr && rom_ptr != nullptr) {
      std::vector<uint8_t> rom_data(CX_ROM_SIZE);
      std::copy_n(rom_ptr, CX_ROM_SIZE, rom_data.begin());
      s_active_harness->roms_[slot] = std::move(rom_data);
    }
  }

  static auto Mock_RegisterExpansionROM(int slot, uint8_t* rom_ptr) -> void {
    (void)slot;
    (void)rom_ptr;
  }

  static auto Mock_RegisterDirectIO(void* instance, uint16_t addr,
                                    PeripheralIOHandler read,
                                    PeripheralIOHandler write) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->handlers_[addr] = {instance, read, write};
    }
  }
};

ClockHarness* ClockHarness::s_active_harness = nullptr;

TEST_CASE("Clock Peripheral: Lifecycle and Registration") {
  ClockHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
  for (uint16_t addr = base; addr < base + REGISTERS_PER_SLOT; ++addr) {
    CHECK(harness.has_handler(addr));
    CHECK(harness.get_handler(addr).read != nullptr);
  }

  REQUIRE(harness.has_rom(slot));
  const auto& rom = harness.get_rom(slot);
  CHECK(rom.at(SIG_OFFSET_0) == PRODOS_SIG_0);
  CHECK(rom.at(SIG_OFFSET_2) == PRODOS_SIG_2);
  CHECK(rom.at(SIG_OFFSET_4) == PRODOS_SIG_4);
  CHECK(rom.at(SIG_OFFSET_6) == PRODOS_SIG_6);
}

TEST_CASE("Clock Peripheral: Time Accuracy") {
  ClockHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  // Phase 1: Latch fixed epoch 1 (2026-03-12 14:30:00 UTC)
  REQUIRE(harness.set_epoch(slot, GOLDEN_EPOCH_1) == peripheral_ok);
  harness.trigger_latch(slot);

  // Verify latched pairs against fixed golden constants
  CHECK(harness.read_pair(slot, LATCH_MONTH) == GOLDEN_MONTH_1);
  CHECK(harness.read_pair(slot, LATCH_WEEKDAY) == GOLDEN_WEEKDAY_1);
  CHECK(harness.read_pair(slot, LATCH_DAY) == GOLDEN_DAY_1);
  CHECK(harness.read_pair(slot, LATCH_HOUR) == GOLDEN_HOUR_1);
  CHECK(harness.read_pair(slot, LATCH_MINUTE) == GOLDEN_MINUTE_1);

  // Verify individual registers (tens and units)
  CHECK(harness.read_reg(slot, 0) == 0);  // Month tens (0)
  CHECK(harness.read_reg(slot, 1) == 3);  // Month units (3)
  CHECK(harness.read_reg(slot, 2) == 0);  // Unused / zero
  CHECK(harness.read_reg(slot, 3) == 4);  // Weekday units (4 = Thu)
  CHECK(harness.read_reg(slot, 4) == 1);  // Day tens (1)
  CHECK(harness.read_reg(slot, 5) == 2);  // Day units (2)
  CHECK(harness.read_reg(slot, 6) == 1);  // Hour tens (1)
  CHECK(harness.read_reg(slot, 7) == 4);  // Hour units (4)
  CHECK(harness.read_reg(slot, 8) == 3);  // Minute tens (3)
  CHECK(harness.read_reg(slot, 9) == 0);  // Minute units (0)

  // Phase 2: Latch fixed epoch 2 with 2-digit month and boundary time
  // (2026-11-28 23:59:00 UTC)
  REQUIRE(harness.set_epoch(slot, GOLDEN_EPOCH_2) == peripheral_ok);
  harness.trigger_latch(slot);

  CHECK(harness.read_pair(slot, LATCH_MONTH) == GOLDEN_MONTH_2);
  CHECK(harness.read_pair(slot, LATCH_WEEKDAY) == GOLDEN_WEEKDAY_2);
  CHECK(harness.read_pair(slot, LATCH_DAY) == GOLDEN_DAY_2);
  CHECK(harness.read_pair(slot, LATCH_HOUR) == GOLDEN_HOUR_2);
  CHECK(harness.read_pair(slot, LATCH_MINUTE) == GOLDEN_MINUTE_2);

  CHECK(harness.read_reg(slot, 0) == 1);  // Month tens (1)
  CHECK(harness.read_reg(slot, 1) == 1);  // Month units (1)
  CHECK(harness.read_reg(slot, 3) == 6);  // Weekday units (6 = Sat)
  CHECK(harness.read_reg(slot, 4) == 2);  // Day tens (2)
  CHECK(harness.read_reg(slot, 5) == 8);  // Day units (8)
  CHECK(harness.read_reg(slot, 6) == 2);  // Hour tens (2)
  CHECK(harness.read_reg(slot, 7) == 3);  // Hour units (3)
  CHECK(harness.read_reg(slot, 8) == 5);  // Minute tens (5)
  CHECK(harness.read_reg(slot, 9) == 9);  // Minute units (9)
}

TEST_CASE("Clock Peripheral: Deterministic Epoch Command Interface") {
  ClockHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  auto* descriptor = clock_get_descriptor();
  REQUIRE(descriptor->command != nullptr);

  // Error handling: null instance
  uint64_t epoch = GOLDEN_EPOCH_1;
  CHECK(descriptor->command(nullptr, clock_cmd_set_epoch, &epoch,
                            sizeof(epoch)) == peripheral_error);

  // Error handling: null payload for set_epoch
  CHECK(descriptor->command(instance, clock_cmd_set_epoch, nullptr,
                            sizeof(epoch)) == peripheral_error);

  // Error handling: invalid payload size
  CHECK(descriptor->command(instance, clock_cmd_set_epoch, &epoch, 1) ==
        peripheral_error);

  // Error handling: unknown command ID
  CHECK(descriptor->command(instance, 0xDEAD, nullptr, 0) == peripheral_error);

  // Valid 32-bit epoch payload
  const uint32_t epoch_32 = static_cast<uint32_t>(GOLDEN_EPOCH_1);
  CHECK(descriptor->command(instance, clock_cmd_set_epoch, &epoch_32,
                            sizeof(epoch_32)) == peripheral_ok);
  harness.trigger_latch(slot);
  CHECK(harness.read_pair(slot, LATCH_MONTH) == GOLDEN_MONTH_1);

  // Clear epoch override
  CHECK(descriptor->command(instance, clock_cmd_clear_epoch, nullptr, 0) ==
        peripheral_ok);
}

TEST_CASE("Clock Peripheral: Reset Behavior") {
  ClockHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  REQUIRE(harness.set_epoch(slot, GOLDEN_EPOCH_1) == peripheral_ok);
  harness.trigger_latch(slot);

  // Verify non-zero state before reset
  CHECK(harness.read_pair(slot, LATCH_DAY) == GOLDEN_DAY_1);

  clock_get_descriptor()->reset(instance);

  for (uint8_t i = 0; i < CLOCK_LATCHES_COUNT; ++i) {
    CHECK(harness.read_reg(slot, i) == 0);
  }
}

TEST_CASE("Clock Peripheral: State Persistence") {
  ClockHarness harness;
  const int slot1 = TEST_SLOT_1;
  void* instance1 = harness.create_clock(slot1);
  REQUIRE(instance1 != nullptr);

  REQUIRE(harness.set_epoch(slot1, GOLDEN_EPOCH_1) == peripheral_ok);
  harness.trigger_latch(slot1);

  std::array<uint8_t, CLOCK_LATCHES_COUNT> original_latches{};
  for (uint8_t i = 0; i < CLOCK_LATCHES_COUNT; ++i) {
    original_latches.at(i) = harness.read_reg(slot1, i);
  }

  size_t state_size = 0;
  CHECK(clock_get_descriptor()->save_state(instance1, nullptr, &state_size) ==
        peripheral_ok);
  REQUIRE(state_size == CLOCK_LATCHES_COUNT);

  std::vector<uint8_t> buffer(state_size);
  CHECK(clock_get_descriptor()->save_state(instance1, buffer.data(),
                                           &state_size) == peripheral_ok);

  const int slot2 = TEST_SLOT_2;
  void* instance2 = harness.create_clock(slot2);
  REQUIRE(instance2 != nullptr);

  CHECK(clock_get_descriptor()->load_state(instance2, buffer.data(),
                                           state_size) == peripheral_ok);

  for (uint8_t i = 0; i < CLOCK_LATCHES_COUNT; ++i) {
    CHECK(harness.read_reg(slot2, i) == original_latches.at(i));
  }
}

TEST_CASE("Clock Peripheral: Robustness and Edge Cases") {
  ClockHarness harness;

  // Null host check
  CHECK(clock_get_descriptor()->init(TEST_SLOT_1, nullptr) == nullptr);

  const int slot1 = TEST_SLOT_1;
  void* instance1 = harness.create_clock(slot1);
  REQUIRE(instance1 != nullptr);

  // Save state buffer too small
  size_t too_small = TINY_BUFFER_SIZE;
  std::array<uint8_t, TINY_BUFFER_SIZE> small_buf{};
  CHECK(clock_get_descriptor()->save_state(instance1, small_buf.data(),
                                           &too_small) == peripheral_error);

  // Load state buffer size mismatch
  std::array<uint8_t, INVALID_STATE_SIZE> wrong_buf{};
  CHECK(clock_get_descriptor()->load_state(instance1, wrong_buf.data(),
                                           INVALID_STATE_SIZE) ==
        peripheral_error);

  // Multi-card isolation: slot 1 is latched, slot 2 is unlatched
  const int slot2 = TEST_SLOT_2;
  void* instance2 = harness.create_clock(slot2);
  REQUIRE(instance2 != nullptr);

  REQUIRE(harness.set_epoch(slot1, GOLDEN_EPOCH_1) == peripheral_ok);
  harness.trigger_latch(slot1);

  // Slot 1 month units is 3 (from March 03)
  CHECK(harness.read_reg(slot1, 0) == 0);
  CHECK(harness.read_reg(slot1, 1) == 3);

  // Slot 2 was never latched, must remain strictly 0 (no weak > 0 assertions)
  CHECK(harness.read_reg(slot2, 0) == 0);
  CHECK(harness.read_reg(slot2, 1) == 0);

  // Null instance safety
  CHECK(clock_get_descriptor()->save_state(nullptr, small_buf.data(),
                                           &too_small) == peripheral_error);
  CHECK(clock_get_descriptor()->load_state(
            nullptr, wrong_buf.data(), INVALID_STATE_SIZE) == peripheral_error);
  clock_get_descriptor()->reset(nullptr);
  clock_get_descriptor()->shutdown(nullptr);
}

}  // namespace
