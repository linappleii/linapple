// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "doctest.h"

extern "C" unsigned mouse_abi_c_frame_size(void);
extern "C" unsigned mouse_abi_c_state_version(void);
extern "C" unsigned mouse_abi_c_button_payload_size(void);
extern "C" uint32_t mouse_abi_c_set_button_id(void);
extern "C" uint32_t mouse_abi_c_is_active_query_id(void);

namespace {

// Through the registry, so one binary covers the built-in card and a loaded
// plugin alike.
auto mouse_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.mouse");
}

constexpr int test_slot = 4;
constexpr size_t frame_size = 92;
constexpr uint32_t foreign_command = 0x9999;
constexpr uint32_t unknown_mouse_id = PERIPHERAL_SUBSYSTEM_MOUSE | 0x7FFF;

class BenchHost_t {
 public:
  BenchHost_t() {
    host_.Log = bench_log;
    host_.AssertIrq = bench_assert_irq;
    host_.RegisterIO = bench_register_io;
    host_.RegisterCxROM = bench_register_cx_rom;
    host_.RegisterExpansionROM = bench_register_expansion_rom;
    host_.GetCycles = bench_get_cycles;
    host_.GetClockHz = bench_get_clock_hz;
    host_.ReadFloatingBus = bench_read_floating_bus;
    host_.ScheduleEvent = bench_schedule_event;
    s_last_log.clear();
  }

  auto host() -> HostInterface_t* { return &host_; }
  static auto last_log() -> const std::string& { return s_last_log; }

 private:
  static auto bench_log(void* instance, PeripheralLogLevel_t level,
                        const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    std::array<char, 256> line{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(line.data(), line.size(), fmt, args);
    va_end(args);
    s_last_log = line.data();
  }
  static auto bench_assert_irq(int slot, bool assert) -> void {
    (void)slot;
    (void)assert;
  }
  static auto bench_register_io(int slot, PeripheralIOHandler read_c0,
                                PeripheralIOHandler write_c0,
                                PeripheralIOHandler read_cx,
                                PeripheralIOHandler write_cx) -> void {
    (void)slot;
    (void)read_c0;
    (void)write_c0;
    (void)read_cx;
    (void)write_cx;
  }
  static auto bench_register_cx_rom(int slot, const uint8_t* rom) -> void {
    (void)slot;
    (void)rom;
  }
  static auto bench_register_expansion_rom(int slot, const uint8_t* rom)
      -> void {
    (void)slot;
    (void)rom;
  }
  static auto bench_get_cycles() -> uint64_t { return 0; }
  static auto bench_get_clock_hz() -> double { return 1020484.45; }
  static auto bench_read_floating_bus(uint32_t executed_cycles) -> uint8_t {
    (void)executed_cycles;
    return 0;
  }
  static auto bench_schedule_event(void* instance, uint64_t at_cycle) -> void {
    (void)instance;
    (void)at_cycle;
  }

  static std::string s_last_log;
  HostInterface_t host_{};
};

std::string BenchHost_t::s_last_log;

class BenchCard_t {
 public:
  explicit BenchCard_t(int slot = test_slot)
      : instance_(mouse_descriptor()->init(slot, host_.host())) {}
  ~BenchCard_t() {
    if (instance_ != nullptr) {
      mouse_descriptor()->shutdown(instance_);
    }
  }
  BenchCard_t(const BenchCard_t&) = delete;
  auto operator=(const BenchCard_t&) -> BenchCard_t& = delete;
  BenchCard_t(BenchCard_t&&) = delete;
  auto operator=(BenchCard_t&&) -> BenchCard_t& = delete;

  auto instance() const -> void* { return instance_; }

 private:
  BenchHost_t host_;
  void* instance_;
};

}  // namespace

TEST_CASE("Mouse card: the registry resolves the card by id and by name") {
  Peripheral_t* by_id = peripheral_find_internal("linapple.mouse");
  REQUIRE(by_id != nullptr);
  CHECK(peripheral_find_internal("Mouse Interface") == by_id);
}

TEST_CASE(
    "Mouse card: the descriptor identifies the card and carries every entry "
    "point") {
  const Peripheral_t* desc = mouse_descriptor();
  REQUIRE(desc != nullptr);
  CHECK(desc->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(desc->id) == "linapple.mouse");
  CHECK(std::string(desc->name) == "Mouse Interface");
  CHECK(desc->compatible_slots == PERIPHERAL_MASK_EXPANSION);
  CHECK(desc->default_slot == test_slot);
  CHECK(desc->init != nullptr);
  CHECK(desc->reset != nullptr);
  CHECK(desc->shutdown != nullptr);
  CHECK(desc->save_state != nullptr);
  CHECK(desc->load_state != nullptr);
  CHECK(desc->command != nullptr);
  CHECK(desc->query != nullptr);
}

TEST_CASE("Mouse card: a null instance or an empty host is refused") {
  const Peripheral_t* desc = mouse_descriptor();
  REQUIRE(desc != nullptr);

  CHECK(desc->init(test_slot, nullptr) == nullptr);
  HostInterface_t empty_host{};
  CHECK(desc->init(test_slot, &empty_host) == nullptr);

  desc->reset(nullptr);
  desc->shutdown(nullptr);

  size_t size = 0;
  uint8_t byte = 0;
  CHECK(desc->save_state(nullptr, &byte, &size) == peripheral_error);
  CHECK(desc->load_state(nullptr, &byte, frame_size) == peripheral_error);
  CHECK(desc->command(nullptr, mouse_cmd_set_button, &byte, 1) ==
        peripheral_error);
  CHECK(desc->query(nullptr, mouse_query_is_active, &byte, &size) ==
        peripheral_error);
}

TEST_CASE(
    "Mouse card: a command or query outside the card's subsystem, or unknown "
    "inside it, is incompatible") {
  BenchCard_t card;
  REQUIRE(card.instance() != nullptr);
  const Peripheral_t* desc = mouse_descriptor();

  MouseButtonPayload_t payload{0, true, {0, 0}};
  size_t size = sizeof(payload);
  CHECK(desc->command(card.instance(), foreign_command, &payload,
                      sizeof(payload)) == peripheral_incompatible);
  CHECK(desc->query(card.instance(), foreign_command, &payload, &size) ==
        peripheral_incompatible);
  CHECK(desc->command(card.instance(), unknown_mouse_id, &payload,
                      sizeof(payload)) == peripheral_incompatible);
  CHECK(desc->query(card.instance(), unknown_mouse_id, &payload, &size) ==
        peripheral_incompatible);
}

TEST_CASE("Mouse card: save_state's sizing probe answers the frame's size") {
  BenchCard_t card;
  REQUIRE(card.instance() != nullptr);
  const Peripheral_t* desc = mouse_descriptor();

  size_t size = 0;
  CHECK(desc->save_state(card.instance(), nullptr, &size) == peripheral_ok);
  CHECK(size == frame_size);
  CHECK(size == sizeof(MouseSaveState_t));
  CHECK(mouse_abi_c_frame_size() == frame_size);
  CHECK(mouse_abi_c_state_version() == MOUSE_STATE_VERSION);

  std::array<uint8_t, frame_size> frame{};
  size = frame_size - 1;
  CHECK(desc->save_state(card.instance(), frame.data(), &size) ==
        peripheral_error);
  CHECK(size == frame_size);

  size = frame_size;
  CHECK(desc->save_state(card.instance(), frame.data(), &size) ==
        peripheral_ok);
  CHECK(size == frame_size);
  CHECK(frame.at(0) == MOUSE_STATE_VERSION);
  CHECK(frame.at(4) == frame_size);
}

TEST_CASE(
    "Mouse card: the is-active query's sizing probe and the button payload "
    "agree with the C99 view") {
  BenchCard_t card;
  REQUIRE(card.instance() != nullptr);
  const Peripheral_t* desc = mouse_descriptor();

  CHECK(mouse_abi_c_is_active_query_id() == mouse_query_is_active);
  CHECK(mouse_abi_c_set_button_id() == mouse_cmd_set_button);
  CHECK(mouse_abi_c_button_payload_size() == sizeof(MouseButtonPayload_t));

  size_t size = 0;
  CHECK(desc->query(card.instance(), mouse_query_is_active, nullptr, &size) ==
        peripheral_ok);
  CHECK(size == sizeof(uint8_t));

  uint8_t active = 0;
  size = 0;
  CHECK(desc->query(card.instance(), mouse_query_is_active, &active, &size) ==
        peripheral_error);
  CHECK(size == sizeof(uint8_t));

  size = sizeof(uint8_t);
  CHECK(desc->query(card.instance(), mouse_query_is_active, &active, &size) ==
        peripheral_ok);
  CHECK(active == 1);
}
