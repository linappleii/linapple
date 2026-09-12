// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

#include "Peripheral_Types.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/Peripheral.h"
#include "doctest.h"

namespace {

constexpr size_t MEMORY_SIZE_64K = 65536;

constexpr int TEST_SLOT_0 = 0;
constexpr int TEST_SLOT_1 = 4;

constexpr uint16_t addr_button0 = 0xC061;
constexpr uint16_t addr_paddle0 = 0xC064;
constexpr uint16_t addr_paddle1 = 0xC065;
constexpr uint16_t addr_paddle_reset = 0xC070;

constexpr int joystick_button_count = 3;
constexpr int PADDLES_PER_JOYSTICK = 2;
constexpr int joystick_count = 2;

constexpr uint8_t BUS_ACTIVE = 0x80;
constexpr uint8_t BUS_INACTIVE = 0x00;
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
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockHandler() = default;
  MockHandler(void* inst, PeripheralIOHandler r, PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

class JoystickHarness {
 public:
  JoystickHarness() {
    s_active_harness = this;
    scoped_mem_.fill(0);
    prev_mem_ = mem;
    mem = scoped_mem_.data();

    host_.Log = Mock_Log;
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.RegisterCxROM = Mock_RegisterCxROM;
    host_.RegisterExpansionROM = Mock_RegisterExpansionROM;
    host_.RegisterDirectIO = Mock_RegisterDirectIO;
    host_.GetCycles = Mock_GetCycles;
  }

  ~JoystickHarness() {
    for (void* instance : instances_) {
      if (instance != nullptr) {
        joystick_get_descriptor()->shutdown(instance);
      }
    }
    instances_.clear();
    mem = prev_mem_;
    s_active_harness = nullptr;
  }

  JoystickHarness(const JoystickHarness&) = delete;
  auto operator=(const JoystickHarness&) -> JoystickHarness& = delete;
  JoystickHarness(JoystickHarness&&) = delete;
  auto operator=(JoystickHarness&&) -> JoystickHarness& = delete;

  auto host() -> HostInterface_t* { return &host_; }

  auto create_joystick(int slot = 0) -> void* {
    void* instance = joystick_get_descriptor()->init(slot, &host_);
    if (instance != nullptr) {
      instances_.push_back(instance);
    }
    return instance;
  }

  auto destroy_joystick(void* instance) -> void {
    auto it = std::find(instances_.begin(), instances_.end(), instance);
    if (it != instances_.end()) {
      joystick_get_descriptor()->shutdown(instance);
      instances_.erase(it);
    }
  }

  auto set_cycles(uint64_t count) -> void { cycles_ = count; }

  auto advance_cycles(uint64_t delta) -> void { cycles_ += delta; }

  auto get_cycles() const -> uint64_t { return cycles_; }

  auto has_handler(uint16_t addr) const -> bool {
    return handlers_.find(addr) != handlers_.end();
  }

  auto get_handler(uint16_t addr) const -> const MockHandler& {
    return handlers_.at(addr);
  }

  auto read_io(void* instance, uint16_t addr) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      void* target = (instance != nullptr) ? instance : it->second.instance;
      return it->second.read(target, 0, addr, 0, 0, 0);
    }
    return 0;
  }

  auto write_io(void* instance, uint16_t addr, uint8_t val) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      void* target = (instance != nullptr) ? instance : it->second.instance;
      return it->second.write(target, 0, addr, 1, val, 0);
    }
    return 0;
  }

  auto strobe_reset_read(void* instance) -> uint8_t {
    return read_io(instance, addr_paddle_reset);
  }

  auto strobe_reset_write(void* instance, uint8_t val = 0) -> uint8_t {
    return write_io(instance, addr_paddle_reset, val);
  }

  auto set_axis(void* instance, uint8_t joystick, uint8_t axis, uint8_t value)
      -> PeripheralStatus_t {
    JoystickAxisPayload_t payload{joystick, axis, value};
    return joystick_get_descriptor()->command(instance, JOY_CMD_SET_AXIS,
                                              &payload, sizeof(payload));
  }

  auto set_button(void* instance, uint8_t button, bool down)
      -> PeripheralStatus_t {
    JoystickButtonPayload_t payload{button, down};
    return joystick_get_descriptor()->command(instance, JOY_CMD_SET_BUTTON,
                                              &payload, sizeof(payload));
  }

  auto set_trim(void* instance, bool axis_x, int16_t value)
      -> PeripheralStatus_t {
    JoystickTrimPayload_t payload{axis_x, value};
    return joystick_get_descriptor()->command(instance, JOY_CMD_SET_TRIM,
                                              &payload, sizeof(payload));
  }

  auto think(void* instance, uint32_t cycles) -> void {
    if (joystick_get_descriptor()->think != nullptr) {
      joystick_get_descriptor()->think(instance, cycles);
    }
  }

  auto set_config(void* instance, const JoystickConfig_t& config)
      -> PeripheralStatus_t {
    return joystick_get_descriptor()->command(instance, JOY_CMD_SET_CONFIG,
                                              &config, sizeof(config));
  }

  auto query_config(void* instance, JoystickConfig_t* out_config)
      -> PeripheralStatus_t {
    size_t size = sizeof(JoystickConfig_t);
    return joystick_get_descriptor()->query(instance, JOY_QUERY_CONFIG,
                                            out_config, &size);
  }

  auto read_button(void* instance, uint8_t button_index) -> uint8_t {
    return read_io(instance,
                   static_cast<uint16_t>(addr_button0 + button_index));
  }

  auto read_paddle(void* instance, uint8_t paddle_index) -> uint8_t {
    return read_io(instance,
                   static_cast<uint16_t>(addr_paddle0 + paddle_index));
  }

 private:
  HostInterface_t host_{};
  std::map<uint16_t, MockHandler> handlers_;
  std::vector<void*> instances_;
  uint64_t cycles_ = 0;
  std::array<uint8_t, MEMORY_SIZE_64K> scoped_mem_{};
  uint8_t* prev_mem_ = nullptr;

  static JoystickHarness* s_active_harness;

  static auto Mock_GetCycles() -> uint64_t {
    return (s_active_harness != nullptr) ? s_active_harness->cycles_ : 0;
  }

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
    (void)slot;
    (void)read_c0;
    (void)write_c0;
    (void)read_cx;
    (void)write_cx;
  }

  static auto Mock_RegisterCxROM(int slot, uint8_t* rom_ptr) -> void {
    (void)slot;
    (void)rom_ptr;
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
  // NOLINTEND(bugprone-easily-swappable-parameters)
};

JoystickHarness* JoystickHarness::s_active_harness = nullptr;

TEST_CASE("Joystick Peripheral: Lifecycle and Registration") {
  JoystickHarness harness;
  const int slot = TEST_SLOT_1;
  void* instance = harness.create_joystick(slot);
  REQUIRE(instance != nullptr);

  for (uint16_t addr = addr_button0;
       addr < addr_button0 + joystick_button_count; ++addr) {
    CHECK(harness.has_handler(addr));
    CHECK(harness.get_handler(addr).read != nullptr);
  }

  for (int addr = static_cast<int>(addr_paddle0);
       addr <
       static_cast<int>(addr_paddle0) + (joystick_count * PADDLES_PER_JOYSTICK);
       ++addr) {
    const auto uaddr = static_cast<uint16_t>(addr);
    CHECK(harness.has_handler(uaddr));
    CHECK(harness.get_handler(uaddr).read != nullptr);
  }

  CHECK(harness.has_handler(addr_paddle_reset));
  CHECK(harness.get_handler(addr_paddle_reset).read != nullptr);
  CHECK(harness.get_handler(addr_paddle_reset).write != nullptr);
}

TEST_CASE("Joystick Peripheral: Analog Timing Accuracy") {
  JoystickHarness harness;
  void* instance = harness.create_joystick(TEST_SLOT_0);
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);

  CHECK(harness.set_axis(instance, 0, 0, 0) == peripheral_ok);

  harness.strobe_reset_read(instance);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);
  harness.advance_cycles(SMALL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);

  CHECK(harness.set_axis(instance, 0, 0, MAX_AXIS_VALUE) == peripheral_ok);

  harness.strobe_reset_read(instance);
  harness.advance_cycles(LARGE_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);
  harness.advance_cycles(FINAL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: Button Latching and Thinking") {
  JoystickHarness harness;
  void* instance = harness.create_joystick(TEST_SLOT_0);
  REQUIRE(instance != nullptr);

  CHECK(harness.set_button(instance, 0, true) == peripheral_ok);
  CHECK(harness.set_button(instance, 0, false) == peripheral_ok);

  CHECK(harness.read_button(instance, 0) == BUS_ACTIVE);

  harness.think(instance, static_cast<uint32_t>(THINK_LATCH_CYCLES));
  CHECK(harness.read_button(instance, 0) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: Trim Logic") {
  JoystickHarness harness;
  void* instance = harness.create_joystick(TEST_SLOT_0);
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);

  CHECK(harness.set_axis(instance, 0, 0, TRIM_TEST_POS) == peripheral_ok);
  CHECK(harness.set_trim(instance, true, TRIM_TEST_OFFSET) == peripheral_ok);

  harness.strobe_reset_read(instance);
  harness.advance_cycles(TRIM_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);
  harness.advance_cycles(TRIM_FINAL_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: Config and Query") {
  JoystickHarness harness;
  void* instance = harness.create_joystick(TEST_SLOT_0);
  REQUIRE(instance != nullptr);

  JoystickConfig_t cfg{};
  cfg.joy_type[0] = 1;
  cfg.joy_index[0] = TEST_JOY_INDEX;
  cfg.joy0_button_map[0] = TEST_BUTTON_MAPPING;

  CHECK(harness.set_config(instance, cfg) == peripheral_ok);

  JoystickConfig_t queried{};
  CHECK(harness.query_config(instance, &queried) == peripheral_ok);

  CHECK(queried.joy_type[0] == 1);
  CHECK(queried.joy_index[0] == TEST_JOY_INDEX);
  CHECK(queried.joy0_button_map[0] == TEST_BUTTON_MAPPING);
}

TEST_CASE("Joystick Peripheral: Robustness and Multiple Instances") {
  JoystickHarness harness;
  void* instance1 = harness.create_joystick(TEST_SLOT_1);
  REQUIRE(instance1 != nullptr);

  CHECK(harness.set_axis(instance1, 0, 0, MAX_AXIS_VALUE) == peripheral_ok);

  harness.strobe_reset_read(instance1);
  harness.advance_cycles(INITIAL_CYCLE_COUNT);

  CHECK(harness.read_paddle(instance1, 0) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: Full Paddle 0 and 1 Dynamic Range") {
  JoystickHarness harness;
  void* instance = harness.create_joystick(TEST_SLOT_0);
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);

  CHECK(harness.set_axis(instance, 0, 0, 0) == peripheral_ok);
  CHECK(harness.set_axis(instance, 0, 1, 0) == peripheral_ok);

  harness.strobe_reset_read(instance);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);
  CHECK(harness.read_paddle(instance, 1) == BUS_ACTIVE);

  harness.advance_cycles(SMALL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);
  CHECK(harness.read_paddle(instance, 1) == BUS_INACTIVE);

  CHECK(harness.set_axis(instance, 0, 0, MAX_AXIS_VALUE) == peripheral_ok);
  CHECK(harness.set_axis(instance, 0, 1, MAX_AXIS_VALUE) == peripheral_ok);

  harness.strobe_reset_read(instance);
  harness.advance_cycles(LARGE_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);
  CHECK(harness.read_paddle(instance, 1) == BUS_ACTIVE);

  harness.advance_cycles(FINAL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);
  CHECK(harness.read_paddle(instance, 1) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: Rapid Successive Strobe Resets") {
  JoystickHarness harness;
  void* instance = harness.create_joystick(TEST_SLOT_0);
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);

  CHECK(harness.set_axis(instance, 0, 0, MAX_AXIS_VALUE) == peripheral_ok);

  // 1. Initial strobe reset via memory-mapped read
  harness.strobe_reset_read(instance);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);

  // Advance partway into the discharge curve (1500 cycles < 2815)
  harness.advance_cycles(1500);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);

  // 2. Rapid second strobe reset via memory-mapped write
  harness.strobe_reset_write(instance, 0x55);

  // Advance another 1500 cycles.
  // Cumulative elapsed since first reset is 3000 (which would be discharged),
  // but elapsed since second reset is only 1500. Paddle MUST still be active.
  harness.advance_cycles(1500);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);

  // Advance to cycle 2814 from second reset (still active)
  harness.advance_cycles(1314);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);

  // Advance across 2815 boundary (elapsed 2816 >= 2815 -> discharged)
  harness.advance_cycles(2);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);

  // 3. Immediate re-trigger while discharged
  harness.strobe_reset_read(instance);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);

  // 4. Consecutive-cycle strobe resets
  harness.advance_cycles(1);
  harness.strobe_reset_read(instance);
  harness.advance_cycles(1);
  harness.strobe_reset_write(instance, 0x00);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);

  harness.advance_cycles(2814);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);
  harness.advance_cycles(2);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);
}

TEST_CASE(
    "Joystick Peripheral: Extreme and Negative Axis and Trim Boundaries") {
  JoystickHarness harness;
  void* instance = harness.create_joystick(TEST_SLOT_0);
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);

  // 1. Negative trim clamping: 50 - 200 = -150 -> clamped to 0. Charge limit =
  // 10 cycles.
  CHECK(harness.set_axis(instance, 0, 0, 50) == peripheral_ok);
  CHECK(harness.set_trim(instance, true, -200) == peripheral_ok);

  harness.strobe_reset_read(instance);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);
  harness.advance_cycles(SMALL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);

  // 2. Extreme negative trim: INT16_MIN clamping to 0.
  CHECK(harness.set_axis(instance, 0, 0, MAX_AXIS_VALUE) == peripheral_ok);
  CHECK(harness.set_trim(instance, true, std::numeric_limits<int16_t>::min()) ==
        peripheral_ok);

  harness.strobe_reset_read(instance);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);
  harness.advance_cycles(SMALL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);

  // 3. Extreme positive trim clamping: 100 + 500 = 600 -> clamped to 255. Limit
  // = 2815 cycles.
  CHECK(harness.set_axis(instance, 0, 0, 100) == peripheral_ok);
  CHECK(harness.set_trim(instance, true, 500) == peripheral_ok);

  harness.strobe_reset_read(instance);
  harness.advance_cycles(LARGE_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);
  harness.advance_cycles(FINAL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);

  // 4. Maximum positive trim: INT16_MAX clamping to 255.
  CHECK(harness.set_axis(instance, 0, 0, 0) == peripheral_ok);
  CHECK(harness.set_trim(instance, true, std::numeric_limits<int16_t>::max()) ==
        peripheral_ok);

  harness.strobe_reset_read(instance);
  harness.advance_cycles(LARGE_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);
  harness.advance_cycles(FINAL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);

  // 5. Y-axis independent trim clamping (paddle 1)
  CHECK(harness.set_trim(instance, true, 0) == peripheral_ok);
  CHECK(harness.set_axis(instance, 0, 1, 50) == peripheral_ok);
  CHECK(harness.set_trim(instance, false, -1000) == peripheral_ok);

  harness.strobe_reset_read(instance);
  CHECK(harness.read_paddle(instance, 1) == BUS_ACTIVE);
  harness.advance_cycles(SMALL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance, 1) == BUS_INACTIVE);

  // 6. Command validation and boundary checks
  CHECK(joystick_get_descriptor()->command(instance, JOY_CMD_SET_AXIS, nullptr,
                                           0) == peripheral_error);
  CHECK(joystick_get_descriptor()->command(instance, JOY_CMD_SET_BUTTON,
                                           nullptr, 0) == peripheral_error);
  CHECK(joystick_get_descriptor()->command(instance, JOY_CMD_SET_TRIM, nullptr,
                                           0) == peripheral_error);
  CHECK(joystick_get_descriptor()->command(instance, JOY_CMD_SET_CONFIG,
                                           nullptr, 0) == peripheral_error);
  CHECK(joystick_get_descriptor()->command(nullptr, JOY_CMD_RESET, nullptr,
                                           0) == peripheral_error);
}

TEST_CASE("Joystick Peripheral: Multiple Instance Isolation") {
  JoystickHarness harness;
  void* instance_a = harness.create_joystick(TEST_SLOT_0);
  void* instance_b = harness.create_joystick(TEST_SLOT_1);
  REQUIRE(instance_a != nullptr);
  REQUIRE(instance_b != nullptr);

  harness.set_cycles(INITIAL_CYCLE_COUNT);

  // Instance A: axis 0 = 0 (charge limit = 10)
  // Instance B: axis 0 = 255 (charge limit = 2815)
  CHECK(harness.set_axis(instance_a, 0, 0, 0) == peripheral_ok);
  CHECK(harness.set_axis(instance_b, 0, 0, MAX_AXIS_VALUE) == peripheral_ok);

  harness.strobe_reset_read(instance_a);
  CHECK(harness.read_paddle(instance_a, 0) == BUS_ACTIVE);

  harness.advance_cycles(SMALL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance_a, 0) == BUS_INACTIVE);

  // Strobe reset instance B independently
  harness.strobe_reset_read(instance_b);
  CHECK(harness.read_paddle(instance_b, 0) == BUS_ACTIVE);
  CHECK(harness.read_paddle(instance_a, 0) == BUS_INACTIVE);

  harness.advance_cycles(LARGE_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance_b, 0) == BUS_ACTIVE);
  CHECK(harness.read_paddle(instance_a, 0) == BUS_INACTIVE);

  harness.advance_cycles(FINAL_WAIT_CYCLES);
  CHECK(harness.read_paddle(instance_b, 0) == BUS_INACTIVE);
  CHECK(harness.read_paddle(instance_a, 0) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: State Serialization and Deserialization") {
  JoystickHarness harness;
  void* instance = harness.create_joystick(TEST_SLOT_0);
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);

  CHECK(harness.set_axis(instance, 0, 0, MAX_AXIS_VALUE) == peripheral_ok);
  harness.strobe_reset_read(instance);

  // Query state buffer size
  size_t state_size = 0;
  CHECK(joystick_get_descriptor()->save_state(instance, nullptr, &state_size) ==
        peripheral_ok);
  CHECK(state_size == sizeof(SS_IO_Joystick));

  // Buffer undersized error check
  std::vector<uint8_t> small_buffer(sizeof(SS_IO_Joystick) - 1);
  size_t small_size = small_buffer.size();
  CHECK(joystick_get_descriptor()->save_state(instance, small_buffer.data(),
                                              &small_size) == peripheral_error);

  // Save state
  std::vector<uint8_t> state_buffer(state_size);
  CHECK(joystick_get_descriptor()->save_state(instance, state_buffer.data(),
                                              &state_size) == peripheral_ok);
  CHECK(state_size == sizeof(SS_IO_Joystick));

  // Advance cycles past the discharge window (3000 > 2815)
  harness.advance_cycles(3000);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);

  // Restore state
  CHECK(joystick_get_descriptor()->load_state(instance, state_buffer.data(),
                                              state_buffer.size()) ==
        peripheral_ok);

  // Set cycle counter to 500 cycles past the saved reset timestamp: should be
  // active
  harness.set_cycles(INITIAL_CYCLE_COUNT + 500);
  CHECK(harness.read_paddle(instance, 0) == BUS_ACTIVE);

  // Advance past discharge threshold from restored reset timestamp
  harness.advance_cycles(2500);
  CHECK(harness.read_paddle(instance, 0) == BUS_INACTIVE);

  // Error handling for load_state
  CHECK(joystick_get_descriptor()->load_state(nullptr, state_buffer.data(),
                                              state_buffer.size()) ==
        peripheral_error);
  CHECK(joystick_get_descriptor()->load_state(
            instance, nullptr, state_buffer.size()) == peripheral_error);
  CHECK(joystick_get_descriptor()->load_state(instance, state_buffer.data(),
                                              sizeof(SS_IO_Joystick) - 1) ==
        peripheral_error);
}

}  // namespace
