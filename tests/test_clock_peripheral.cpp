// SPDX-License-Identifier: GPL-2.0-only
#include <stdlib.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/clock/ClockCommands.h"
#include "doctest.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

// The card is reached the way the emulator reaches it, through the registry,
// so one test binary covers the built-in card and the loaded plugin alike.
auto clock_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.clock");
}

constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;
constexpr int REGISTERS_PER_SLOT = 16;
constexpr size_t SLOT_ROM_SIZE = 256;

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

constexpr size_t CLOCK_LATCHES_COUNT = 10;
constexpr int LATCH_MONTH = 0;
constexpr int LATCH_WEEKDAY = 2;
constexpr int LATCH_DAY = 4;
constexpr int LATCH_HOUR = 6;
constexpr int LATCH_MINUTE = 8;

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

// Golden timestamp 3: Leap day 2024-02-29 00:00:00 UTC (Thursday)
constexpr uint64_t GOLDEN_EPOCH_3 = 1709164800ULL;
constexpr int GOLDEN_MONTH_3 = 2;
constexpr int GOLDEN_WEEKDAY_3 = 4;
constexpr int GOLDEN_DAY_3 = 29;
constexpr int GOLDEN_HOUR_3 = 0;
constexpr int GOLDEN_MINUTE_3 = 0;

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
    REQUIRE(clock_descriptor() != nullptr);
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
        clock_descriptor()->shutdown(slot_inst.second);
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
    void* instance = clock_descriptor()->init(slot, &host_);
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
    if (inst == nullptr || clock_descriptor()->command == nullptr) {
      return peripheral_error;
    }
    ClockSetEpochPayload_t payload{epoch};
    return clock_descriptor()->command(inst, clock_cmd_set_epoch, &payload,
                                       sizeof(payload));
  }

  auto clear_epoch(int slot) -> PeripheralStatus_t {
    void* inst = get_instance(slot);
    if (inst == nullptr || clock_descriptor()->command == nullptr) {
      return peripheral_error;
    }
    return clock_descriptor()->command(inst, clock_cmd_clear_epoch, nullptr, 0);
  }

  auto trigger_latch(int slot, uint32_t remaining_cycles = 0) -> uint8_t {
    const uint16_t addr =
        IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET) + LATCH_TRIGGER_OFFSET;
    return read_io(addr, remaining_cycles);
  }

  auto read_io(uint16_t addr, uint32_t remaining_cycles = 0) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      return it->second.read(it->second.instance, 0, addr, 0, 0,
                             remaining_cycles);
    }
    return 0;
  }

  auto read_reg(int slot, uint8_t offset, uint32_t remaining_cycles = 0)
      -> uint8_t {
    const uint16_t addr = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET) + offset;
    return read_io(addr, remaining_cycles);
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

  static auto Mock_RegisterCxROM(int slot, const uint8_t* rom_ptr) -> void {
    if (s_active_harness != nullptr && rom_ptr != nullptr) {
      std::vector<uint8_t> rom_data(SLOT_ROM_SIZE);
      std::copy_n(rom_ptr, SLOT_ROM_SIZE, rom_data.begin());
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

TEST_CASE("Clock Peripheral: Registration and Identity Metadata") {
  auto* descriptor = clock_descriptor();
  REQUIRE(descriptor != nullptr);

  CHECK(descriptor->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::strcmp(descriptor->id, "linapple.clock") == 0);
  CHECK(std::strcmp(descriptor->name, "Clock Card") == 0);
  CHECK(descriptor->compatible_slots == PERIPHERAL_MASK_EXPANSION);
  CHECK(descriptor->default_slot == -1);

  CHECK(descriptor->init != nullptr);
  CHECK(descriptor->reset != nullptr);
  CHECK(descriptor->shutdown != nullptr);
  CHECK(descriptor->think == nullptr);
  CHECK(descriptor->on_vblank == nullptr);
  CHECK(descriptor->save_state != nullptr);
  CHECK(descriptor->load_state != nullptr);
  CHECK(descriptor->command != nullptr);
  CHECK(descriptor->query != nullptr);
}

TEST_CASE("Clock Peripheral: Slot ROM Contract") {
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
  REQUIRE(rom.size() == SLOT_ROM_SIZE);
  CHECK(rom.at(SIG_OFFSET_0) == PRODOS_SIG_0);
  CHECK(rom.at(SIG_OFFSET_2) == PRODOS_SIG_2);
  CHECK(rom.at(SIG_OFFSET_4) == PRODOS_SIG_4);
  CHECK(rom.at(SIG_OFFSET_6) == PRODOS_SIG_6);
}

TEST_CASE("Clock Peripheral: Bus Fidelity and Floating Bus Pass-Through") {
  ClockHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  REQUIRE(harness.set_epoch(slot, GOLDEN_EPOCH_1) == peripheral_ok);

  // Strobe register $C08F updates latches and returns floating bus data
  constexpr uint32_t test_cycles_1 = 42;
  const uint8_t strobe_read_1 = harness.trigger_latch(slot, test_cycles_1);
  CHECK(strobe_read_1 == mem_read_floating_bus(test_cycles_1));

  // Latches are now populated
  CHECK(harness.read_pair(slot, LATCH_MONTH) == GOLDEN_MONTH_1);

  // Unmapped register holes ($C08A..$C08E) return floating bus data
  for (uint8_t unmapped = 0x0A; unmapped <= 0x0E; ++unmapped) {
    constexpr uint32_t test_cycles_2 = 100;
    CHECK(harness.read_reg(slot, unmapped, test_cycles_2) ==
          mem_read_floating_bus(test_cycles_2));
  }
}

TEST_CASE("Clock Peripheral: Time Accuracy Across Epochs") {
  ClockHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  // Scenario 1: Golden Epoch 1 (2026-03-12 14:30:00 UTC, Thursday)
  REQUIRE(harness.set_epoch(slot, GOLDEN_EPOCH_1) == peripheral_ok);
  harness.trigger_latch(slot);

  CHECK(harness.read_pair(slot, LATCH_MONTH) == GOLDEN_MONTH_1);
  CHECK(harness.read_pair(slot, LATCH_WEEKDAY) == GOLDEN_WEEKDAY_1);
  CHECK(harness.read_pair(slot, LATCH_DAY) == GOLDEN_DAY_1);
  CHECK(harness.read_pair(slot, LATCH_HOUR) == GOLDEN_HOUR_1);
  CHECK(harness.read_pair(slot, LATCH_MINUTE) == GOLDEN_MINUTE_1);

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

  // Scenario 2: Golden Epoch 2 (2026-11-28 23:59:00 UTC, Saturday)
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

  // Scenario 3: Golden Epoch 3 (Leap Day 2024-02-29 00:00:00 UTC, Thursday)
  REQUIRE(harness.set_epoch(slot, GOLDEN_EPOCH_3) == peripheral_ok);
  harness.trigger_latch(slot);

  CHECK(harness.read_pair(slot, LATCH_MONTH) == GOLDEN_MONTH_3);
  CHECK(harness.read_pair(slot, LATCH_WEEKDAY) == GOLDEN_WEEKDAY_3);
  CHECK(harness.read_pair(slot, LATCH_DAY) == GOLDEN_DAY_3);
  CHECK(harness.read_pair(slot, LATCH_HOUR) == GOLDEN_HOUR_3);
  CHECK(harness.read_pair(slot, LATCH_MINUTE) == GOLDEN_MINUTE_3);

  CHECK(harness.read_reg(slot, 0) == 0);  // Month tens (0)
  CHECK(harness.read_reg(slot, 1) == 2);  // Month units (2)
  CHECK(harness.read_reg(slot, 4) == 2);  // Day tens (2)
  CHECK(harness.read_reg(slot, 5) == 9);  // Day units (9)
  CHECK(harness.read_reg(slot, 6) == 0);  // Hour tens (0)
  CHECK(harness.read_reg(slot, 7) == 0);  // Hour units (0)
}

TEST_CASE("Clock Peripheral: Command ABI Protocol") {
  ClockHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  auto* descriptor = clock_descriptor();
  REQUIRE(descriptor->command != nullptr);

  // Struct payload
  ClockSetEpochPayload_t struct_payload{GOLDEN_EPOCH_1};
  CHECK(descriptor->command(instance, clock_cmd_set_epoch, &struct_payload,
                            sizeof(struct_payload)) == peripheral_ok);
  harness.trigger_latch(slot);
  CHECK(harness.read_pair(slot, LATCH_MONTH) == GOLDEN_MONTH_1);

  // Legacy bare 64-bit payload
  uint64_t epoch_64 = GOLDEN_EPOCH_2;
  CHECK(descriptor->command(instance, clock_cmd_set_epoch, &epoch_64,
                            sizeof(epoch_64)) == peripheral_ok);
  harness.trigger_latch(slot);
  CHECK(harness.read_pair(slot, LATCH_MONTH) == GOLDEN_MONTH_2);

  // Legacy bare 32-bit payload
  const uint32_t epoch_32 = static_cast<uint32_t>(GOLDEN_EPOCH_1);
  CHECK(descriptor->command(instance, clock_cmd_set_epoch, &epoch_32,
                            sizeof(epoch_32)) == peripheral_ok);
  harness.trigger_latch(slot);
  CHECK(harness.read_pair(slot, LATCH_MONTH) == GOLDEN_MONTH_1);

  // Clear epoch override
  CHECK(descriptor->command(instance, clock_cmd_clear_epoch, nullptr, 0) ==
        peripheral_ok);

  // Error paths: null instance
  CHECK(descriptor->command(nullptr, clock_cmd_set_epoch, &struct_payload,
                            sizeof(struct_payload)) == peripheral_error);

  // Error paths: null payload for set_epoch
  CHECK(descriptor->command(instance, clock_cmd_set_epoch, nullptr,
                            sizeof(struct_payload)) == peripheral_error);

  // Error paths: invalid payload size
  CHECK(descriptor->command(instance, clock_cmd_set_epoch, &struct_payload,
                            1) == peripheral_error);

  // Error paths: unknown command ID must return peripheral_incompatible
  CHECK(descriptor->command(instance, 0xDEAD, nullptr, 0) ==
        peripheral_incompatible);
}

TEST_CASE("Clock Peripheral: Query ABI Protocol") {
  ClockHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  auto* descriptor = clock_descriptor();
  REQUIRE(descriptor->query != nullptr);

  REQUIRE(harness.set_epoch(slot, GOLDEN_EPOCH_1) == peripheral_ok);
  harness.trigger_latch(slot);

  // Query Epoch: two-pass sizing probe
  size_t epoch_size = 0;
  CHECK(descriptor->query(instance, clock_query_get_epoch, nullptr,
                          &epoch_size) == peripheral_ok);
  CHECK(epoch_size == sizeof(ClockEpochQuery_t));

  ClockEpochQuery_t epoch_query{};
  CHECK(descriptor->query(instance, clock_query_get_epoch, &epoch_query,
                          &epoch_size) == peripheral_ok);
  CHECK(epoch_query.epoch == GOLDEN_EPOCH_1);
  CHECK(epoch_query.is_fixed == 1);

  // Query Time: two-pass sizing probe
  size_t time_size = 0;
  CHECK(descriptor->query(instance, clock_query_get_time, nullptr,
                          &time_size) == peripheral_ok);
  CHECK(time_size == sizeof(ClockTimeQuery_t));

  ClockTimeQuery_t time_query{};
  CHECK(descriptor->query(instance, clock_query_get_time, &time_query,
                          &time_size) == peripheral_ok);
  CHECK(time_query.month == GOLDEN_MONTH_1);
  CHECK(time_query.day_of_week == GOLDEN_WEEKDAY_1);
  CHECK(time_query.day == GOLDEN_DAY_1);
  CHECK(time_query.hour == GOLDEN_HOUR_1);
  CHECK(time_query.minute == GOLDEN_MINUTE_1);

  // Error paths: null size pointer
  CHECK(descriptor->query(instance, clock_query_get_epoch, &epoch_query,
                          nullptr) == peripheral_error);

  // Error paths: buffer too small
  size_t too_small = 2;
  CHECK(descriptor->query(instance, clock_query_get_epoch, &epoch_query,
                          &too_small) == peripheral_error);

  // Error paths: null instance
  CHECK(descriptor->query(nullptr, clock_query_get_epoch, &epoch_query,
                          &epoch_size) == peripheral_error);

  // Error paths: unknown query ID
  CHECK(descriptor->query(instance, 0xBEEF, &epoch_query, &epoch_size) ==
        peripheral_incompatible);
}

TEST_CASE("Clock Peripheral: Reset Behavior") {
  ClockHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  REQUIRE(harness.set_epoch(slot, GOLDEN_EPOCH_1) == peripheral_ok);
  harness.trigger_latch(slot);

  CHECK(harness.read_pair(slot, LATCH_DAY) == GOLDEN_DAY_1);

  clock_descriptor()->reset(instance);

  for (uint8_t i = 0; i < CLOCK_LATCHES_COUNT; ++i) {
    CHECK(harness.read_reg(slot, i) == 0);
  }
}

TEST_CASE("Clock Peripheral: Deterministic Save State Persistence") {
  ClockHarness harness;
  const int slot1 = TEST_SLOT_1;
  void* instance1 = harness.create_clock(slot1);
  REQUIRE(instance1 != nullptr);

  REQUIRE(harness.set_epoch(slot1, GOLDEN_EPOCH_1) == peripheral_ok);
  harness.trigger_latch(slot1);

  // Sizing probe
  size_t state_size = 0;
  CHECK(clock_descriptor()->save_state(instance1, nullptr, &state_size) ==
        peripheral_ok);
  REQUIRE(state_size == sizeof(ClockSaveState_t));
  CHECK(state_size == 32);

  // Undersized buffer guard
  size_t too_small = sizeof(ClockSaveState_t) - 1;
  std::vector<uint8_t> small_buffer(too_small);
  CHECK(clock_descriptor()->save_state(instance1, small_buffer.data(),
                                       &too_small) == peripheral_error);

  // Successful serialization
  std::vector<uint8_t> buffer(state_size);
  CHECK(clock_descriptor()->save_state(instance1, buffer.data(), &state_size) ==
        peripheral_ok);

  const auto* state_view =
      reinterpret_cast<const ClockSaveState_t*>(buffer.data());
  CHECK(state_view->version == CLOCK_STATE_VERSION);
  CHECK(state_view->struct_size == sizeof(ClockSaveState_t));
  CHECK(state_view->use_fixed_epoch == 1);
  CHECK(state_view->fixed_epoch == GOLDEN_EPOCH_1);

  // Restore onto fresh instance in slot 2
  const int slot2 = TEST_SLOT_2;
  void* instance2 = harness.create_clock(slot2);
  REQUIRE(instance2 != nullptr);

  CHECK(clock_descriptor()->load_state(instance2, buffer.data(), state_size) ==
        peripheral_ok);

  // Verify latches are restored bit-for-bit
  for (uint8_t i = 0; i < CLOCK_LATCHES_COUNT; ++i) {
    CHECK(harness.read_reg(slot2, i) == harness.read_reg(slot1, i));
  }

  // Verify fixed epoch was restored via query
  ClockEpochQuery_t restored_epoch{};
  size_t query_size = sizeof(restored_epoch);
  CHECK(clock_descriptor()->query(instance2, clock_query_get_epoch,
                                  &restored_epoch,
                                  &query_size) == peripheral_ok);
  CHECK(restored_epoch.epoch == GOLDEN_EPOCH_1);
  CHECK(restored_epoch.is_fixed == 1);
}

TEST_CASE("Clock Peripheral: Defensive Deserialization Validation") {
  ClockHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  REQUIRE(harness.set_epoch(slot, GOLDEN_EPOCH_1) == peripheral_ok);
  harness.trigger_latch(slot);

  size_t state_size = 0;
  clock_descriptor()->save_state(instance, nullptr, &state_size);
  std::vector<uint8_t> valid_state(state_size);
  clock_descriptor()->save_state(instance, valid_state.data(), &state_size);

  // Mismatched buffer size
  CHECK(clock_descriptor()->load_state(instance, valid_state.data(),
                                       state_size - 1) == peripheral_error);

  // Corrupt version
  auto bad_version = valid_state;
  reinterpret_cast<ClockSaveState_t*>(bad_version.data())->version = 999;
  CHECK(clock_descriptor()->load_state(instance, bad_version.data(),
                                       state_size) == peripheral_error);

  // Corrupt struct_size
  auto bad_struct_size = valid_state;
  reinterpret_cast<ClockSaveState_t*>(bad_struct_size.data())->struct_size =
      12345;
  CHECK(clock_descriptor()->load_state(instance, bad_struct_size.data(),
                                       state_size) == peripheral_error);

  // Corrupt BCD digit (> 9)
  auto bad_digit = valid_state;
  reinterpret_cast<ClockSaveState_t*>(bad_digit.data())->latches[0] = 15;
  CHECK(clock_descriptor()->load_state(instance, bad_digit.data(),
                                       state_size) == peripheral_error);

  // Corrupt non-zero latch[2]
  auto bad_latch_2 = valid_state;
  reinterpret_cast<ClockSaveState_t*>(bad_latch_2.data())->latches[2] = 1;
  CHECK(clock_descriptor()->load_state(instance, bad_latch_2.data(),
                                       state_size) == peripheral_error);

  // Corrupt month (> 12)
  auto bad_month = valid_state;
  reinterpret_cast<ClockSaveState_t*>(bad_month.data())->latches[0] = 1;
  reinterpret_cast<ClockSaveState_t*>(bad_month.data())->latches[1] = 5;  // 15
  CHECK(clock_descriptor()->load_state(instance, bad_month.data(),
                                       state_size) == peripheral_error);

  // Corrupt weekday (> 6)
  auto bad_weekday = valid_state;
  reinterpret_cast<ClockSaveState_t*>(bad_weekday.data())->latches[3] = 7;
  CHECK(clock_descriptor()->load_state(instance, bad_weekday.data(),
                                       state_size) == peripheral_error);

  // Corrupt day (> 31)
  auto bad_day = valid_state;
  reinterpret_cast<ClockSaveState_t*>(bad_day.data())->latches[4] = 3;
  reinterpret_cast<ClockSaveState_t*>(bad_day.data())->latches[5] = 5;  // 35
  CHECK(clock_descriptor()->load_state(instance, bad_day.data(), state_size) ==
        peripheral_error);

  // Corrupt hour (> 23)
  auto bad_hour = valid_state;
  reinterpret_cast<ClockSaveState_t*>(bad_hour.data())->latches[6] = 2;
  reinterpret_cast<ClockSaveState_t*>(bad_hour.data())->latches[7] = 5;  // 25
  CHECK(clock_descriptor()->load_state(instance, bad_hour.data(), state_size) ==
        peripheral_error);

  // Corrupt minute (> 59)
  auto bad_minute = valid_state;
  reinterpret_cast<ClockSaveState_t*>(bad_minute.data())->latches[8] = 6;
  reinterpret_cast<ClockSaveState_t*>(bad_minute.data())->latches[9] = 0;  // 60
  CHECK(clock_descriptor()->load_state(instance, bad_minute.data(),
                                       state_size) == peripheral_error);
}

TEST_CASE("Clock Peripheral: Multi-Card Concurrency and Lifecycle Robustness") {
  ClockHarness harness;

  // Null host check on init
  CHECK(clock_descriptor()->init(TEST_SLOT_1, nullptr) == nullptr);

  // Multi-card isolation: slot 1 is latched to Epoch 1, slot 2 is unlatched
  const int slot1 = TEST_SLOT_1;
  const int slot2 = TEST_SLOT_2;
  void* instance1 = harness.create_clock(slot1);
  void* instance2 = harness.create_clock(slot2);
  REQUIRE(instance1 != nullptr);
  REQUIRE(instance2 != nullptr);

  REQUIRE(harness.set_epoch(slot1, GOLDEN_EPOCH_1) == peripheral_ok);
  harness.trigger_latch(slot1);

  // Slot 1 has valid latch data
  CHECK(harness.read_pair(slot1, LATCH_MONTH) == GOLDEN_MONTH_1);

  // Slot 2 was never triggered/latched, all latches remain strictly 0
  for (uint8_t i = 0; i < CLOCK_LATCHES_COUNT; ++i) {
    CHECK(harness.read_reg(slot2, i) == 0);
  }

  // Null instance safety
  size_t dummy_size = 32;
  std::array<uint8_t, 32> dummy_buf{};
  CHECK(clock_descriptor()->save_state(nullptr, dummy_buf.data(),
                                       &dummy_size) == peripheral_error);
  CHECK(clock_descriptor()->load_state(nullptr, dummy_buf.data(), dummy_size) ==
        peripheral_error);
  clock_descriptor()->reset(nullptr);
  clock_descriptor()->shutdown(nullptr);
}

}  // namespace
