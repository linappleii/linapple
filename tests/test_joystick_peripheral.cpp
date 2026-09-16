// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "Peripheral_Types.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/Peripheral.h"
#include "doctest.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

constexpr uint16_t addr_button0 = 0xC061;
constexpr uint16_t addr_button2 = 0xC063;
constexpr uint16_t addr_paddle0 = 0xC064;
constexpr uint16_t addr_paddle1 = 0xC065;
constexpr uint16_t addr_paddle3 = 0xC067;
constexpr uint16_t addr_paddle_reset = 0xC070;

constexpr int joystick_button_count = 3;
constexpr int joystick_count = 2;

constexpr uint8_t BUS_ACTIVE = 0x80;
constexpr uint8_t BUS_INACTIVE = 0x00;
constexpr uint8_t MAX_AXIS_VALUE = 255;
constexpr uint8_t DEFAULT_AXIS_VALUE = 127;

constexpr uint64_t INITIAL_CYCLE_COUNT = 1000000;
constexpr uint64_t BUTTON_LATCH_CYCLES = 10205;
constexpr uint64_t SMALL_WAIT_CYCLES = 11;
constexpr uint64_t LARGE_WAIT_CYCLES = 2800;
constexpr uint64_t FINAL_WAIT_CYCLES = 20;

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

  auto read_io(void* instance, uint16_t addr, uint32_t remaining_cycles = 0)
      -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      void* target = (instance != nullptr) ? instance : it->second.instance;
      return it->second.read(target, 0, addr, 0, 0, remaining_cycles);
    }
    return 0;
  }

  auto write_io(void* instance, uint16_t addr, uint8_t val = 0,
                uint32_t remaining_cycles = 0) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      void* target = (instance != nullptr) ? instance : it->second.instance;
      return it->second.write(target, 0, addr, 1, val, remaining_cycles);
    }
    return 0;
  }

  auto strobe_reset_read(void* instance, uint32_t remaining_cycles = 0)
      -> uint8_t {
    return read_io(instance, addr_paddle_reset, remaining_cycles);
  }

  auto strobe_reset_write(void* instance, uint8_t val = 0,
                          uint32_t remaining_cycles = 0) -> uint8_t {
    return write_io(instance, addr_paddle_reset, val, remaining_cycles);
  }

  auto set_axis(void* instance, uint8_t joystick, uint8_t axis, uint8_t value)
      -> PeripheralStatus_t {
    JoystickAxisPayload_t payload{joystick, axis, value, 0};
    return joystick_get_descriptor()->command(instance, JOY_CMD_SET_AXIS,
                                              &payload, sizeof(payload));
  }

  auto set_button(void* instance, uint8_t button, bool down)
      -> PeripheralStatus_t {
    JoystickButtonPayload_t payload{button, down, {0, 0}};
    return joystick_get_descriptor()->command(instance, JOY_CMD_SET_BUTTON,
                                              &payload, sizeof(payload));
  }

  auto set_trim(void* instance, bool axis_x, int16_t value)
      -> PeripheralStatus_t {
    JoystickTrimPayload_t payload{axis_x, 0, value};
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

  auto read_button(void* instance, uint8_t button_index,
                   uint32_t remaining_cycles = 0) -> uint8_t {
    return read_io(instance, static_cast<uint16_t>(addr_button0 + button_index),
                   remaining_cycles);
  }

  auto read_paddle(void* instance, uint8_t paddle_index,
                   uint32_t remaining_cycles = 0) -> uint8_t {
    return read_io(instance, static_cast<uint16_t>(addr_paddle0 + paddle_index),
                   remaining_cycles);
  }

 private:
  HostInterface_t host_{};
  std::map<uint16_t, MockHandler> handlers_;
  std::vector<void*> instances_;
  uint64_t cycles_ = 0;

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
  // NOLINTEND(bugprone-easily-swappable-parameters)

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
};

JoystickHarness* JoystickHarness::s_active_harness = nullptr;

TEST_CASE("Joystick Peripheral: Registration and Identity Metadata") {
  auto* descriptor = joystick_get_descriptor();
  REQUIRE(descriptor != nullptr);

  CHECK(descriptor->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::strcmp(descriptor->id, "linapple.joystick") == 0);
  CHECK(std::strcmp(descriptor->name, "Joystick") == 0);
  CHECK(descriptor->compatible_slots == PERIPHERAL_MASK_INTERNAL);
  CHECK(descriptor->default_slot == 0);

  CHECK(descriptor->init != nullptr);
  CHECK(descriptor->reset != nullptr);
  CHECK(descriptor->shutdown != nullptr);
  CHECK(descriptor->think != nullptr);
  CHECK(descriptor->on_vblank == nullptr);
  CHECK(descriptor->save_state != nullptr);
  CHECK(descriptor->load_state != nullptr);
  CHECK(descriptor->command != nullptr);
  CHECK(descriptor->query != nullptr);
}

TEST_CASE("Joystick Peripheral: Direct IO Registration") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  for (uint16_t addr = addr_button0; addr <= addr_button2; ++addr) {
    CHECK(harness.has_handler(addr));
    CHECK(harness.get_handler(addr).read != nullptr);
  }

  for (uint16_t addr = addr_paddle0; addr <= addr_paddle3; ++addr) {
    CHECK(harness.has_handler(addr));
    CHECK(harness.get_handler(addr).read != nullptr);
  }

  CHECK(harness.has_handler(addr_paddle_reset));
  CHECK(harness.get_handler(addr_paddle_reset).read != nullptr);
  CHECK(harness.get_handler(addr_paddle_reset).write != nullptr);
}

TEST_CASE("Joystick Peripheral: Button Inputs and Floating Bus") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  for (uint8_t i = 0; i < joystick_button_count; ++i) {
    constexpr uint32_t test_cycles = 10;
    const uint8_t unpressed = harness.read_button(instance, i, test_cycles);
    const uint8_t expected_floating = mem_read_floating_bus(test_cycles) & 0x7F;
    CHECK((unpressed & 0x80) == 0);
    CHECK((unpressed & 0x7F) == expected_floating);

    REQUIRE(harness.set_button(instance, i, true) == peripheral_ok);
    const uint8_t pressed = harness.read_button(instance, i, test_cycles);
    CHECK((pressed & 0x80) == 0x80);
    CHECK((pressed & 0x7F) == expected_floating);

    REQUIRE(harness.set_button(instance, i, false) == peripheral_ok);
  }
}

TEST_CASE("Joystick Peripheral: Button Latch Timing and Think") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  REQUIRE(harness.set_button(instance, 0, true) == peripheral_ok);
  REQUIRE(harness.set_button(instance, 0, false) == peripheral_ok);

  // Still latched high immediately after release
  CHECK((harness.read_button(instance, 0) & 0x80) == BUS_ACTIVE);

  // Think for part of debounce duration
  harness.think(instance, 5000);
  CHECK((harness.read_button(instance, 0) & 0x80) == BUS_ACTIVE);

  // Think past debounce duration
  harness.think(instance, 6000);
  CHECK((harness.read_button(instance, 0) & 0x80) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: Paddle Analog Timing") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  harness.set_cycles(INITIAL_CYCLE_COUNT);

  // Position 0: charge time is 10 cycles
  REQUIRE(harness.set_axis(instance, 0, 0, 0) == peripheral_ok);
  harness.strobe_reset_read(instance);

  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);
  harness.advance_cycles(SMALL_WAIT_CYCLES);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_INACTIVE);

  // Position 255: charge time is 255 * 11 + 10 = 2815 cycles
  REQUIRE(harness.set_axis(instance, 0, 0, MAX_AXIS_VALUE) == peripheral_ok);
  harness.strobe_reset_write(instance);

  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);
  harness.advance_cycles(LARGE_WAIT_CYCLES);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);

  harness.advance_cycles(FINAL_WAIT_CYCLES);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: Paddle Calibration Trim") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  harness.set_cycles(INITIAL_CYCLE_COUNT);

  // Raw position 100 with trim +50 = 150. Charge: 150 * 11 + 10 = 1660 cycles
  REQUIRE(harness.set_axis(instance, 0, 0, TRIM_TEST_POS) == peripheral_ok);
  REQUIRE(harness.set_trim(instance, true, TRIM_TEST_OFFSET) == peripheral_ok);
  harness.strobe_reset_read(instance);

  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);
  harness.advance_cycles(TRIM_WAIT_CYCLES);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);

  harness.advance_cycles(TRIM_FINAL_CYCLES);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: Paddle Strobe Reset") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  harness.set_cycles(INITIAL_CYCLE_COUNT);
  REQUIRE(harness.set_axis(instance, 0, 0, 100) == peripheral_ok);

  harness.strobe_reset_read(instance);
  harness.advance_cycles(1200);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_INACTIVE);

  // Re-strobe via write resets charge cycle
  harness.strobe_reset_write(instance, 0x42);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);
}

TEST_CASE("Joystick Peripheral: Command ABI Protocol") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  auto* descriptor = joystick_get_descriptor();
  REQUIRE(descriptor->command != nullptr);

  // Set axis
  JoystickAxisPayload_t axis_payload{0, 0, 200, 0};
  CHECK(descriptor->command(instance, JOY_CMD_SET_AXIS, &axis_payload,
                            sizeof(axis_payload)) == peripheral_ok);

  // Invalid joystick index
  JoystickAxisPayload_t bad_joy{5, 0, 200, 0};
  CHECK(descriptor->command(instance, JOY_CMD_SET_AXIS, &bad_joy,
                            sizeof(bad_joy)) == peripheral_error);

  // Invalid axis index
  JoystickAxisPayload_t bad_axis{0, 2, 200, 0};
  CHECK(descriptor->command(instance, JOY_CMD_SET_AXIS, &bad_axis,
                            sizeof(bad_axis)) == peripheral_error);

  // Invalid button index
  JoystickButtonPayload_t bad_btn{7, true, {0, 0}};
  CHECK(descriptor->command(instance, JOY_CMD_SET_BUTTON, &bad_btn,
                            sizeof(bad_btn)) == peripheral_error);

  // Null data pointer
  CHECK(descriptor->command(instance, JOY_CMD_SET_AXIS, nullptr,
                            sizeof(axis_payload)) == peripheral_error);

  // Reset command
  CHECK(descriptor->command(instance, JOY_CMD_RESET, nullptr, 0) ==
        peripheral_ok);

  // Unhandled command ID
  CHECK(descriptor->command(instance, 0x9999, nullptr, 0) ==
        peripheral_incompatible);
}

TEST_CASE("Joystick Peripheral: Query ABI Protocol") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  auto* descriptor = joystick_get_descriptor();
  REQUIRE(descriptor->query != nullptr);

  // Sizing probe for Config query
  size_t config_size = 0;
  CHECK(descriptor->query(instance, JOY_QUERY_CONFIG, nullptr, &config_size) ==
        peripheral_ok);
  CHECK(config_size == sizeof(JoystickConfig_t));

  // Sizing probe for Exit Event query
  size_t exit_size = 0;
  CHECK(descriptor->query(instance, JOY_QUERY_EXIT_EVENT, nullptr,
                          &exit_size) == peripheral_ok);
  CHECK(exit_size == sizeof(uint8_t));

  // Successful exit event query
  uint8_t exit_event = 0;
  CHECK(descriptor->query(instance, JOY_QUERY_EXIT_EVENT, &exit_event,
                          &exit_size) == peripheral_ok);
  CHECK(exit_event == 0);

  // Error paths: null size pointer
  CHECK(descriptor->query(instance, JOY_QUERY_EXIT_EVENT, &exit_event,
                          nullptr) == peripheral_error);

  // Error paths: undersized buffer
  size_t too_small = 0;
  CHECK(descriptor->query(instance, JOY_QUERY_CONFIG, &exit_event,
                          &too_small) == peripheral_error);

  // Error paths: unhandled query ID
  CHECK(descriptor->query(instance, 0x8888, &exit_event, &exit_size) ==
        peripheral_incompatible);
}

TEST_CASE("Joystick Peripheral: Reset Behavior") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  REQUIRE(harness.set_axis(instance, 0, 0, 50) == peripheral_ok);
  REQUIRE(harness.set_button(instance, 0, true) == peripheral_ok);

  joystick_get_descriptor()->reset(instance);

  // Buttons must be released
  CHECK((harness.read_button(instance, 0) & 0x80) == BUS_INACTIVE);

  // Reset restores positions to 127
  harness.set_cycles(100);
  harness.strobe_reset_read(instance);
  // Charge for 127: 127 * 11 + 10 = 1407 cycles
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);
  harness.advance_cycles(1400);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);
  harness.advance_cycles(10);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: Deterministic Save State Persistence") {
  JoystickHarness harness;
  void* instance1 = harness.create_joystick();
  REQUIRE(instance1 != nullptr);

  harness.set_cycles(INITIAL_CYCLE_COUNT);
  REQUIRE(harness.set_axis(instance1, 0, 0, 80) == peripheral_ok);
  REQUIRE(harness.set_axis(instance1, 0, 1, 90) == peripheral_ok);
  REQUIRE(harness.set_button(instance1, 1, true) == peripheral_ok);
  REQUIRE(harness.set_trim(instance1, true, 15) == peripheral_ok);
  harness.strobe_reset_read(instance1);

  // Sizing probe
  size_t state_size = 0;
  CHECK(joystick_get_descriptor()->save_state(instance1, nullptr,
                                              &state_size) == peripheral_ok);
  REQUIRE(state_size == sizeof(JoystickSaveState_t));
  CHECK(state_size == 56);

  // Undersized buffer guard
  size_t too_small = sizeof(JoystickSaveState_t) - 1;
  std::vector<uint8_t> small_buffer(too_small);
  CHECK(joystick_get_descriptor()->save_state(instance1, small_buffer.data(),
                                              &too_small) == peripheral_error);

  // Successful save
  std::vector<uint8_t> buffer(state_size);
  CHECK(joystick_get_descriptor()->save_state(instance1, buffer.data(),
                                              &state_size) == peripheral_ok);

  const auto* state_view =
      reinterpret_cast<const JoystickSaveState_t*>(buffer.data());
  CHECK(state_view->version == JOYSTICK_STATE_VERSION);
  CHECK(state_view->struct_size == sizeof(JoystickSaveState_t));
  CHECK(state_view->reset_cycle == INITIAL_CYCLE_COUNT);
  CHECK(state_view->x_pos[0] == 80);
  CHECK(state_view->y_pos[0] == 90);
  CHECK(state_view->buttons[1] == 1);
  CHECK(state_view->trim_x == 15);

  // Load onto a fresh instance
  void* instance2 = harness.create_joystick();
  REQUIRE(instance2 != nullptr);

  CHECK(joystick_get_descriptor()->load_state(instance2, buffer.data(),
                                              state_size) == peripheral_ok);

  // Verify button 1 is pressed on restored instance
  CHECK((harness.read_button(instance2, 1) & 0x80) == BUS_ACTIVE);
  CHECK((harness.read_button(instance2, 0) & 0x80) == BUS_INACTIVE);

  // Verify paddle 0 charge curve matches restored position 80 + trim 15 = 95
  // Charge for 95: 95 * 11 + 10 = 1055 cycles
  harness.set_cycles(INITIAL_CYCLE_COUNT + 1050);
  CHECK((harness.read_paddle(instance2, 0) & 0x80) == BUS_ACTIVE);
  harness.set_cycles(INITIAL_CYCLE_COUNT + 1060);
  CHECK((harness.read_paddle(instance2, 0) & 0x80) == BUS_INACTIVE);
}

TEST_CASE("Joystick Peripheral: Defensive Deserialization Validation") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  size_t state_size = 0;
  joystick_get_descriptor()->save_state(instance, nullptr, &state_size);
  std::vector<uint8_t> valid_state(state_size);
  joystick_get_descriptor()->save_state(instance, valid_state.data(),
                                        &state_size);

  // Mismatched buffer size
  CHECK(joystick_get_descriptor()->load_state(
            instance, valid_state.data(), state_size - 1) == peripheral_error);

  // Corrupt version
  auto bad_version = valid_state;
  reinterpret_cast<JoystickSaveState_t*>(bad_version.data())->version = 99;
  CHECK(joystick_get_descriptor()->load_state(instance, bad_version.data(),
                                              state_size) == peripheral_error);

  // Corrupt struct_size
  auto bad_struct_size = valid_state;
  reinterpret_cast<JoystickSaveState_t*>(bad_struct_size.data())->struct_size =
      1234;
  CHECK(joystick_get_descriptor()->load_state(instance, bad_struct_size.data(),
                                              state_size) == peripheral_error);

  // Corrupt boolean button value (> 1)
  auto bad_btn = valid_state;
  reinterpret_cast<JoystickSaveState_t*>(bad_btn.data())->buttons[0] = 5;
  CHECK(joystick_get_descriptor()->load_state(instance, bad_btn.data(),
                                              state_size) == peripheral_error);
}

TEST_CASE("Joystick Peripheral: Host Boundary and Lifecycle Robustness") {
  JoystickHarness harness;

  // Null host check on init
  CHECK(joystick_get_descriptor()->init(0, nullptr) == nullptr);

  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  // Null instance safety across callbacks
  size_t dummy_size = sizeof(JoystickSaveState_t);
  std::vector<uint8_t> dummy_buf(dummy_size);
  CHECK(joystick_get_descriptor()->save_state(nullptr, dummy_buf.data(),
                                              &dummy_size) == peripheral_error);
  CHECK(joystick_get_descriptor()->load_state(nullptr, dummy_buf.data(),
                                              dummy_size) == peripheral_error);
  CHECK(joystick_get_descriptor()->command(nullptr, JOY_CMD_RESET, nullptr,
                                           0) == peripheral_error);
  CHECK(joystick_get_descriptor()->query(nullptr, JOY_QUERY_CONFIG,
                                         dummy_buf.data(),
                                         &dummy_size) == peripheral_error);

  joystick_get_descriptor()->reset(nullptr);
  joystick_get_descriptor()->think(nullptr, 100);
  joystick_get_descriptor()->shutdown(nullptr);
}

}  // namespace
