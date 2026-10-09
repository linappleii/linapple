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
#include "apple2/SwitchInputs.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"

namespace {

constexpr uint16_t addr_keyboard_data = 0xC000;
constexpr uint16_t addr_switch0 = 0xC061;
constexpr uint8_t switch_bit = 0x80;
constexpr uint8_t strobe_bit = 0x80;
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
constexpr uint32_t unknown_keyboard_id = PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x00FF;
// An id the game port once answered and no longer knows.
constexpr uint32_t retired_joystick_id = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0001;
constexpr uint32_t host_key_a = 4;

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
    // A hand-built machine gets no motherboard I/O handlers, so they and the
    // board are stated here: a //e with its keyboard in, PB0 and PB1 held low.
    mem_pre_initialize();
    REQUIRE(mem_initialize() == 0);
    switch_inputs_reset_configuration(true, true);
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
    // The bridge's modifier record outlives the machine.
    linapple_set_modifiers(false, false, false, false);
    peripheral_manager_think(0);
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

void bare_log(void*, PeripheralLogLevel_t, const char*, ...) {}
void bare_register_direct_io(void*, uint16_t, PeripheralIOHandler,
                             PeripheralIOHandler) {}
void bare_register_direct_io_strobe(void*, uint16_t,
                                    PeripheralStrobeHandler_t) {}
void bare_schedule_event(void*, uint64_t) {}
auto bare_get_cycles() -> uint64_t { return 0; }
auto bare_get_clock_hz() -> double { return 1020484.0; }
auto bare_read_floating_bus(uint32_t) -> uint8_t { return 0; }
auto bare_get_machine() -> PeripheralMachine_t {
  return peripheral_machine_apple2e;
}
auto bare_get_frame_cycles() -> uint32_t { return 17030; }

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
    host.ScheduleEvent = bare_schedule_event;
    host.GetCycles = bare_get_cycles;
    host.GetClockHz = bare_get_clock_hz;
    host.ReadFloatingBus = bare_read_floating_bus;
    host.GetMachine = bare_get_machine;
    host.GetFrameCycles = bare_get_frame_cycles;
    kbd = keyboard->init(0, &host);
    joy = joystick->init(0, &host);
  }

  ~BareHost_t() {
    if (kbd != nullptr) {
      keyboard->shutdown(kbd);
    }
    if (joy != nullptr) {
      joystick->shutdown(joy);
    }
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

auto key_event(uint32_t host_key, uint8_t code, bool down)
    -> KeyboardKeyEvent_t {
  return KeyboardKeyEvent_t{
      host_key, code, static_cast<uint8_t>(down ? 1 : 0), {0, 0, 0, 0, 0, 0}};
}

auto press_key(uint32_t host_key, uint8_t code) -> void {
  const KeyboardKeyEvent_t event = key_event(host_key, code, true);
  send(keyboard_cmd_key, &event, sizeof(event));
}

auto release_key(uint32_t host_key) -> void {
  const KeyboardKeyEvent_t event = key_event(host_key, 0, false);
  send(keyboard_cmd_key, &event, sizeof(event));
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

struct HostModifiers_t {
  bool shift = false;
  bool ctrl = false;
  bool open_apple = false;
  bool solid_apple = false;
};

auto host_modifiers() -> HostModifiers_t {
  HostModifiers_t mods;
  linapple_get_modifiers(&mods.shift, &mods.ctrl, &mods.open_apple,
                         &mods.solid_apple);
  return mods;
}

auto keyboard_data() -> uint8_t {
  return io_map_dispatch(0, addr_keyboard_data, 0, 0, 0);
}

// Bit 7 of a switch line through the memory map, as the 6502 would read it.
auto line_level(uint8_t line) -> uint8_t {
  const uint16_t addr = static_cast<uint16_t>(addr_switch0 + line);
  return (io_map_dispatch(0, addr, 0, 0, 0) & switch_bit) != 0 ? 1 : 0;
}

auto press_button(uint8_t button) -> void {
  linapple_set_game_switch(button, true);
}

auto release_button(uint8_t button) -> void {
  linapple_set_game_switch(button, false);
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

TEST_CASE("Slot 0: a key and a button under one slot") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    press_key(host_key_a, 'A');
    press_button(0);

    CHECK(keyboard_data() == ('A' | strobe_bit));
    CHECK(line_level(0) == 1);
    release_button(0);
    release_key(host_key_a);
  }
}

TEST_CASE("Slot 0: a stick move and the host's modifiers under one id") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    move_axis(0, 0, joy_off_centre);
    press_button(0);

    CHECK(paddle_pulse_is(0, off_centre_pulse));
    CHECK(line_level(0) == 1);
    CHECK_FALSE(host_modifiers().shift);
    CHECK_FALSE(host_modifiers().ctrl);
    release_button(0);
  }
}

TEST_CASE("Slot 0: a key leaves the sticks where they are") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    move_axis(0, 0, joy_off_centre);
    press_button(0);
    REQUIRE(paddle_pulse_is(0, off_centre_pulse));
    REQUIRE(line_level(0) == 1);

    press_key(host_key_a, 'A');

    CHECK(paddle_pulse_is(0, off_centre_pulse));
    CHECK(line_level(0) == 1);
    CHECK(keyboard_data() == ('A' | strobe_bit));
    release_button(0);
  }
}

TEST_CASE("Slot 0: a connector button latches no key and reaches no modifier") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const HostModifiers_t before = host_modifiers();
    press_button(1);
    settle();

    const HostModifiers_t after = host_modifiers();
    CHECK(after.shift == before.shift);
    CHECK(after.ctrl == before.ctrl);
    CHECK(after.open_apple == before.open_apple);
    CHECK(after.solid_apple == before.solid_apple);
    CHECK((keyboard_data() & strobe_bit) == 0);
    CHECK(line_level(1) == 1);
    release_button(1);
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

    linapple_set_modifiers(true, true, false, false);
    settle();

    CHECK(host_modifiers().shift);
    CHECK(host_modifiers().ctrl);
    const JoystickSaveState_t after = joystick_state();
    CHECK(line_level(0) == 1);
    CHECK(std::memcmp(&before, &after, sizeof(JoystickSaveState_t)) == 0);
    CHECK(paddle_pulse_is(0, off_centre_pulse));
    release_button(0);
  }
}

TEST_CASE("Slot 0: a key reaches no switch line") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    // PB0 and PB1 rest low through the keyboard's pull-downs; PB2 has none, so
    // its open TTL input rests high (Sather IIe 7-8; Understanding the Apple
    // II, 7-9 and 7-11).
    REQUIRE(line_level(0) == 0);
    REQUIRE(line_level(1) == 0);
    REQUIRE(line_level(2) == 1);

    press_key(host_key_a, 'A');
    CHECK(keyboard_data() == ('A' | strobe_bit));
    CHECK(line_level(0) == 0);
    CHECK(line_level(1) == 0);
    CHECK(line_level(2) == 1);
    release_key(host_key_a);
  }
}

TEST_CASE("Slot 0: a payload of the wrong size changes nothing") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    press_key(host_key_a, 'A');
    release_key(host_key_a);
    static_cast<void>(io_map_dispatch(0, 0xC010, 0, 0, 0));
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
    const OneByteLong_t<KeyboardKeyEvent_t> key{key_event(5, 'B', true), 0};
    REQUIRE(peripheral_command(0, keyboard_cmd_key, &key,
                               sizeof(KeyboardKeyEvent_t) - 1) ==
            peripheral_ok);
    REQUIRE(peripheral_command(0, keyboard_cmd_key, &key,
                               sizeof(KeyboardKeyEvent_t) + 1) ==
            peripheral_ok);
    settle();

    const KeyboardSaveState_t keyboard_after = keyboard_state();
    const JoystickSaveState_t joystick_after = joystick_state();
    CHECK(keyboard_data() == 'A');
    CHECK(line_level(0) == 0);
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
  const OneByteLong_t<KeyboardKeyEvent_t> key{key_event(host_key_a, 'A', true),
                                              0};
  constexpr size_t key_size = sizeof(KeyboardKeyEvent_t);
  CHECK(keyboard->command(kbd, keyboard_cmd_key, &key, key_size) ==
        peripheral_ok);
  CHECK(keyboard->command(kbd, keyboard_cmd_key, &key, 0) == peripheral_error);
  CHECK(keyboard->command(kbd, keyboard_cmd_key, &key, key_size - 1) ==
        peripheral_error);
  CHECK(keyboard->command(kbd, keyboard_cmd_key, &key, key_size + 1) ==
        peripheral_error);
  CHECK(keyboard->command(kbd, keyboard_cmd_key, nullptr, key_size) ==
        peripheral_error);
  const KeyboardKeyEvent_t too_high = key_event(host_key_a, 0x80, true);
  CHECK(keyboard->command(kbd, keyboard_cmd_key, &too_high, key_size) ==
        peripheral_error);
  CHECK(keyboard->command(kbd, keyboard_cmd_release_all, &byte, sizeof(byte)) ==
        peripheral_error);
  CHECK(keyboard->command(kbd, keyboard_cmd_release_all, nullptr, 0) ==
        peripheral_ok);
  CHECK(keyboard->command(kbd, keyboard_cmd_rept, &byte, sizeof(byte)) ==
        peripheral_ok);
  CHECK(keyboard->command(kbd, keyboard_cmd_rept, &two_bytes, 2) ==
        peripheral_error);
  CHECK(keyboard->command(kbd, keyboard_cmd_rept, nullptr, 1) ==
        peripheral_error);

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
}

TEST_CASE("Slot 0: an id from another subsystem is refused and changes none") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    press_key(host_key_a, 'A');
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
    CHECK(keyboard_data() == ('A' | strobe_bit));
    CHECK(std::memcmp(&keyboard_before, &keyboard_after,
                      sizeof(KeyboardSaveState_t)) == 0);
    CHECK(std::memcmp(&joystick_before, &joystick_after,
                      sizeof(JoystickSaveState_t)) == 0);
    release_button(0);
    release_key(host_key_a);
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
  CHECK(joystick->query(joy, unknown_keyboard_id, &answer, &size) ==
        peripheral_incompatible);
  const std::array<uint8_t, 4> button{{0, 1, 0, 0}};
  CHECK(keyboard->command(kbd, retired_joystick_id, button.data(),
                          button.size()) == peripheral_incompatible);
  const KeyboardKeyEvent_t key = key_event(host_key_a, 'A', true);
  CHECK(joystick->command(joy, keyboard_cmd_key, &key, sizeof(key)) ==
        peripheral_incompatible);
  CHECK(joystick->command(joy, keyboard_cmd_release_all, nullptr, 0) ==
        peripheral_incompatible);

  // An id inside the card's own subsystem that it does not know is
  // incompatible too, so a neighbour in the slot still gets asked.
  CHECK(keyboard->command(kbd, unknown_keyboard_id, nullptr, 0) ==
        peripheral_incompatible);
  CHECK(keyboard->command(kbd, unknown_keyboard_id, &key, sizeof(key)) ==
        peripheral_incompatible);
  size = sizeof(answer);
  CHECK(keyboard->query(kbd, unknown_keyboard_id, &answer, &size) ==
        peripheral_incompatible);
  CHECK(joystick->command(joy, unknown_joystick_id, nullptr, 0) ==
        peripheral_incompatible);
  CHECK(joystick->command(joy, retired_joystick_id, button.data(),
                          button.size()) == peripheral_incompatible);
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

    const KeyboardKeyEvent_t down = key_event(host_key_a, 'A', true);
    REQUIRE(peripheral_command_by_id(0, "linapple.keyboard", keyboard_cmd_key,
                                     &down, sizeof(down)) == peripheral_ok);
    settle();
    CHECK(keyboard_data() == ('A' | strobe_bit));

    const KeyboardKeyEvent_t other = key_event(5, 'B', true);
    CHECK(peripheral_command_by_id(0, "linapple.disk_II", keyboard_cmd_key,
                                   &other, sizeof(other)) == peripheral_error);
    CHECK(peripheral_command_by_id(
              0, "linapple.a-name-that-does-not-fit-in-the-queue",
              keyboard_cmd_key, &other, sizeof(other)) == peripheral_error);
    CHECK(peripheral_command_by_id(0, nullptr, keyboard_cmd_key, &other,
                                   sizeof(other)) == peripheral_error);
    CHECK(peripheral_command_by_id(0, "linapple.keyboard", keyboard_cmd_key,
                                   nullptr, sizeof(other)) == peripheral_error);
    CHECK(peripheral_command(0, keyboard_cmd_key, nullptr, sizeof(other)) ==
          peripheral_error);
    settle();
    CHECK(keyboard_data() == ('A' | strobe_bit));

    uint8_t answer = 0;
    size_t size = sizeof(answer);
    CHECK(peripheral_query_by_id(0, "linapple.joystick", unknown_keyboard_id,
                                 &answer, &size) == peripheral_incompatible);
    release_key(host_key_a);
  }
}
