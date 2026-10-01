// Integration tests: slot 0 command and query dispatch across keyboard and
// joystick.

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <ostream>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"

namespace {

constexpr uint16_t addr_switch0 = 0xC061;
constexpr uint8_t switch_bit = 0x80;
constexpr uint8_t source_connector = 0;
constexpr uint8_t source_keyboard = 1;
constexpr uint8_t all_lines_pulled_down = 0x07;
constexpr uint16_t addr_paddle0 = 0xC064;
constexpr uint16_t addr_trigger = 0xC070;
constexpr uint8_t joy_centre = 127;
constexpr uint8_t joy_off_centre = 200;
// A paddle at 200 holds its timer high for 11 * 200 + 10 = 2,210 cycles; at the
// centre it would have fallen 803 cycles earlier.
constexpr uint64_t off_centre_pulse = 2210;
// Far past any pulse, so the first strobe finds every timer expired.
constexpr uint64_t probe_counter = 1000000;
constexpr uint32_t unknown_joystick_id = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x00FF;

enum class Order_t : uint8_t { keyboard_first, joystick_first };

// So that CAPTURE(order) says which pass a failure came from.
auto operator<<(std::ostream& out, Order_t order) -> std::ostream& {
  return out << (order == Order_t::keyboard_first ? "keyboard first"
                                                  : "joystick first");
}

auto keyboard_descriptor() -> Peripheral_t* {
  Peripheral_t* descriptor = peripheral_find_internal("linapple.keyboard");
  REQUIRE(descriptor != nullptr);
  return descriptor;
}

auto joystick_descriptor() -> Peripheral_t* {
  Peripheral_t* descriptor = peripheral_find_internal("linapple.joystick");
  REQUIRE(descriptor != nullptr);
  return descriptor;
}

// Slot 0 fixture managing registration and teardown of keyboard and joystick.
struct Slot0_t {
  eApple2Type saved_type{current_apple2_type};
  // A fresh CPU context so the I/O bridge's cycle accounting starts at zero
  // and the counter the probes set is the one the card reads.
  CpuInstance_t* saved_cpu{cpu_get_active_context()};
  CpuInstance_t cpu{};
  int keyboard_registered{-1};
  int joystick_registered{-1};

  explicit Slot0_t(Order_t order) {
    cpu_set_active_context(&cpu);
    current_apple2_type = A2TYPE_APPLE2EENHANCED;
    REQUIRE(mem_initialize() == 0);
    peripheral_manager_init();
    if (order == Order_t::keyboard_first) {
      keyboard_registered = peripheral_register(keyboard_descriptor(), 0);
      joystick_registered = peripheral_register(joystick_descriptor(), 0);
    } else {
      joystick_registered = peripheral_register(joystick_descriptor(), 0);
      keyboard_registered = peripheral_register(keyboard_descriptor(), 0);
    }
    peripheral_manager_reset();
  }

  ~Slot0_t() {
    peripheral_manager_shutdown();
    mem_destroy();
    current_apple2_type = saved_type;
    cpu_set_active_context(saved_cpu);
  }

  Slot0_t(const Slot0_t&) = delete;
  auto operator=(const Slot0_t&) -> Slot0_t& = delete;
  Slot0_t(Slot0_t&&) = delete;
  auto operator=(Slot0_t&&) -> Slot0_t& = delete;
};

// Dedicated test fixture for inspecting synchronous dispatcher status.
void bare_log(void*, PeripheralLogLevel_t, const char*, ...) {}
void bare_register_direct_io(void*, uint16_t, PeripheralIOHandler,
                             PeripheralIOHandler) {}
void bare_register_direct_io_strobe(void*, uint16_t,
                                    PeripheralStrobeHandler_t) {}
auto bare_get_cycles() -> uint64_t { return 0; }
auto bare_read_floating_bus(uint32_t) -> uint8_t { return 0; }

struct BareHost_t {
  HostInterface_t host{};
  Peripheral_t* keyboard{keyboard_descriptor()};
  Peripheral_t* joystick{joystick_descriptor()};
  void* kbd{nullptr};
  void* joy{nullptr};

  BareHost_t() {
    host.Log = bare_log;
    host.RegisterDirectIO = bare_register_direct_io;
    host.RegisterDirectIOStrobe = bare_register_direct_io_strobe;
    host.GetCycles = bare_get_cycles;
    host.ReadFloatingBus = bare_read_floating_bus;
    kbd = keyboard->init(0, &host);
    joy = joystick->init(0, &host);
  }

  ~BareHost_t() {
    if (kbd != nullptr) keyboard->shutdown(kbd);
    if (joy != nullptr) joystick->shutdown(joy);
  }

  BareHost_t(const BareHost_t&) = delete;
  auto operator=(const BareHost_t&) -> BareHost_t& = delete;
  BareHost_t(BareHost_t&&) = delete;
  auto operator=(BareHost_t&&) -> BareHost_t& = delete;
};

// Commands are queued and dispatched on emulation frame boundaries.
auto settle() -> void { peripheral_manager_think(0); }

auto send(uint32_t cmd_id, const void* data, size_t size) -> void {
  REQUIRE(peripheral_command(0, cmd_id, data, size) == peripheral_ok);
  settle();
}

auto keyboard_state() -> KeyboardSaveState_t {
  KeyboardSaveState_t state{};
  size_t size = sizeof(state);
  peripheral_save_state_by_name(0, "Keyboard", &state, &size);
  REQUIRE(size == sizeof(state));
  return state;
}

auto joystick_state() -> JoystickSaveState_t {
  JoystickSaveState_t state{};
  size_t size = sizeof(state);
  peripheral_save_state_by_name(0, "Joystick", &state, &size);
  REQUIRE(size == sizeof(state));
  return state;
}

auto keyboard_mods() -> KeyboardModifiers_t {
  KeyboardModifiers_t mods{};
  size_t size = sizeof(mods);
  REQUIRE(peripheral_query_by_id(0, "linapple.keyboard", keyboard_query_mods,
                                 &mods, &size) == peripheral_ok);
  return mods;
}

// Bit 7 of a switch line through the memory map, as the 6502 would read it.
auto switch_level(uint8_t line) -> uint8_t {
  const uint16_t addr = static_cast<uint16_t>(addr_switch0 + line);
  return (io_map_dispatch(0, addr, 0, 0, 0) & switch_bit) != 0 ? 1 : 0;
}

auto set_switch(uint8_t line, uint8_t source, uint8_t down) -> void {
  const JoystickButtonPayload_t payload{line, down, source, 0};
  send(JOYSTICK_CMD_SET_BUTTON, &payload, sizeof(payload));
}

auto press_button(uint8_t button) -> void {
  set_switch(button, source_connector, 1);
}

auto set_pulldowns(uint8_t mask) -> void {
  send(JOYSTICK_CMD_SET_PULLDOWNS, &mask, sizeof(mask));
}

// A position is visible only as the length of the pulse a strobe starts, so
// the probe strobes at probe_counter and samples bit 7 one cycle before and at
// the fall the position predicts.
auto paddle_level_after(uint8_t paddle, uint64_t cycles) -> uint8_t {
  g_cumulative_cycles = probe_counter + cycles;
  const uint16_t addr = static_cast<uint16_t>(addr_paddle0 + paddle);
  return (io_map_dispatch(0, addr, 0, 0, 0) & switch_bit) != 0 ? 1 : 0;
}

auto paddle_pulse_is(uint8_t paddle, uint64_t pulse) -> bool {
  g_cumulative_cycles = probe_counter;
  static_cast<void>(io_map_dispatch(0, addr_trigger, 0, 0, 0));
  return paddle_level_after(paddle, pulse - 1) == 1 &&
         paddle_level_after(paddle, pulse) == 0;
}

auto move_axis(uint8_t joystick, uint8_t axis, uint8_t value) -> void {
  const JoystickAxisPayload_t payload{joystick, axis, value, 0};
  send(JOYSTICK_CMD_SET_AXIS, &payload, sizeof(payload));
}

const std::initializer_list<Order_t> both_orders = {Order_t::keyboard_first,
                                                    Order_t::joystick_first};

// Allocate oversized buffer to test payload bounds handling safely.
template <typename T>
struct OneByteLong_t {
  T value;
  uint8_t extra;
};

}  // namespace

TEST_CASE("Slot 0: the rocker switch and a button under one slot") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const uint8_t on = 1;
    send(keyboard_cmd_set_rocker, &on, sizeof(on));
    press_button(0);

    uint8_t rocker = 0;
    size_t size = sizeof(rocker);
    CHECK(peripheral_query(0, keyboard_query_rocker, &rocker, &size) ==
          peripheral_ok);
    CHECK(rocker == 1);
    CHECK(switch_level(0) == 1);
  }
}

TEST_CASE("Slot 0: a stick move and the modifiers under one id") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    move_axis(0, 0, joy_off_centre);
    press_button(0);

    CHECK(paddle_pulse_is(0, off_centre_pulse));
    CHECK(switch_level(0) == 1);
    CHECK(keyboard_mods().shift == 0);
    CHECK(keyboard_mods().ctrl == 0);
  }
}

TEST_CASE("Slot 0: flipping the rocker leaves the sticks where they are") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    move_axis(0, 0, joy_off_centre);
    press_button(0);
    REQUIRE(paddle_pulse_is(0, off_centre_pulse));
    REQUIRE(switch_level(0) == 1);

    const uint8_t on = 1;
    send(keyboard_cmd_set_rocker, &on, sizeof(on));

    CHECK(paddle_pulse_is(0, off_centre_pulse));
    CHECK(switch_level(0) == 1);
    CHECK(keyboard_state().rocker_switch == 1);
  }
}

TEST_CASE("Slot 0: a button payload does not hold down shift") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const KeyboardModifiers_t before = keyboard_mods();
    // Bytes 1, 1 would land on KeyboardModifiers_t::shift and ::ctrl.
    const JoystickButtonPayload_t payload{1, 1, 0, 0};
    send(JOYSTICK_CMD_SET_BUTTON, &payload, sizeof(payload));

    const KeyboardModifiers_t after = keyboard_mods();
    CHECK(after.shift == 0);
    CHECK(after.ctrl == 0);
    CHECK(after.caps == before.caps);
    CHECK(switch_level(1) == 1);
  }
}

TEST_CASE("Slot 0: setting modifiers does not move the stick") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    move_axis(0, 0, joy_off_centre);
    press_button(0);
    const JoystickSaveState_t before = joystick_state();

    KeyboardModifiers_t mods{};
    mods.shift = 1;
    mods.ctrl = 1;
    send(keyboard_cmd_set_mods, &mods, sizeof(mods));

    CHECK(keyboard_mods().shift == 1);
    CHECK(keyboard_mods().ctrl == 1);
    const JoystickSaveState_t after = joystick_state();
    CHECK(switch_level(0) == 1);
    CHECK(std::memcmp(&before, &after, sizeof(JoystickSaveState_t)) == 0);
    CHECK(paddle_pulse_is(0, off_centre_pulse));
  }
}

TEST_CASE("Slot 0: a button press does not toggle caps lock") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const uint8_t caps_off = 0;
    send(keyboard_cmd_set_caps, &caps_off, sizeof(caps_off));
    REQUIRE(keyboard_state().caps_lock == 0);

    // With nothing pulling PB2 down the line rests high, so a pull-down is
    // installed first to make the press visible.
    set_pulldowns(all_lines_pulled_down);
    REQUIRE(switch_level(2) == 0);
    press_button(2);

    CHECK(keyboard_state().caps_lock == 0);
    CHECK(switch_level(2) == 1);
  }
}

TEST_CASE("Slot 0: a line reads the OR of its button and its Apple key") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    // PB0 and PB1 rest low through the controller's pull-downs; PB2 has none
    // and its open TTL input rests high (Sather, Understanding the Apple II,
    // 7-9 and 7-11).
    CHECK(switch_level(0) == 0);
    CHECK(switch_level(1) == 0);
    CHECK(switch_level(2) == 1);

    set_switch(0, source_keyboard, 1);
    CHECK(switch_level(0) == 1);
    CHECK(switch_level(1) == 0);
    set_switch(0, source_connector, 1);
    CHECK(switch_level(0) == 1);
    set_switch(0, source_keyboard, 0);
    CHECK(switch_level(0) == 1);
    set_switch(0, source_connector, 0);
    CHECK(switch_level(0) == 0);

    // The keyboard card's own modifier levels answer the debugger's query and
    // reach no switch line.
    KeyboardModifiers_t mods{};
    mods.gui = 1;
    send(keyboard_cmd_set_mods, &mods, sizeof(mods));
    CHECK(keyboard_mods().gui == 1);
    CHECK(switch_level(0) == 0);
    CHECK(switch_level(1) == 0);
  }
}

TEST_CASE("Slot 0: a payload of the wrong size changes nothing") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const uint8_t on = 1;
    send(keyboard_cmd_set_rocker, &on, sizeof(on));
    move_axis(0, 1, joy_off_centre);

    const KeyboardSaveState_t keyboard_before = keyboard_state();
    const JoystickSaveState_t joystick_before = joystick_state();

    // Verify dispatch rejects invalid payload lengths.
    const OneByteLong_t<JoystickAxisPayload_t> axis{{0, 1, joy_centre, 0}, 0};
    REQUIRE(peripheral_command(0, JOYSTICK_CMD_SET_AXIS, &axis,
                               sizeof(JoystickAxisPayload_t) - 1) ==
            peripheral_ok);
    REQUIRE(peripheral_command(0, JOYSTICK_CMD_SET_AXIS, &axis,
                               sizeof(JoystickAxisPayload_t) + 1) ==
            peripheral_ok);
    const OneByteLong_t<JoystickButtonPayload_t> button{{0, 1, 0, 0}, 0};
    REQUIRE(peripheral_command(0, JOYSTICK_CMD_SET_BUTTON, &button,
                               sizeof(JoystickButtonPayload_t) - 1) ==
            peripheral_ok);
    REQUIRE(peripheral_command(0, JOYSTICK_CMD_SET_BUTTON, &button,
                               sizeof(JoystickButtonPayload_t) + 1) ==
            peripheral_ok);
    KeyboardModifiers_t mods{};
    mods.shift = 1;
    REQUIRE(peripheral_command(0, keyboard_cmd_set_mods, &mods,
                               sizeof(mods) - 1) == peripheral_ok);
    settle();

    const KeyboardSaveState_t keyboard_after = keyboard_state();
    const JoystickSaveState_t joystick_after = joystick_state();
    CHECK(switch_level(0) == 0);
    CHECK(std::memcmp(&keyboard_before, &keyboard_after,
                      sizeof(KeyboardSaveState_t)) == 0);
    CHECK(std::memcmp(&joystick_before, &joystick_after,
                      sizeof(JoystickSaveState_t)) == 0);
    CHECK(paddle_pulse_is(1, off_centre_pulse));
  }
}

TEST_CASE("Slot 0: a dispatcher says peripheral_error to the wrong size") {
  BareHost_t bare;
  Peripheral_t* keyboard = bare.keyboard;
  Peripheral_t* joystick = bare.joystick;
  void* kbd = bare.kbd;
  void* joy = bare.joy;
  REQUIRE(kbd != nullptr);
  REQUIRE(joy != nullptr);

  const uint8_t byte = 1;
  const OneByteLong_t<uint8_t> two_bytes{1, 0};
  CHECK(keyboard->command(kbd, keyboard_cmd_set_rocker, &byte, sizeof(byte)) ==
        peripheral_ok);
  CHECK(keyboard->command(kbd, keyboard_cmd_set_rocker, &byte, 0) ==
        peripheral_error);
  CHECK(keyboard->command(kbd, keyboard_cmd_set_rocker, &two_bytes, 2) ==
        peripheral_error);
  CHECK(keyboard->command(kbd, keyboard_cmd_set_rocker, nullptr, 1) ==
        peripheral_error);
  CHECK(keyboard->command(kbd, keyboard_cmd_clear_custom_keys, &byte,
                          sizeof(byte)) == peripheral_error);
  CHECK(keyboard->command(kbd, keyboard_cmd_clear_custom_keys, nullptr, 0) ==
        peripheral_ok);

  const OneByteLong_t<JoystickAxisPayload_t> axis{{0, 0, joy_off_centre, 0}, 0};
  constexpr size_t axis_size = sizeof(JoystickAxisPayload_t);
  CHECK(joystick->command(joy, JOYSTICK_CMD_SET_AXIS, &axis, axis_size) ==
        peripheral_ok);
  CHECK(joystick->command(joy, JOYSTICK_CMD_SET_AXIS, &axis, axis_size - 1) ==
        peripheral_error);
  CHECK(joystick->command(joy, JOYSTICK_CMD_SET_AXIS, &axis, axis_size + 1) ==
        peripheral_error);
  CHECK(joystick->command(joy, JOYSTICK_CMD_SET_AXIS, nullptr, axis_size) ==
        peripheral_error);

  const OneByteLong_t<JoystickButtonPayload_t> button{{0, 1, 0, 0}, 0};
  constexpr size_t button_size = sizeof(JoystickButtonPayload_t);
  CHECK(joystick->command(joy, JOYSTICK_CMD_SET_BUTTON, &button, button_size) ==
        peripheral_ok);
  CHECK(joystick->command(joy, JOYSTICK_CMD_SET_BUTTON, &button,
                          button_size - 1) == peripheral_error);
  CHECK(joystick->command(joy, JOYSTICK_CMD_SET_BUTTON, &button,
                          button_size + 1) == peripheral_error);
  CHECK(joystick->command(joy, JOYSTICK_CMD_SET_BUTTON, nullptr, button_size) ==
        peripheral_error);
}

TEST_CASE("Slot 0: an id from another subsystem is refused and changes none") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const uint8_t on = 1;
    send(keyboard_cmd_set_rocker, &on, sizeof(on));
    move_axis(0, 0, joy_off_centre);
    press_button(0);

    const KeyboardSaveState_t keyboard_before = keyboard_state();
    const JoystickSaveState_t joystick_before = joystick_state();

    // Verify commands belonging to foreign subsystems are rejected.
    const std::initializer_list<uint32_t> foreign = {
        PERIPHERAL_SUBSYSTEM_DISK | 0x0003,
        PERIPHERAL_SUBSYSTEM_HARDDISK | 0x0004,
        PERIPHERAL_SUBSYSTEM_MOUSE | 0x0001,
        PERIPHERAL_SUBSYSTEM_SERIAL | 0x0002,
    };
    std::array<uint8_t, 64> payload{};
    payload.fill(1);
    for (uint32_t cmd_id : foreign) {
      REQUIRE(peripheral_command(0, cmd_id, payload.data(), payload.size()) ==
              peripheral_ok);
      settle();

      // Unhandled query returns error when all slot peripherals stand aside.
      uint8_t answer = 0;
      size_t size = sizeof(answer);
      CHECK(peripheral_query(0, cmd_id, &answer, &size) == peripheral_error);
    }

    const KeyboardSaveState_t keyboard_after = keyboard_state();
    const JoystickSaveState_t joystick_after = joystick_state();
    CHECK(std::memcmp(&keyboard_before, &keyboard_after,
                      sizeof(KeyboardSaveState_t)) == 0);
    CHECK(std::memcmp(&joystick_before, &joystick_after,
                      sizeof(JoystickSaveState_t)) == 0);
  }
}

TEST_CASE("Slot 0: a foreign id is incompatible, never an error") {
  // Peripheral query search halts on first status that is not incompatible.
  BareHost_t bare;
  Peripheral_t* keyboard = bare.keyboard;
  Peripheral_t* joystick = bare.joystick;
  void* kbd = bare.kbd;
  void* joy = bare.joy;
  REQUIRE(kbd != nullptr);
  REQUIRE(joy != nullptr);

  uint8_t answer = 0;
  size_t size = sizeof(answer);
  CHECK(keyboard->query(kbd, unknown_joystick_id, &answer, &size) ==
        peripheral_incompatible);
  size = sizeof(answer);
  CHECK(joystick->query(joy, keyboard_query_rocker, &answer, &size) ==
        peripheral_incompatible);
  const JoystickButtonPayload_t button{0, 1, 0, 0};
  CHECK(keyboard->command(kbd, JOYSTICK_CMD_SET_BUTTON, &button,
                          sizeof(button)) == peripheral_incompatible);
  CHECK(joystick->command(joy, keyboard_cmd_set_rocker, &answer,
                          sizeof(answer)) == peripheral_incompatible);

  // An id inside the card's own subsystem that it does not know is
  // incompatible too, so a neighbour in the slot still gets asked.
  CHECK(joystick->command(joy, unknown_joystick_id, nullptr, 0) ==
        peripheral_incompatible);
  size = sizeof(answer);
  CHECK(joystick->query(joy, unknown_joystick_id, &answer, &size) ==
        peripheral_incompatible);

  // Generic commands bypass subsystem check and query each peripheral.
  size = sizeof(answer);
  CHECK(keyboard->query(kbd, PERIPHERAL_QUERY_AUDIO_INFO, &answer, &size) ==
        peripheral_incompatible);
  size = sizeof(answer);
  CHECK(joystick->query(joy, PERIPHERAL_QUERY_AUDIO_INFO, &answer, &size) ==
        peripheral_incompatible);
}

TEST_CASE("Slot 0: a command can name the peripheral it is for") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const uint8_t on = 1;
    REQUIRE(peripheral_command_by_id(0, "linapple.keyboard",
                                     keyboard_cmd_set_rocker, &on,
                                     sizeof(on)) == peripheral_ok);
    settle();
    CHECK(keyboard_state().rocker_switch == 1);

    // Verify rejection of invalid target peripheral names.
    const uint8_t off = 0;
    CHECK(peripheral_command_by_id(0, "linapple.disk_II",
                                   keyboard_cmd_set_rocker, &off,
                                   sizeof(off)) == peripheral_error);
    CHECK(peripheral_command_by_id(
              0, "linapple.a-name-that-does-not-fit-in-the-queue",
              keyboard_cmd_set_rocker, &off, sizeof(off)) == peripheral_error);
    CHECK(peripheral_command_by_id(0, nullptr, keyboard_cmd_set_rocker, &off,
                                   sizeof(off)) == peripheral_error);
    CHECK(peripheral_command_by_id(0, "linapple.keyboard",
                                   keyboard_cmd_set_rocker, nullptr,
                                   sizeof(off)) == peripheral_error);
    CHECK(peripheral_command(0, keyboard_cmd_set_rocker, nullptr,
                             sizeof(off)) == peripheral_error);
    settle();
    CHECK(keyboard_state().rocker_switch == 1);

    uint8_t rocker = 0;
    size_t size = sizeof(rocker);
    CHECK(peripheral_query_by_id(0, "linapple.joystick", keyboard_query_rocker,
                                 &rocker, &size) == peripheral_incompatible);
  }
}
