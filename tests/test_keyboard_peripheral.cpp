// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "doctest.h"

extern "C" unsigned keyboard_abi_c_frame_size(void);
extern "C" unsigned keyboard_abi_c_state_version(void);
extern "C" unsigned keyboard_abi_c_repeat_key_offset(void);
extern "C" unsigned keyboard_abi_c_latch_offset(void);
extern "C" unsigned keyboard_abi_c_strobe_offset(void);
extern "C" unsigned keyboard_abi_c_caps_lock_offset(void);
extern "C" unsigned keyboard_abi_c_auto_repeat_offset(void);

namespace {

constexpr uint16_t addr_keyboard_data = 0xC000;
constexpr uint8_t strobe_bit = 0x80;
constexpr size_t frame_size = 552;
constexpr uint32_t foreign_joystick_id = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0001;
constexpr uint32_t foreign_disk_id = PERIPHERAL_SUBSYSTEM_DISK | 0x0003;
// A scanner byte with bit 7 set: what the bus holds while the text page is
// full of normal spaces.
constexpr uint8_t bus_marker = 0xA0;

// Through the registry, so one binary covers the built-in card and a plugin.
auto keyboard_card() -> Peripheral_t* {
  Peripheral_t* descriptor = peripheral_find_internal("linapple.keyboard");
  REQUIRE(descriptor != nullptr);
  return descriptor;
}

struct BenchHandler_t {
  void* instance;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
};

// A host built by hand, for what the real one cannot show: the handlers a
// card registers and the status a call returns.
class BenchHost_t {
 public:
  BenchHost_t() {
    s_active = this;
    host_.Log = bench_log;
    host_.RegisterDirectIO = bench_register_direct_io;
    host_.ReadFloatingBus = bench_read_floating_bus;
    host_.GetCycles = bench_get_cycles;
    host_.GetClockHz = bench_get_clock_hz;
    host_.ScheduleEvent = bench_schedule_event;
  }

  ~BenchHost_t() {
    if (instance_ != nullptr) {
      keyboard_card()->shutdown(instance_);
    }
    s_active = nullptr;
  }

  BenchHost_t(const BenchHost_t&) = delete;
  auto operator=(const BenchHost_t&) -> BenchHost_t& = delete;
  BenchHost_t(BenchHost_t&&) = delete;
  auto operator=(BenchHost_t&&) -> BenchHost_t& = delete;

  auto host() -> HostInterface_t* { return &host_; }

  auto create() -> void* {
    instance_ = keyboard_card()->init(0, &host_);
    return instance_;
  }

  auto read(uint16_t addr) -> uint8_t {
    auto it = handlers_.find(addr);
    REQUIRE(it != handlers_.end());
    REQUIRE(it->second.read != nullptr);
    return it->second.read(it->second.instance, 0, addr, 0, 0, 0);
  }

 private:
  HostInterface_t host_{};
  std::map<uint16_t, BenchHandler_t> handlers_;
  void* instance_ = nullptr;

  static BenchHost_t* s_active;

  static auto bench_log(void* instance, PeripheralLogLevel_t level,
                        const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    (void)fmt;
  }

  static auto bench_register_direct_io(void* instance, uint16_t addr,
                                       PeripheralIOHandler read,
                                       PeripheralIOHandler write) -> void {
    if (s_active != nullptr) {
      s_active->handlers_[addr] = BenchHandler_t{instance, read, write};
    }
  }

  static auto bench_read_floating_bus(uint32_t executed_cycles) -> uint8_t {
    (void)executed_cycles;
    return bus_marker;
  }

  static auto bench_get_cycles() -> uint64_t { return 0; }

  static auto bench_get_clock_hz() -> double { return 1020484.0; }

  static auto bench_schedule_event(void* instance, uint64_t at_cycle) -> void {
    (void)instance;
    (void)at_cycle;
  }
};

BenchHost_t* BenchHost_t::s_active = nullptr;

}  // namespace

TEST_CASE(
    "Keyboard: the registry resolves the card by id and by name, and the "
    "descriptor is the motherboard's keyboard") {
  Peripheral_t* descriptor = keyboard_card();
  CHECK(peripheral_find_internal("Keyboard") == descriptor);

  CHECK(descriptor->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::strcmp(descriptor->id, "linapple.keyboard") == 0);
  CHECK(std::strcmp(descriptor->name, "Keyboard") == 0);
  CHECK(descriptor->compatible_slots == PERIPHERAL_MASK_INTERNAL);
  CHECK(descriptor->default_slot == 0);

  CHECK(descriptor->init != nullptr);
  CHECK(descriptor->reset != nullptr);
  CHECK(descriptor->shutdown != nullptr);
  CHECK(descriptor->think != nullptr);
  CHECK(descriptor->save_state != nullptr);
  CHECK(descriptor->load_state != nullptr);
  CHECK(descriptor->command != nullptr);
  CHECK(descriptor->query != nullptr);
}

TEST_CASE(
    "Keyboard: a null host gets no card and the entry points take a null "
    "instance") {
  Peripheral_t* descriptor = keyboard_card();
  CHECK(descriptor->init(0, nullptr) == nullptr);
  descriptor->reset(nullptr);
  descriptor->shutdown(nullptr);
  descriptor->think(nullptr, 1000);

  BenchHost_t bench;
  CHECK(bench.create() != nullptr);
}

TEST_CASE(
    "Keyboard: an id from another subsystem is incompatible for command and "
    "query, so a neighbour in the slot still gets asked") {
  BenchHost_t bench;
  void* card = bench.create();
  REQUIRE(card != nullptr);

  uint8_t byte = 1;
  CHECK(keyboard_card()->command(card, foreign_joystick_id, &byte,
                                 sizeof(byte)) == peripheral_incompatible);
  CHECK(keyboard_card()->command(card, foreign_disk_id, &byte, sizeof(byte)) ==
        peripheral_incompatible);

  size_t size = sizeof(byte);
  CHECK(keyboard_card()->query(card, foreign_joystick_id, &byte, &size) ==
        peripheral_incompatible);
  size = sizeof(byte);
  CHECK(keyboard_card()->query(card, foreign_disk_id, &byte, &size) ==
        peripheral_incompatible);
  size = sizeof(byte);
  CHECK(keyboard_card()->query(card, PERIPHERAL_QUERY_AUDIO_INFO, &byte,
                               &size) == peripheral_incompatible);
}

TEST_CASE(
    "Keyboard: the save-state sizing probe answers 552 bytes and the C99 view "
    "of the frame matches the C++ one") {
  BenchHost_t bench;
  void* card = bench.create();
  REQUIRE(card != nullptr);

  size_t size = 0;
  CHECK(keyboard_card()->save_state(card, nullptr, &size) == peripheral_ok);
  CHECK(size == frame_size);
  std::array<uint8_t, frame_size> scratch{};
  size = frame_size - 1;
  CHECK(keyboard_card()->save_state(card, scratch.data(), &size) ==
        peripheral_error);
  CHECK(keyboard_card()->save_state(card, scratch.data(), nullptr) ==
        peripheral_error);

  static_assert(sizeof(KeyboardSaveState_t) == frame_size,
                "the version-1 frame is 552 bytes");
  CHECK(keyboard_abi_c_frame_size() == frame_size);
  CHECK(keyboard_abi_c_state_version() == KEYBOARD_STATE_VERSION);
  CHECK(keyboard_abi_c_repeat_key_offset() == 12);
  CHECK(keyboard_abi_c_repeat_key_offset() ==
        offsetof(KeyboardSaveState_t, repeat_key));
  CHECK(keyboard_abi_c_latch_offset() == 24);
  CHECK(keyboard_abi_c_latch_offset() ==
        offsetof(KeyboardSaveState_t, current_latch));
  CHECK(keyboard_abi_c_strobe_offset() == 25);
  CHECK(keyboard_abi_c_strobe_offset() ==
        offsetof(KeyboardSaveState_t, strobe));
  CHECK(keyboard_abi_c_caps_lock_offset() == 31);
  CHECK(keyboard_abi_c_caps_lock_offset() ==
        offsetof(KeyboardSaveState_t, caps_lock));
  CHECK(keyboard_abi_c_auto_repeat_offset() == 35);
  CHECK(keyboard_abi_c_auto_repeat_offset() ==
        offsetof(KeyboardSaveState_t, auto_repeat_enabled));
}

TEST_CASE(
    "Keyboard: a key sent down latches its code under the strobe at $C000") {
  BenchHost_t bench;
  void* card = bench.create();
  REQUIRE(card != nullptr);
  CHECK((bench.read(addr_keyboard_data) & strobe_bit) == 0);

  KeyboardEvent_t event{};
  event.key = 'A';
  event.is_down = 1;
  REQUIRE(keyboard_card()->command(card, keyboard_cmd_event, &event,
                                   sizeof(event)) == peripheral_ok);
  CHECK(bench.read(addr_keyboard_data) == ('A' | strobe_bit));
}
