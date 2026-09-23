// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/clock/ClockCardCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

// The card is reached the way the emulator reaches it, through the registry,
// so one test binary covers the built-in card and the loaded plugin alike.
auto clock_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.clock");
}

constexpr uint16_t io_base_address = 0xC080;
constexpr int io_slot_shift = 4;
constexpr int registers_per_slot = 16;
constexpr size_t slot_rom_size = 256;

constexpr int test_slot_1 = 4;
constexpr int test_slot_2 = 5;

constexpr uint8_t strobe_offset = 0x0F;
constexpr uint8_t first_unmapped_offset = 0x0A;
constexpr uint8_t last_unmapped_offset = 0x0E;

constexpr size_t sig_offset_0 = 0x00;
constexpr size_t sig_offset_2 = 0x02;
constexpr size_t sig_offset_4 = 0x04;
constexpr size_t sig_offset_6 = 0x06;

constexpr uint8_t prodos_sig_0 = 0x08;
constexpr uint8_t prodos_sig_2 = 0x28;
constexpr uint8_t prodos_sig_4 = 0x58;
constexpr uint8_t prodos_sig_6 = 0x70;

constexpr size_t latch_count = CLOCKCARD_LATCH_COUNT;
constexpr size_t latch_month = 0;
constexpr size_t latch_weekday = 2;
constexpr size_t latch_day = 4;
constexpr size_t latch_hour = 6;
constexpr size_t latch_minute = 8;
constexpr uint8_t bcd_digit_max = 9;

constexpr size_t frame_size = 32;
constexpr size_t frame_latches_offset = 16;
constexpr size_t frame_header_size = 8;

// The Mockingboard's frame, the largest a slot buffer is sized for today.
constexpr size_t mockingboard_frame_size = 232;

using Latches_t = std::array<uint8_t, latch_count>;
using Frame_t = std::array<uint8_t, frame_size>;

// The frozen time every literal below encodes:
//   date -u -d @1773325800 -> Thu Mar 12 14:30:00 UTC 2026
// so the latches read month 03, weekday 04 (Thursday, Sunday = 0), day 12,
// hour 14, minute 30, one BCD digit per register.
constexpr Latches_t frozen_latches = {0, 3, 0, 4, 1, 2, 1, 4, 3, 0};

// The same instant as the host hands it over: already local, so the card
// copies the fields and applies no offset. unix_seconds and the offset are
// carried for completeness; the card never reads them.
constexpr HostLocalTime_t frozen_thursday = {1773325800, 0,  2026, 3, 12,
                                             4,          14, 30,   0};

// date -u -d @1795910340 -> Sat Nov 28 23:59:00 UTC 2026
constexpr HostLocalTime_t frozen_saturday = {1795910340, 0,  2026, 11, 28,
                                             6,          23, 59,   0};
constexpr Latches_t saturday_latches = {1, 1, 0, 6, 2, 8, 2, 3, 5, 9};

// date -u -d @1709164800 -> Thu Feb 29 00:00:00 UTC 2024, a leap day
constexpr HostLocalTime_t frozen_leap_day = {1709164800, 0, 2024, 2, 29,
                                             4,          0, 0,    0};
constexpr Latches_t leap_day_latches = {0, 2, 0, 4, 2, 9, 0, 0, 0, 0};

// What the mock host says the undriven bus holds on a given cycle: a value
// that no latch register can produce, so a pass-through is unmistakable.
auto floating_bus_marker(uint32_t executed_cycles) -> uint8_t {
  return static_cast<uint8_t>(0xA0 | (executed_cycles & 0x1F));
}

// Slot 4 on a real machine: its sixteen I/O registers start at $C0C0.
constexpr int oracle_slot = 4;
constexpr uint16_t oracle_strobe = 0xC0CF;
constexpr uint16_t oracle_hole = 0xC0CC;
constexpr uint16_t oracle_first_latch = 0xC0C0;
constexpr uint32_t oracle_strobe_cycle = 42;
constexpr uint32_t oracle_hole_cycle = 100;
constexpr uint8_t oracle_strobe_marker = 0x5A;
constexpr uint8_t oracle_hole_marker = 0x3C;

// Version 1: version, struct_size, the eight-byte epoch pin (always zero now),
// the ten latches, the pin flag (always zero now), five reserved bytes.
constexpr Frame_t frozen_frame = {
    0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x04, 0x01, 0x02,
    0x01, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// tests/fixtures/clock-frame-v1.bin is the frame an earlier card wrote for
// the same latches with its epoch pin engaged (fixed_epoch 0x69B2CDE8 =
// 1773325800 little-endian at byte 8, use_fixed_epoch = 1 at byte 26),
// written as two printf calls, the first redirected with > and the second
// with >>:
//   printf '\x01\x00\x00\x00\x20\x00\x00\x00\xE8\xCD\xB2\x69\x00\x00\x00\x00'
//   printf '\x00\x03\x00\x04\x01\x02\x01\x04\x03\x00\x01\x00\x00\x00\x00\x00'
// and pinned by test-clock-frame-v1-pinned (SHA-1
// b04c4dcef167bb1e9094e7fc452e0bac61e6669c).
constexpr std::array<uint8_t, 12> frame_v1_prefix = {
    0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0xE8, 0xCD, 0xB2, 0x69};

// A command or query id the card never defined, plus the two commands and
// two queries an earlier card did define.
constexpr uint32_t unknown_command = 0xDEAD;
constexpr uint32_t unknown_query = 0xBEEF;
constexpr uint32_t retired_cmd_set_epoch = 0x0001;
constexpr uint32_t retired_cmd_clear_epoch = 0x0002;
constexpr uint32_t retired_query_epoch = 0x0100;
constexpr uint32_t retired_query_time = 0x0101;

struct MockHandler_t {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockHandler_t() = default;
  MockHandler_t(void* inst, PeripheralIOHandler r, PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

class ClockHarness_t {
 public:
  ClockHarness_t() {
    s_active_harness = this;
    REQUIRE(clock_descriptor() != nullptr);
    host_.Log = mock_log;
    host_.AssertIrq = mock_assert_irq;
    host_.RegisterIO = mock_register_io;
    host_.RegisterCxROM = mock_register_cx_rom;
    host_.RegisterExpansionROM = mock_register_expansion_rom;
    host_.RegisterDirectIO = mock_register_direct_io;
    host_.ReadFloatingBus = mock_read_floating_bus;
    host_.GetLocalTime = mock_host_clock;
    time_ = frozen_thursday;
  }

  ~ClockHarness_t() {
    for (auto& slot_inst : instances_) {
      if (slot_inst.second != nullptr) {
        clock_descriptor()->shutdown(slot_inst.second);
      }
    }
    instances_.clear();
    s_active_harness = nullptr;
  }

  ClockHarness_t(const ClockHarness_t&) = delete;
  auto operator=(const ClockHarness_t&) -> ClockHarness_t& = delete;
  ClockHarness_t(ClockHarness_t&&) = delete;
  auto operator=(ClockHarness_t&&) -> ClockHarness_t& = delete;

  auto host() -> HostInterface_t* { return &host_; }

  auto freeze_clock(const HostLocalTime_t& frozen) -> void {
    time_ = frozen;
    has_time_ = true;
  }
  auto stop_clock() -> void { has_time_ = false; }
  auto time_calls() const -> unsigned { return time_calls_; }
  auto log_messages() const -> const std::vector<std::string>& {
    return log_messages_;
  }

  auto create_clock(int slot) -> void* {
    void* instance = clock_descriptor()->init(slot, &host_);
    if (instance != nullptr) {
      instances_[slot] = instance;
      const uint16_t base = io_base_address + (slot << io_slot_shift);
      for (uint16_t i = 0; i < registers_per_slot; ++i) {
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

  auto load_frame(int slot, const void* frame, size_t size)
      -> PeripheralStatus_t {
    return clock_descriptor()->load_state(get_instance(slot), frame, size);
  }

  auto save_frame(int slot) -> Frame_t {
    Frame_t frame{};
    size_t size = frame.size();
    REQUIRE(clock_descriptor()->save_state(get_instance(slot), frame.data(),
                                           &size) == peripheral_ok);
    REQUIRE(size == frame_size);
    return frame;
  }

  auto strobe(int slot, uint32_t executed_cycles = 0) -> uint8_t {
    return read_reg(slot, strobe_offset, executed_cycles);
  }

  auto read_io(uint16_t addr, uint32_t executed_cycles = 0) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      return it->second.read(it->second.instance, 0, addr, 0, 0,
                             executed_cycles);
    }
    return 0;
  }

  auto read_reg(int slot, uint8_t offset, uint32_t executed_cycles = 0)
      -> uint8_t {
    const uint16_t addr = io_base_address + (slot << io_slot_shift) + offset;
    return read_io(addr, executed_cycles);
  }

  auto latches(int slot) -> Latches_t {
    Latches_t out{};
    for (size_t i = 0; i < latch_count; ++i) {
      out.at(i) = read_reg(slot, static_cast<uint8_t>(i));
    }
    return out;
  }

  auto has_handler(uint16_t addr) const -> bool {
    return handlers_.find(addr) != handlers_.end();
  }

  auto get_handler(uint16_t addr) const -> const MockHandler_t& {
    return handlers_.at(addr);
  }

  auto has_rom(int slot) const -> bool {
    return roms_.find(slot) != roms_.end();
  }

  auto get_rom(int slot) const -> const std::vector<uint8_t>& {
    return roms_.at(slot);
  }

 private:
  HostInterface_t host_{};
  HostLocalTime_t time_{};
  bool has_time_ = true;
  unsigned time_calls_ = 0;
  std::vector<std::string> log_messages_;
  std::map<uint16_t, MockHandler_t> handlers_;
  std::map<int, std::vector<uint8_t>> roms_;
  std::map<int, void*> instances_;

  static ClockHarness_t* s_active_harness;

  // NOLINTBEGIN(cert-dcl50-cpp, cppcoreguidelines-pro-type-vararg)
  // Justification: Log is variadic in the HostInterface_t ABI.
  static auto mock_log(void* instance, PeripheralLogLevel_t level,
                       const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    if (s_active_harness == nullptr) {
      return;
    }
    std::array<char, 256> text{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(text.data(), text.size(), fmt, args);
    va_end(args);
    s_active_harness->log_messages_.emplace_back(text.data());
  }
  // NOLINTEND(cert-dcl50-cpp, cppcoreguidelines-pro-type-vararg)

  static auto mock_read_floating_bus(uint32_t executed_cycles) -> uint8_t {
    return floating_bus_marker(executed_cycles);
  }

  static auto mock_host_clock(HostLocalTime_t* out) -> bool {
    if (s_active_harness == nullptr || out == nullptr) {
      return false;
    }
    ++s_active_harness->time_calls_;
    if (!s_active_harness->has_time_) {
      return false;
    }
    *out = s_active_harness->time_;
    return true;
  }

  static auto mock_assert_irq(int slot, bool assert_irq) -> void {
    (void)slot;
    (void)assert_irq;
  }

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  // Justification: Signature is required by HostInterface_t ABI.
  static auto mock_register_io(int slot, PeripheralIOHandler read_c0,
                               PeripheralIOHandler write_c0,
                               PeripheralIOHandler read_cx,
                               PeripheralIOHandler write_cx) -> void {
    (void)read_cx;
    (void)write_cx;
    if (s_active_harness != nullptr &&
        (read_c0 != nullptr || write_c0 != nullptr)) {
      const uint16_t base = io_base_address + (slot << io_slot_shift);
      for (uint16_t i = 0; i < registers_per_slot; ++i) {
        s_active_harness->handlers_[base + i] = {nullptr, read_c0, write_c0};
      }
    }
  }
  // NOLINTEND(bugprone-easily-swappable-parameters)

  static auto mock_register_cx_rom(int slot, const uint8_t* rom_ptr) -> void {
    if (s_active_harness != nullptr && rom_ptr != nullptr) {
      std::vector<uint8_t> rom_data(slot_rom_size);
      std::copy_n(rom_ptr, slot_rom_size, rom_data.begin());
      s_active_harness->roms_[slot] = std::move(rom_data);
    }
  }

  static auto mock_register_expansion_rom(int slot, uint8_t* rom_ptr) -> void {
    (void)slot;
    (void)rom_ptr;
  }

  static auto mock_register_direct_io(void* instance, uint16_t addr,
                                      PeripheralIOHandler read,
                                      PeripheralIOHandler write) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->handlers_[addr] = {instance, read, write};
    }
  }
};

ClockHarness_t* ClockHarness_t::s_active_harness = nullptr;

auto read_fixture_frame(const char* name) -> Frame_t {
  Frame_t frame{};
  std::ifstream in(TestFixtures::get_fixture_path(name), std::ios::binary);
  REQUIRE(in.is_open());
  in.read(reinterpret_cast<char*>(frame.data()),
          static_cast<std::streamsize>(frame.size()));
  REQUIRE(in.gcount() == static_cast<std::streamsize>(frame.size()));
  return frame;
}

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

TEST_CASE("Clock Peripheral: A [Slots] line still names the card as before") {
  auto* descriptor = clock_descriptor();
  REQUIRE(descriptor != nullptr);
  CHECK(std::strcmp(descriptor->description,
                    "ThunderClock-compatible ProDOS clock") == 0);

  // Every existing configuration and .aws manifest names the card by these
  // two strings; the descriptor's name and id are part of that contract.
  CHECK(peripheral_find_internal("Clock Card") == descriptor);
  CHECK(peripheral_find_internal("linapple.clock") == descriptor);
  CHECK(peripheral_find_internal("Clock") == descriptor);
  CHECK(peripheral_find_internal("No-Slot Clock") == nullptr);
}

TEST_CASE("Clock Peripheral: Slot ROM Contract") {
  ClockHarness_t harness;
  const int slot = test_slot_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  const uint16_t base = io_base_address + (slot << io_slot_shift);
  for (uint16_t addr = base; addr < base + registers_per_slot; ++addr) {
    CHECK(harness.has_handler(addr));
    CHECK(harness.get_handler(addr).read != nullptr);
  }

  REQUIRE(harness.has_rom(slot));
  const auto& rom = harness.get_rom(slot);
  REQUIRE(rom.size() == slot_rom_size);
  CHECK(rom.at(sig_offset_0) == prodos_sig_0);
  CHECK(rom.at(sig_offset_2) == prodos_sig_2);
  CHECK(rom.at(sig_offset_4) == prodos_sig_4);
  CHECK(rom.at(sig_offset_6) == prodos_sig_6);
}

TEST_CASE("Clock Peripheral: Bus Fidelity and Floating Bus Pass-Through") {
  ClockHarness_t harness;
  const int slot = test_slot_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);

  // The strobe latches the time but drives nothing onto the bus.
  CHECK(harness.strobe(slot, oracle_strobe_cycle) ==
        floating_bus_marker(oracle_strobe_cycle));
  CHECK(harness.latches(slot) == frozen_latches);

  for (uint8_t offset = first_unmapped_offset; offset <= last_unmapped_offset;
       ++offset) {
    CHECK(harness.read_reg(slot, offset, oracle_hole_cycle) ==
          floating_bus_marker(oracle_hole_cycle));
  }

  // The bus byte follows the cycle, not the register.
  CHECK(harness.read_reg(slot, first_unmapped_offset, 7) ==
        floating_bus_marker(7));
  CHECK(floating_bus_marker(7) != floating_bus_marker(oracle_hole_cycle));
}

TEST_CASE("Clock Peripheral: The state frame is 32 bytes with latches at 16") {
  static_assert(sizeof(ClockCardSaveState_t) == frame_size);
  static_assert(offsetof(ClockCardSaveState_t, version) == 0);
  static_assert(offsetof(ClockCardSaveState_t, struct_size) == 4);
  static_assert(offsetof(ClockCardSaveState_t, fixed_epoch) ==
                frame_header_size);
  static_assert(offsetof(ClockCardSaveState_t, latches) ==
                frame_latches_offset);
  static_assert(offsetof(ClockCardSaveState_t, use_fixed_epoch) ==
                frame_latches_offset + latch_count);
  static_assert(offsetof(ClockCardSaveState_t, reserved) ==
                frame_latches_offset + latch_count + 1);
  CHECK(CLOCKCARD_STATE_VERSION == 1);
  CHECK(frozen_frame.at(0) == CLOCKCARD_STATE_VERSION);
  CHECK(frozen_frame.at(4) == frame_size);
  CHECK(std::equal(frozen_latches.begin(), frozen_latches.end(),
                   frozen_frame.begin() + frame_latches_offset));
}

TEST_CASE("Clock Peripheral: Saving the frozen latches gives the literal") {
  ClockHarness_t harness;
  const int slot = test_slot_1;
  REQUIRE(harness.create_clock(slot) != nullptr);
  REQUIRE(harness.load_frame(slot, frozen_frame.data(), frozen_frame.size()) ==
          peripheral_ok);
  CHECK(harness.latches(slot) == frozen_latches);

  size_t probe = 0;
  CHECK(clock_descriptor()->save_state(harness.get_instance(slot), nullptr,
                                       &probe) == peripheral_ok);
  CHECK(probe == frame_size);

  size_t too_small = frame_size - 1;
  std::array<uint8_t, frame_size - 1> small{};
  CHECK(clock_descriptor()->save_state(harness.get_instance(slot), small.data(),
                                       &too_small) == peripheral_error);

  CHECK(harness.save_frame(slot) == frozen_frame);

  // A slot buffer sized for a bigger card takes the same 32 bytes and
  // reports 32, leaving the rest of the buffer alone.
  std::vector<uint8_t> slot_buffer(mockingboard_frame_size, 0xA5);
  size_t written = slot_buffer.size();
  CHECK(clock_descriptor()->save_state(harness.get_instance(slot),
                                       slot_buffer.data(),
                                       &written) == peripheral_ok);
  CHECK(written == frame_size);
  CHECK(std::equal(frozen_frame.begin(), frozen_frame.end(),
                   slot_buffer.begin()));
  CHECK(std::all_of(slot_buffer.begin() + frame_size, slot_buffer.end(),
                    [](uint8_t byte) { return byte == 0xA5; }));
}

TEST_CASE(
    "Clock Peripheral: A frame with its pin engaged loads its latches, not its "
    "pin") {
  const Frame_t pinned = read_fixture_frame("clock-frame-v1.bin");
  REQUIRE(std::equal(frame_v1_prefix.begin(), frame_v1_prefix.end(),
                     pinned.begin()));

  ClockHarness_t harness;
  const int slot = test_slot_1;
  REQUIRE(harness.create_clock(slot) != nullptr);
  REQUIRE(harness.load_frame(slot, pinned.data(), pinned.size()) ==
          peripheral_ok);
  CHECK(harness.latches(slot) == frozen_latches);

  // Re-saved, the pin fields are zero: the card has no pin to report.
  CHECK(harness.save_frame(slot) == frozen_frame);
}

TEST_CASE("Clock Peripheral: A slot buffer larger than the frame loads") {
  ClockHarness_t harness;
  const int slot = test_slot_1;
  REQUIRE(harness.create_clock(slot) != nullptr);

  std::vector<uint8_t> slot_buffer(mockingboard_frame_size, 0xA5);
  std::copy(frozen_frame.begin(), frozen_frame.end(), slot_buffer.begin());
  CHECK(harness.load_frame(slot, slot_buffer.data(), slot_buffer.size()) ==
        peripheral_ok);
  CHECK(harness.latches(slot) == frozen_latches);

  std::vector<uint8_t> one_over(frozen_frame.begin(), frozen_frame.end());
  one_over.push_back(0xA5);
  CHECK(harness.load_frame(slot, one_over.data(), one_over.size()) ==
        peripheral_ok);
}

TEST_CASE("Clock Peripheral: A rejected load leaves the latches as they were") {
  ClockHarness_t harness;
  const int slot = test_slot_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);
  REQUIRE(harness.load_frame(slot, frozen_frame.data(), frozen_frame.size()) ==
          peripheral_ok);

  auto rejects = [&](const Frame_t& frame, size_t size) {
    CHECK(harness.load_frame(slot, frame.data(), size) == peripheral_error);
    CHECK(harness.latches(slot) == frozen_latches);
  };
  auto corrupted = [](size_t index, uint8_t value) {
    Frame_t frame = frozen_frame;
    frame.at(index) = value;
    return frame;
  };

  SUBCASE("a buffer shorter than the frame") {
    rejects(frozen_frame, frame_size - 1);
    rejects(frozen_frame, frame_header_size - 1);
    rejects(frozen_frame, 0);
  }
  SUBCASE("null buffer, null instance") {
    CHECK(harness.load_frame(slot, nullptr, frame_size) == peripheral_error);
    CHECK(clock_descriptor()->load_state(nullptr, frozen_frame.data(),
                                         frame_size) == peripheral_error);
    CHECK(harness.latches(slot) == frozen_latches);
  }
  SUBCASE("an unknown version") {
    // No version 2 was ever written, so none is accepted.
    rejects(corrupted(0, 0), frame_size);
    rejects(corrupted(0, 2), frame_size);
    rejects(corrupted(0, 3), frame_size);
    rejects(corrupted(1, 0xE7), frame_size);
  }
  SUBCASE("a struct_size that is not 32") {
    rejects(corrupted(4, 0x10), frame_size);
    rejects(corrupted(4, 0x1A), frame_size);
    rejects(corrupted(4, 0x39), frame_size);
    rejects(corrupted(5, 0x30), frame_size);
  }
  SUBCASE("a digit above 9") {
    rejects(corrupted(frame_latches_offset + latch_month, 15), frame_size);
    rejects(corrupted(frame_latches_offset + latch_minute + 1, 10), frame_size);
  }
  SUBCASE("a weekday tens digit") {
    rejects(corrupted(frame_latches_offset + latch_weekday, 1), frame_size);
  }
  SUBCASE("month 15, weekday 7, day 35, hour 25, minute 60") {
    Frame_t month = frozen_frame;
    month.at(frame_latches_offset + latch_month) = 1;
    month.at(frame_latches_offset + latch_month + 1) = 5;
    rejects(month, frame_size);
    rejects(corrupted(frame_latches_offset + latch_weekday + 1, 7), frame_size);
    Frame_t day = frozen_frame;
    day.at(frame_latches_offset + latch_day) = 3;
    day.at(frame_latches_offset + latch_day + 1) = 5;
    rejects(day, frame_size);
    Frame_t hour = frozen_frame;
    hour.at(frame_latches_offset + latch_hour) = 2;
    hour.at(frame_latches_offset + latch_hour + 1) = 5;
    rejects(hour, frame_size);
    Frame_t minute = frozen_frame;
    minute.at(frame_latches_offset + latch_minute) = 6;
    minute.at(frame_latches_offset + latch_minute + 1) = 0;
    rejects(minute, frame_size);
  }
}

TEST_CASE("Clock Peripheral: The retired epoch commands answer incompatible") {
  ClockHarness_t harness;
  const int slot = test_slot_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);
  auto* descriptor = clock_descriptor();

  const uint64_t payload = 1773325800ULL;
  CHECK(descriptor->command(instance, retired_cmd_set_epoch, &payload,
                            sizeof(payload)) == peripheral_incompatible);
  CHECK(descriptor->command(instance, retired_cmd_clear_epoch, nullptr, 0) ==
        peripheral_incompatible);
  CHECK(descriptor->command(instance, unknown_command, nullptr, 0) ==
        peripheral_incompatible);
  CHECK(descriptor->command(nullptr, retired_cmd_set_epoch, &payload,
                            sizeof(payload)) == peripheral_error);

  std::array<uint8_t, 16> out{};
  size_t out_size = out.size();
  CHECK(descriptor->query(instance, retired_query_epoch, out.data(),
                          &out_size) == peripheral_incompatible);
  CHECK(descriptor->query(instance, retired_query_time, out.data(),
                          &out_size) == peripheral_incompatible);
  CHECK(descriptor->query(instance, unknown_query, nullptr, &out_size) ==
        peripheral_incompatible);
  CHECK(out_size == out.size());
  CHECK(descriptor->query(instance, retired_query_time, out.data(), nullptr) ==
        peripheral_error);

  // Nothing above touched the card.
  CHECK(harness.latches(slot) == Latches_t{});
}

// A real clock keeps time through RESET; the firmware strobes before every
// read, so a zeroed latch bank would never be seen by ProDOS anyway.
TEST_CASE("Clock Peripheral: Reset keeps the latched time") {
  ClockHarness_t harness;
  const int slot = test_slot_1;
  void* instance = harness.create_clock(slot);
  REQUIRE(instance != nullptr);
  harness.strobe(slot);
  REQUIRE(harness.latches(slot) == frozen_latches);

  clock_descriptor()->reset(instance);
  CHECK(harness.latches(slot) == frozen_latches);
  CHECK(harness.time_calls() == 1);
}

TEST_CASE("Clock Peripheral: The frozen host time drives the strobe") {
  ClockHarness_t harness;
  const int slot = test_slot_1;
  REQUIRE(harness.create_clock(slot) != nullptr);
  CHECK(harness.latches(slot) == Latches_t{});
  CHECK(harness.time_calls() == 0);

  harness.strobe(slot);
  CHECK(harness.time_calls() == 1);
  CHECK(harness.latches(slot) == frozen_latches);

  harness.freeze_clock(frozen_saturday);
  harness.strobe(slot);
  CHECK(harness.latches(slot) == saturday_latches);

  harness.freeze_clock(frozen_leap_day);
  harness.strobe(slot);
  CHECK(harness.latches(slot) == leap_day_latches);
  CHECK(harness.time_calls() == 3);

  // Reading the latches never asks the host again; only the strobe does.
  CHECK(harness.time_calls() == 3);
}

TEST_CASE("Clock Peripheral: A host without a time leaves the latches alone") {
  ClockHarness_t harness;
  const int slot = test_slot_1;
  REQUIRE(harness.create_clock(slot) != nullptr);
  harness.strobe(slot);
  REQUIRE(harness.latches(slot) == frozen_latches);
  REQUIRE(harness.log_messages().empty());

  harness.stop_clock();
  CHECK(harness.strobe(slot, oracle_strobe_cycle) ==
        floating_bus_marker(oracle_strobe_cycle));
  CHECK(harness.latches(slot) == frozen_latches);
  harness.strobe(slot);
  CHECK(harness.latches(slot) == frozen_latches);
  CHECK(harness.time_calls() == 3);

  // Said once, not on every strobe.
  REQUIRE(harness.log_messages().size() == 1);
  CHECK(harness.log_messages().front().find("slot 4") != std::string::npos);
  CHECK(harness.log_messages().front().find("local time") != std::string::npos);

  harness.freeze_clock(frozen_saturday);
  harness.strobe(slot);
  CHECK(harness.latches(slot) == saturday_latches);
}

TEST_CASE(
    "Clock Peripheral: A host missing a member gets no card, and hears why") {
  ClockHarness_t harness;
  struct Missing_t {
    const char* name;
    void (*strip)(HostInterface_t*);
  };
  const std::array<Missing_t, 4> members = {{
      {"RegisterIO", [](HostInterface_t* h) { h->RegisterIO = nullptr; }},
      {"RegisterCxROM", [](HostInterface_t* h) { h->RegisterCxROM = nullptr; }},
      {"GetLocalTime", [](HostInterface_t* h) { h->GetLocalTime = nullptr; }},
      {"ReadFloatingBus",
       [](HostInterface_t* h) { h->ReadFloatingBus = nullptr; }},
  }};

  for (const Missing_t& member : members) {
    CAPTURE(member.name);
    HostInterface_t partial = *harness.host();
    member.strip(&partial);
    const size_t logged_before = harness.log_messages().size();
    CHECK(clock_descriptor()->init(test_slot_1, &partial) == nullptr);
    REQUIRE(harness.log_messages().size() == logged_before + 1);
    CHECK(harness.log_messages().back().find(member.name) != std::string::npos);
    CHECK(harness.log_messages().back().find("slot 4") != std::string::npos);
  }

  // A host that cannot even log is still refused, silently.
  HostInterface_t mute = *harness.host();
  mute.Log = nullptr;
  mute.GetLocalTime = nullptr;
  CHECK(clock_descriptor()->init(test_slot_1, &mute) == nullptr);

  // The complete host still gets its card.
  CHECK(harness.create_clock(test_slot_2) != nullptr);
}

// The other cases hand the card a mock host. This one puts the card in slot
// 4 of a real Enhanced //e and reads it the way the 6502 does, through the
// I/O dispatcher, so the bytes on the undriven bus and the frozen clock both
// arrive by the host interface the core actually builds.
TEST_CASE("Clock Peripheral: The real bus and the frozen clock reach slot 4") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots.at(oracle_slot - 1) = "Clock Card";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  peripheral_manager_init();
  linapple_register_peripherals();
  TestFixtures::ScopedLocalTimeProvider_t clock(frozen_thursday);

  const uint16_t strobe_fetch =
      video_get_scanner_address(nullptr, oracle_strobe_cycle);
  const uint16_t hole_fetch =
      video_get_scanner_address(nullptr, oracle_hole_cycle);
  REQUIRE(strobe_fetch != hole_fetch);
  TestFixtures::ScopedCore_t::poke(strobe_fetch, &oracle_strobe_marker, 1);
  TestFixtures::ScopedCore_t::poke(hole_fetch, &oracle_hole_marker, 1);

  CHECK(io_map_dispatch(0, oracle_strobe, 0, 0, oracle_strobe_cycle) ==
        oracle_strobe_marker);
  CHECK(io_map_dispatch(0, oracle_hole, 0, 0, oracle_hole_cycle) ==
        oracle_hole_marker);
  CHECK(clock.calls() == 1);

  Latches_t latched{};
  for (size_t i = 0; i < latch_count; ++i) {
    latched.at(i) = io_map_dispatch(
        0, static_cast<uint16_t>(oracle_first_latch + i), 0, 0, 0);
  }
  CHECK(latched == frozen_latches);
  CHECK(clock.calls() == 1);
}

TEST_CASE("Clock Peripheral: Multi-Card Concurrency and Lifecycle Robustness") {
  ClockHarness_t harness;

  CHECK(clock_descriptor()->init(test_slot_1, nullptr) == nullptr);

  const int slot1 = test_slot_1;
  const int slot2 = test_slot_2;
  void* instance1 = harness.create_clock(slot1);
  void* instance2 = harness.create_clock(slot2);
  REQUIRE(instance1 != nullptr);
  REQUIRE(instance2 != nullptr);

  REQUIRE(harness.load_frame(slot1, frozen_frame.data(), frozen_frame.size()) ==
          peripheral_ok);
  CHECK(harness.latches(slot1) == frozen_latches);
  CHECK(harness.latches(slot2) == Latches_t{});

  size_t dummy_size = frame_size;
  Frame_t dummy_buf{};
  CHECK(clock_descriptor()->save_state(nullptr, dummy_buf.data(),
                                       &dummy_size) == peripheral_error);
  CHECK(clock_descriptor()->load_state(nullptr, dummy_buf.data(), dummy_size) ==
        peripheral_error);
  clock_descriptor()->reset(nullptr);
  clock_descriptor()->shutdown(nullptr);
}

}  // namespace

extern "C" auto clockcard_abi_c_state_size() -> size_t;
extern "C" auto clockcard_abi_c_fixed_epoch_offset() -> size_t;
extern "C" auto clockcard_abi_c_latches_offset() -> size_t;
extern "C" auto clockcard_abi_c_use_fixed_epoch_offset() -> size_t;
extern "C" auto clockcard_abi_c_reserved_offset() -> size_t;
extern "C" auto clockcard_abi_c_state_version() -> uint32_t;

TEST_CASE("Clock Peripheral: The C99 view of the state frame matches C++") {
  CHECK(clockcard_abi_c_state_size() == sizeof(ClockCardSaveState_t));
  CHECK(clockcard_abi_c_state_size() == frame_size);
  CHECK(clockcard_abi_c_fixed_epoch_offset() == frame_header_size);
  CHECK(clockcard_abi_c_latches_offset() == frame_latches_offset);
  CHECK(clockcard_abi_c_use_fixed_epoch_offset() ==
        frame_latches_offset + latch_count);
  CHECK(clockcard_abi_c_reserved_offset() ==
        frame_latches_offset + latch_count + 1);
  CHECK(clockcard_abi_c_state_version() == CLOCKCARD_STATE_VERSION);
}
