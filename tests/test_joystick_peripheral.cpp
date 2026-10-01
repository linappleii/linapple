// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "doctest.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

extern "C" auto joystick_abi_c_descriptor() -> Peripheral_t*;
extern "C" auto joystick_abi_c_state_size() -> size_t;
extern "C" auto joystick_abi_c_trigger_cycle_offset() -> size_t;
extern "C" auto joystick_abi_c_trigger_cycle_size() -> size_t;
extern "C" auto joystick_abi_c_x_pos_offset() -> size_t;
extern "C" auto joystick_abi_c_y_pos_offset() -> size_t;
extern "C" auto joystick_abi_c_buttons_offset() -> size_t;
extern "C" auto joystick_abi_c_trim_x_offset() -> size_t;
extern "C" auto joystick_abi_c_trim_y_offset() -> size_t;
extern "C" auto joystick_abi_c_axis_payload_size() -> size_t;
extern "C" auto joystick_abi_c_button_payload_size() -> size_t;
extern "C" auto joystick_abi_c_state_version() -> uint32_t;

namespace {

constexpr uint16_t addr_button0 = 0xC061;
constexpr uint16_t addr_paddle0 = 0xC064;
constexpr uint16_t addr_paddle_reset = 0xC070;

// A standard controller pulls PB0 and PB1 down through the 560 ohm resistors
// in its plug, so those two lines rest low; PB2 has no pull-down (Sather,
// Understanding the Apple II, 7-9 and 7-11).
constexpr uint8_t pulled_down_button_count = 2;

constexpr uint8_t BUS_ACTIVE = 0x80;
constexpr uint8_t BUS_INACTIVE = 0x00;
constexpr uint8_t MAX_AXIS_VALUE = 255;

constexpr uint64_t INITIAL_CYCLE_COUNT = 1000000;
constexpr uint64_t SMALL_WAIT_CYCLES = 11;
constexpr uint64_t LARGE_WAIT_CYCLES = 2800;
constexpr uint64_t FINAL_WAIT_CYCLES = 20;

struct MockHandler {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;
  PeripheralStrobeHandler_t on_strobe = nullptr;

  MockHandler() = default;
  MockHandler(void* inst, PeripheralIOHandler r, PeripheralIOHandler w,
              PeripheralStrobeHandler_t strobe)
      : instance(inst), read(r), write(w), on_strobe(strobe) {}
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
    host_.RegisterDirectIOStrobe = Mock_RegisterDirectIOStrobe;
    host_.GetCycles = Mock_GetCycles;
    host_.ReadFloatingBus = Mock_ReadFloatingBus;
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

  auto has_handler(uint16_t addr) const -> bool {
    return handlers_.find(addr) != handlers_.end();
  }

  auto create_joystick(int slot = 0) -> void* {
    void* instance = joystick_get_descriptor()->init(slot, &host_);
    if (instance != nullptr) {
      instances_.push_back(instance);
    }
    return instance;
  }

  auto set_cycles(uint64_t count) -> void { cycles_ = count; }

  auto advance_cycles(uint64_t delta) -> void { cycles_ += delta; }

  // A strobe registration answers any access by firing and leaving the bus
  // alone, as the bridge does.
  auto read_io(void* instance, uint16_t addr, uint32_t remaining_cycles = 0)
      -> uint8_t {
    auto it = handlers_.find(addr);
    if (it == handlers_.end()) {
      return 0;
    }
    void* target = (instance != nullptr) ? instance : it->second.instance;
    if (it->second.on_strobe != nullptr) {
      it->second.on_strobe(target);
      return 0;
    }
    if (it->second.read != nullptr) {
      return it->second.read(target, 0, addr, 0, 0, remaining_cycles);
    }
    return 0;
  }

  auto write_io(void* instance, uint16_t addr, uint8_t val = 0,
                uint32_t remaining_cycles = 0) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it == handlers_.end()) {
      return 0;
    }
    void* target = (instance != nullptr) ? instance : it->second.instance;
    if (it->second.on_strobe != nullptr) {
      it->second.on_strobe(target);
      return 0;
    }
    if (it->second.write != nullptr) {
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
    return joystick_get_descriptor()->command(instance, JOYSTICK_CMD_SET_AXIS,
                                              &payload, sizeof(payload));
  }

  auto set_button(void* instance, uint8_t button, bool down)
      -> PeripheralStatus_t {
    JoystickButtonPayload_t payload{button, static_cast<uint8_t>(down ? 1 : 0),
                                    0, 0};
    return joystick_get_descriptor()->command(instance, JOYSTICK_CMD_SET_BUTTON,
                                              &payload, sizeof(payload));
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

  static auto Mock_ReadFloatingBus(uint32_t executed_cycles) -> uint8_t {
    return mem_read_floating_bus(executed_cycles);
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

  static auto Mock_RegisterCxROM(int slot, const uint8_t* rom_ptr) -> void {
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
      s_active_harness->handlers_[addr] = {instance, read, write, nullptr};
    }
  }

  static auto Mock_RegisterDirectIOStrobe(void* instance, uint16_t addr,
                                          PeripheralStrobeHandler_t on_strobe)
      -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->handlers_[addr] = {instance, nullptr, nullptr,
                                           on_strobe};
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
  CHECK(descriptor->shutdown != nullptr);
  CHECK(descriptor->on_vblank == nullptr);
  CHECK(descriptor->save_state != nullptr);
  CHECK(descriptor->load_state != nullptr);
  CHECK(descriptor->command != nullptr);
  CHECK(descriptor->query != nullptr);
}

TEST_CASE("Joystick Peripheral: Button Inputs and Floating Bus") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  for (uint8_t i = 0; i < pulled_down_button_count; ++i) {
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

TEST_CASE("Joystick Peripheral: Command ABI Protocol") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  auto* descriptor = joystick_get_descriptor();
  REQUIRE(descriptor->command != nullptr);

  // Set axis
  JoystickAxisPayload_t axis_payload{0, 0, 200, 0};
  CHECK(descriptor->command(instance, JOYSTICK_CMD_SET_AXIS, &axis_payload,
                            sizeof(axis_payload)) == peripheral_ok);

  // Invalid joystick index
  JoystickAxisPayload_t bad_joy{5, 0, 200, 0};
  CHECK(descriptor->command(instance, JOYSTICK_CMD_SET_AXIS, &bad_joy,
                            sizeof(bad_joy)) == peripheral_error);

  // Invalid axis index
  JoystickAxisPayload_t bad_axis{0, 2, 200, 0};
  CHECK(descriptor->command(instance, JOYSTICK_CMD_SET_AXIS, &bad_axis,
                            sizeof(bad_axis)) == peripheral_error);

  // Invalid button index, level and source
  JoystickButtonPayload_t bad_btn{7, 1, 0, 0};
  CHECK(descriptor->command(instance, JOYSTICK_CMD_SET_BUTTON, &bad_btn,
                            sizeof(bad_btn)) == peripheral_error);
  JoystickButtonPayload_t bad_level{0, 2, 0, 0};
  CHECK(descriptor->command(instance, JOYSTICK_CMD_SET_BUTTON, &bad_level,
                            sizeof(bad_level)) == peripheral_error);
  JoystickButtonPayload_t bad_source{0, 1, 2, 0};
  CHECK(descriptor->command(instance, JOYSTICK_CMD_SET_BUTTON, &bad_source,
                            sizeof(bad_source)) == peripheral_error);

  // Null data pointer
  CHECK(descriptor->command(instance, JOYSTICK_CMD_SET_AXIS, nullptr,
                            sizeof(axis_payload)) == peripheral_error);

  // Unhandled command ID
  CHECK(descriptor->command(instance, 0x9999, nullptr, 0) ==
        peripheral_incompatible);
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
  const JoystickAxisPayload_t axis{0, 0, 0, 0};
  CHECK(joystick_get_descriptor()->command(nullptr, JOYSTICK_CMD_SET_AXIS,
                                           &axis,
                                           sizeof(axis)) == peripheral_error);

  if (joystick_get_descriptor()->reset != nullptr) {
    joystick_get_descriptor()->reset(nullptr);
  }
  if (joystick_get_descriptor()->think != nullptr) {
    joystick_get_descriptor()->think(nullptr, 100);
  }
  joystick_get_descriptor()->shutdown(nullptr);
}

TEST_CASE(
    "Joystick Peripheral: The C99 view of the frame matches the C++ one") {
  CHECK(joystick_abi_c_descriptor() == joystick_get_descriptor());
  static_assert(sizeof(JoystickSaveState_t) == 56,
                "the version-1 frame is 56 bytes");
  CHECK(joystick_abi_c_state_size() == 56);
  CHECK(joystick_abi_c_state_size() == sizeof(JoystickSaveState_t));
  CHECK(joystick_abi_c_state_version() == JOYSTICK_STATE_VERSION);

  CHECK(joystick_abi_c_trigger_cycle_offset() == 8);
  CHECK(joystick_abi_c_trigger_cycle_offset() ==
        offsetof(JoystickSaveState_t, trigger_cycle));
  CHECK(joystick_abi_c_trigger_cycle_size() == 32);
  CHECK(joystick_abi_c_trigger_cycle_size() ==
        sizeof(JoystickSaveState_t::trigger_cycle));
  CHECK(joystick_abi_c_x_pos_offset() == 40);
  CHECK(joystick_abi_c_x_pos_offset() == offsetof(JoystickSaveState_t, x_pos));
  CHECK(joystick_abi_c_y_pos_offset() == 42);
  CHECK(joystick_abi_c_y_pos_offset() == offsetof(JoystickSaveState_t, y_pos));
  CHECK(joystick_abi_c_buttons_offset() == 44);
  CHECK(joystick_abi_c_buttons_offset() ==
        offsetof(JoystickSaveState_t, buttons));
  CHECK(joystick_abi_c_trim_x_offset() == 48);
  CHECK(joystick_abi_c_trim_x_offset() ==
        offsetof(JoystickSaveState_t, trim_x));
  CHECK(joystick_abi_c_trim_y_offset() == 50);
  CHECK(joystick_abi_c_trim_y_offset() ==
        offsetof(JoystickSaveState_t, trim_y));

  CHECK(joystick_abi_c_axis_payload_size() == 4);
  CHECK(joystick_abi_c_axis_payload_size() == sizeof(JoystickAxisPayload_t));
  CHECK(joystick_abi_c_button_payload_size() == 4);
  CHECK(joystick_abi_c_button_payload_size() ==
        sizeof(JoystickButtonPayload_t));
}

TEST_CASE(
    "Joystick Peripheral: A strobe during a pulse leaves the fall time alone") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);

  // Paddle 0 at 100 is a 11 * 100 + 10 = 1,110-cycle pulse; paddle 1 at 0 is
  // a 10-cycle pulse, so the second strobe finds paddle 1 expired and paddle
  // 0 still high.
  constexpr uint8_t long_position = 100;
  constexpr uint64_t long_pulse = 1110;
  constexpr uint64_t second_strobe_after = 500;
  REQUIRE(harness.set_axis(instance, 0, 0, long_position) == peripheral_ok);
  REQUIRE(harness.set_axis(instance, 0, 1, 0) == peripheral_ok);
  harness.strobe_reset_read(instance);

  harness.advance_cycles(second_strobe_after);
  harness.strobe_reset_read(instance);
  CHECK((harness.read_paddle(instance, 1) & 0x80) == BUS_ACTIVE);

  harness.advance_cycles(long_pulse - second_strobe_after - 1);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);
  CHECK((harness.read_paddle(instance, 1) & 0x80) == BUS_INACTIVE);

  // Had the second strobe restarted paddle 0 it would stay high until
  // 500 + 1,110 cycles after the first.
  harness.advance_cycles(1);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_INACTIVE);
}

TEST_CASE(
    "Joystick Peripheral: Any access to $C070-$C07E triggers an idle timer") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);

  // Position 0 is a ten-cycle pulse, so eleven cycles later the timer is idle
  // again for the next address.
  constexpr uint16_t first_trigger = 0xC070;
  constexpr uint16_t last_trigger = 0xC07E;
  constexpr uint16_t rddhires = 0xC07F;
  REQUIRE(harness.set_axis(instance, 0, 0, 0) == peripheral_ok);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_INACTIVE);

  for (uint16_t addr = first_trigger; addr <= last_trigger; ++addr) {
    CAPTURE(addr);
    harness.read_io(instance, addr);
    CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);
    harness.advance_cycles(SMALL_WAIT_CYCLES);
    CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_INACTIVE);

    harness.write_io(instance, addr);
    CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_ACTIVE);
    harness.advance_cycles(SMALL_WAIT_CYCLES);
    CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_INACTIVE);
  }
  CHECK_FALSE(harness.has_handler(rddhires));
}

TEST_CASE("Joystick Peripheral: $C068-$C06F read as $C060-$C067") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);

  constexpr uint16_t cassette_in = 0xC060;
  constexpr uint16_t cassette_in_mirror = 0xC068;
  constexpr uint16_t switch0_mirror = 0xC069;
  constexpr uint16_t paddle2 = 0xC066;
  constexpr uint16_t paddle2_mirror = 0xC06E;
  // Paddle 2 is joystick 1's x axis; at 20 its pulse is 11 * 20 + 10 = 230.
  constexpr uint8_t paddle2_position = 20;
  constexpr uint64_t paddle2_pulse = 230;

  CHECK_FALSE(harness.has_handler(cassette_in));
  CHECK_FALSE(harness.has_handler(cassette_in_mirror));

  REQUIRE(harness.set_button(instance, 0, true) == peripheral_ok);
  CHECK((harness.read_io(instance, switch0_mirror) & 0x80) == BUS_ACTIVE);
  CHECK((harness.read_io(instance, addr_button0) & 0x80) == BUS_ACTIVE);

  REQUIRE(harness.set_axis(instance, 1, 0, paddle2_position) == peripheral_ok);
  harness.strobe_reset_read(instance);
  CHECK((harness.read_io(instance, paddle2_mirror) & 0x80) == BUS_ACTIVE);
  CHECK((harness.read_io(instance, paddle2) & 0x80) == BUS_ACTIVE);
  harness.advance_cycles(paddle2_pulse);
  CHECK((harness.read_io(instance, paddle2_mirror) & 0x80) == BUS_INACTIVE);
  CHECK((harness.read_io(instance, paddle2) & 0x80) == BUS_INACTIVE);
}

TEST_CASE(
    "Joystick Peripheral: A cold start reads every paddle expired and the card "
    "has no reset work") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);

  for (uint8_t paddle = 0; paddle < 4; ++paddle) {
    CAPTURE(paddle);
    CHECK((harness.read_paddle(instance, paddle) & 0x80) == BUS_INACTIVE);
  }

  // The centre position 127 is a 11 * 127 + 10 = 1,407-cycle pulse.
  constexpr uint64_t centre_pulse = 1407;
  harness.strobe_reset_read(instance);
  CHECK(joystick_get_descriptor()->reset == nullptr);
  harness.advance_cycles(centre_pulse - 1);
  for (uint8_t paddle = 0; paddle < 4; ++paddle) {
    CAPTURE(paddle);
    CHECK((harness.read_paddle(instance, paddle) & 0x80) == BUS_ACTIVE);
  }
  harness.advance_cycles(1);
  for (uint8_t paddle = 0; paddle < 4; ++paddle) {
    CAPTURE(paddle);
    CHECK((harness.read_paddle(instance, paddle) & 0x80) == BUS_INACTIVE);
  }
}

// The card as the registry hands it out, which is what the machine wires in.
auto game_port() -> Peripheral_t* {
  Peripheral_t* descriptor = peripheral_find_internal("linapple.joystick");
  REQUIRE(descriptor != nullptr);
  return descriptor;
}

using Frame_t = std::array<uint8_t, sizeof(JoystickSaveState_t)>;

auto save_frame(void* instance) -> Frame_t {
  Frame_t frame{};
  size_t size = frame.size();
  REQUIRE(game_port()->save_state(instance, frame.data(), &size) ==
          peripheral_ok);
  REQUIRE(size == frame.size());
  return frame;
}

auto set_pulldowns(void* instance, uint8_t mask) -> PeripheralStatus_t {
  return game_port()->command(instance, JOYSTICK_CMD_SET_PULLDOWNS, &mask,
                              sizeof(mask));
}

auto set_shift_key_mod(void* instance, uint8_t jumper) -> PeripheralStatus_t {
  return game_port()->command(instance, JOYSTICK_CMD_SET_SHIFT_KEY_MOD, &jumper,
                              sizeof(jumper));
}

// Header: version 1, struct_size 56 ($38). A cold start has every timer
// expired, so one strobe at cycle 1,000,000 ($0F4240) triggers all four. The
// positions, switch levels and trim are not the port's and go out as zeros.
// Bytes as a little-endian host lays the frame out.
constexpr Frame_t frame_after_one_strobe = {{
    0x01, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00,  //
    0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
}};

// Positions 0, 10, 100 and 255 are pulses of 10, 120, 1,110 and 2,815 cycles.
// Strobes at 1,000,000, 1,001,200, 1,001,400 and 1,001,450: the second finds
// channels 0-2 expired (1,200 is past 10, 120 and 1,110) and channel 3 not;
// the third finds 0 and 1 expired (200 is past 10 and 120, short of 1,110);
// the fourth only channel 0 (50 is past 10, short of 120). So the triggers are
// 1,001,450 ($0F47EA), 1,001,400 ($0F47B8), 1,001,200 ($0F46F0) and 1,000,000
// ($0F4240).
constexpr Frame_t frame_after_four_strobes = {{
    0x01, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00,  //
    0xEA, 0x47, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0xB8, 0x47, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0xF0, 0x46, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
}};

TEST_CASE(
    "Joystick Peripheral: The frame after one strobe from a cold start is the "
    "literal, and after four strobes the triggers tell the channels apart") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  harness.set_cycles(INITIAL_CYCLE_COUNT);
  REQUIRE(harness.set_button(instance, 0, true) == peripheral_ok);
  REQUIRE(harness.set_axis(instance, 0, 0, MAX_AXIS_VALUE) == peripheral_ok);
  harness.strobe_reset_read(instance);
  CHECK(save_frame(instance) == frame_after_one_strobe);

  JoystickHarness second;
  void* port = second.create_joystick();
  REQUIRE(port != nullptr);
  REQUIRE(second.set_axis(port, 0, 0, 0) == peripheral_ok);
  REQUIRE(second.set_axis(port, 0, 1, 10) == peripheral_ok);
  REQUIRE(second.set_axis(port, 1, 0, 100) == peripheral_ok);
  REQUIRE(second.set_axis(port, 1, 1, MAX_AXIS_VALUE) == peripheral_ok);
  second.set_cycles(INITIAL_CYCLE_COUNT);
  second.strobe_reset_read(port);
  second.set_cycles(INITIAL_CYCLE_COUNT + 1200);
  second.strobe_reset_read(port);
  second.set_cycles(INITIAL_CYCLE_COUNT + 1400);
  second.strobe_reset_read(port);
  second.set_cycles(INITIAL_CYCLE_COUNT + 1450);
  second.strobe_reset_read(port);
  CHECK(save_frame(port) == frame_after_four_strobes);
}

TEST_CASE(
    "Joystick Peripheral: A rejected load changes nothing and an accepted one "
    "leaves the jumper and the pull-downs alone") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);
  harness.set_cycles(INITIAL_CYCLE_COUNT);
  harness.strobe_reset_read(instance);
  REQUIRE(save_frame(instance) == frame_after_one_strobe);

  // Each rejected frame carries a different trigger so that a load that
  // slipped through would show in the re-saved frame.
  Frame_t short_frame = frame_after_four_strobes;
  CHECK(game_port()->load_state(instance, short_frame.data(),
                                short_frame.size() - 1) == peripheral_error);
  Frame_t bad_version = frame_after_four_strobes;
  bad_version.at(0) = 0x02;
  CHECK(game_port()->load_state(instance, bad_version.data(),
                                bad_version.size()) == peripheral_error);
  Frame_t bad_struct_size = frame_after_four_strobes;
  bad_struct_size.at(4) = 0x30;
  CHECK(game_port()->load_state(instance, bad_struct_size.data(),
                                bad_struct_size.size()) == peripheral_error);
  CHECK(game_port()->load_state(instance, nullptr, short_frame.size()) ==
        peripheral_error);
  CHECK(save_frame(instance) == frame_after_one_strobe);

  // With every line pulled down and the jumper in, PB2 reads 0 at rest and
  // follows shift; a loaded frame carries neither and leaves both as set.
  constexpr uint8_t every_line_pulled_down = 0x07;
  constexpr uint8_t pb2 = 2;
  REQUIRE(set_pulldowns(instance, every_line_pulled_down) == peripheral_ok);
  REQUIRE(set_shift_key_mod(instance, 1) == peripheral_ok);
  CHECK((harness.read_button(instance, pb2) & 0x80) == BUS_ACTIVE);
  CHECK((harness.read_button(instance, 1) & 0x80) == BUS_INACTIVE);
  CHECK(game_port()->load_state(instance, frame_after_four_strobes.data(),
                                frame_after_four_strobes.size()) ==
        peripheral_ok);
  CHECK(save_frame(instance) == frame_after_four_strobes);
  CHECK((harness.read_button(instance, pb2) & 0x80) == BUS_ACTIVE);
  CHECK((harness.read_button(instance, 1) & 0x80) == BUS_INACTIVE);
  const JoystickButtonPayload_t shift_down{pb2, 1, 1, 0};
  REQUIRE(game_port()->command(instance, JOYSTICK_CMD_SET_BUTTON, &shift_down,
                               sizeof(shift_down)) == peripheral_ok);
  CHECK((harness.read_button(instance, pb2) & 0x80) == BUS_INACTIVE);

  // Out-of-range wiring is refused: the mask has three bits, the jumper one.
  CHECK(set_pulldowns(instance, every_line_pulled_down + 1) ==
        peripheral_error);
  CHECK(set_shift_key_mod(instance, 2) == peripheral_error);
}

TEST_CASE(
    "Joystick Peripheral: A loaded trigger ahead of the counter reads expired "
    "and any trigger value loads") {
  JoystickHarness harness;
  void* instance = harness.create_joystick();
  REQUIRE(instance != nullptr);

  // The frame's triggers sit at 1,000,000 and the counter at 1,000: on a real
  // machine three milliseconds after anything every timer has fallen.
  constexpr uint64_t counter_behind_the_triggers = 1000;
  harness.set_cycles(counter_behind_the_triggers);
  REQUIRE(game_port()->load_state(instance, frame_after_one_strobe.data(),
                                  frame_after_one_strobe.size()) ==
          peripheral_ok);
  for (uint8_t paddle = 0; paddle < 4; ++paddle) {
    CAPTURE(paddle);
    CHECK((harness.read_paddle(instance, paddle) & 0x80) == BUS_INACTIVE);
  }
  CHECK(save_frame(instance) == frame_after_one_strobe);

  // An expired channel accepts the next strobe, so the triggers move to the
  // counter.
  harness.strobe_reset_read(instance);
  Frame_t frame = save_frame(instance);
  for (size_t paddle = 0; paddle < 4; ++paddle) {
    uint64_t trigger = 0;
    std::memcpy(&trigger, &frame.at(8 + (paddle * 8)), sizeof(trigger));
    CHECK(trigger == counter_behind_the_triggers);
  }

  Frame_t all_ones = frame_after_one_strobe;
  std::fill(all_ones.begin() + 8, all_ones.begin() + 40, 0xFF);
  CHECK(game_port()->load_state(instance, all_ones.data(), all_ones.size()) ==
        peripheral_ok);
  CHECK(save_frame(instance) == all_ones);
  CHECK((harness.read_paddle(instance, 0) & 0x80) == BUS_INACTIVE);
}

}  // namespace
