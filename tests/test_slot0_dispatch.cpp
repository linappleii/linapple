// Integration tests: slot 0 command and query dispatch across keyboard and
// joystick.

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <ostream>

#include "apple2/Apple2Types.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/keyboard/Keyboard.h"
#include "core/LinAppleCore.h"
#include "doctest.h"

namespace {

constexpr uint16_t addr_keyboard = 0xC000;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint8_t key_strobe_bit = 0x80;
constexpr uint8_t key_code_mask = 0x7F;
constexpr uint8_t joy_centre = 127;
constexpr uint8_t joy_off_centre = 200;
constexpr uint32_t key_a = 'A';

enum class Order_t : uint8_t { keyboard_first, joystick_first };

// So that CAPTURE(order) says which pass a failure came from.
auto operator<<(std::ostream& out, Order_t order) -> std::ostream& {
  return out << (order == Order_t::keyboard_first ? "keyboard first"
                                                  : "joystick first");
}

// Slot 0 fixture managing registration and teardown of keyboard and joystick.
struct Slot0_t {
  eApple2Type saved_type{g_apple2_type};
  int keyboard_registered{-1};
  int joystick_registered{-1};

  explicit Slot0_t(Order_t order) {
    g_apple2_type = A2TYPE_APPLE2EENHANCED;
    mem_initialize();
    peripheral_manager_init();
    if (order == Order_t::keyboard_first) {
      keyboard_registered = peripheral_register(keyboard_get_descriptor(), 0);
      joystick_registered = peripheral_register(joystick_get_descriptor(), 0);
    } else {
      joystick_registered = peripheral_register(joystick_get_descriptor(), 0);
      keyboard_registered = peripheral_register(keyboard_get_descriptor(), 0);
    }
    peripheral_manager_reset();
  }

  ~Slot0_t() {
    peripheral_manager_shutdown();
    mem_destroy();
    g_apple2_type = saved_type;
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

struct BareHost_t {
  HostInterface_t host{};
  Peripheral_t* keyboard{keyboard_get_descriptor()};
  Peripheral_t* joystick{joystick_get_descriptor()};
  void* kbd{nullptr};
  void* joy{nullptr};

  BareHost_t() {
    host.Log = bare_log;
    host.RegisterDirectIO = bare_register_direct_io;
    host.RegisterDirectIOStrobe = bare_register_direct_io_strobe;
    host.GetCycles = bare_get_cycles;
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

auto read_io(uint16_t addr) -> uint8_t {
  return io_map_dispatch(0, addr, 0, 0, 0);
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

auto joystick_config() -> JoystickConfig_t {
  JoystickConfig_t config{};
  size_t size = sizeof(config);
  REQUIRE(peripheral_query_by_id(0, "linapple.joystick", JOY_QUERY_CONFIG,
                                 &config, &size) == peripheral_ok);
  return config;
}

auto keyboard_mods() -> KeyboardModifiers_t {
  KeyboardModifiers_t mods{};
  size_t size = sizeof(mods);
  REQUIRE(peripheral_query_by_id(0, "linapple.keyboard", keyboard_query_mods,
                                 &mods, &size) == peripheral_ok);
  return mods;
}

auto press_key(uint32_t key) -> void {
  KeyboardEvent_t event{};
  event.key = key;
  event.is_down = 1;
  send(keyboard_cmd_event, &event, sizeof(event));
}

auto press_button(uint8_t button) -> void {
  const JoystickButtonPayload_t payload{button, true, {0, 0}};
  send(JOY_CMD_SET_BUTTON, &payload, sizeof(payload));
}

auto move_axis(uint8_t joystick, uint8_t axis, uint8_t value) -> void {
  const JoystickAxisPayload_t payload{joystick, axis, value, 0};
  send(JOY_CMD_SET_AXIS, &payload, sizeof(payload));
}

auto a_config() -> JoystickConfig_t {
  JoystickConfig_t config{};
  config.joy_type[0] = 1;  // would land on KeyboardModifiers_t::shift
  config.joy_type[1] = 1;  // and on ::ctrl
  config.joy_index[0] = 5;
  config.joy_exit_enable = 1;
  return config;
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

TEST_CASE("Slot 0: the rocker switch and the joystick's exit event") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const uint8_t on = 1;
    send(keyboard_cmd_set_rocker, &on, sizeof(on));

    uint8_t rocker = 0;
    size_t size = sizeof(rocker);
    CHECK(peripheral_query(0, keyboard_query_rocker, &rocker, &size) ==
          peripheral_ok);
    CHECK(rocker == 1);

    uint8_t exiting = 0xFF;
    size = sizeof(exiting);
    CHECK(peripheral_query(0, JOY_QUERY_EXIT_EVENT, &exiting, &size) ==
          peripheral_ok);
    CHECK(exiting == 0);
  }
}

TEST_CASE("Slot 0: the modifiers and the joystick config under one id") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const JoystickConfig_t wanted = a_config();
    send(JOY_CMD_SET_CONFIG, &wanted, sizeof(wanted));

    CHECK(joystick_config().joy_index[0] == 5);
    CHECK(keyboard_mods().shift == 0);
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
    REQUIRE(joystick_state().x_pos[0] == joy_off_centre);
    REQUIRE(joystick_state().buttons[0] == 1);

    const uint8_t on = 1;
    send(keyboard_cmd_set_rocker, &on, sizeof(on));

    CHECK(joystick_state().x_pos[0] == joy_off_centre);
    CHECK(joystick_state().buttons[0] == 1);
    CHECK(keyboard_state().rocker_switch == 1);
  }
}

TEST_CASE("Slot 0: resetting the joystick leaves the keyboard alone") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const uint8_t on = 1;
    send(keyboard_cmd_set_rocker, &on, sizeof(on));
    press_key(key_a);
    move_axis(0, 0, joy_off_centre);
    press_button(1);
    REQUIRE((read_io(addr_keyboard) & key_strobe_bit) != 0);

    send(JOY_CMD_RESET, nullptr, 0);

    CHECK(joystick_state().x_pos[0] == joy_centre);
    CHECK(joystick_state().buttons[1] == 0);
    CHECK(keyboard_state().rocker_switch == 1);
    // The key the machine has not read yet is still waiting at $C000.
    CHECK((read_io(addr_keyboard) & key_strobe_bit) != 0);
    CHECK((read_io(addr_keyboard) & key_code_mask) == key_a);
  }
}

TEST_CASE("Slot 0: a joystick config does not hold down shift") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const KeyboardModifiers_t before = keyboard_mods();
    const JoystickConfig_t wanted = a_config();
    send(JOY_CMD_SET_CONFIG, &wanted, sizeof(wanted));

    const KeyboardModifiers_t after = keyboard_mods();
    CHECK(after.shift == 0);
    CHECK(after.ctrl == 0);
    CHECK(after.caps == before.caps);
    CHECK(joystick_config().joy_type[0] == 1);
  }
}

TEST_CASE("Slot 0: setting modifiers does not reconfigure the joystick") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const JoystickConfig_t wanted = a_config();
    send(JOY_CMD_SET_CONFIG, &wanted, sizeof(wanted));
    KeyboardModifiers_t mods{};
    mods.shift = 1;
    mods.ctrl = 1;
    send(keyboard_cmd_set_mods, &mods, sizeof(mods));

    CHECK(keyboard_mods().shift == 1);
    CHECK(keyboard_mods().ctrl == 1);
    const JoystickConfig_t config = joystick_config();
    CHECK(config.joy_index[0] == 5);
    CHECK(config.joy_exit_enable == 1);
  }
}

TEST_CASE("Slot 0: a joystick trim does not toggle caps lock") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    Slot0_t slot0(order);
    REQUIRE(slot0.keyboard_registered == 0);
    REQUIRE(slot0.joystick_registered == 0);

    const uint8_t caps_off = 0;
    send(keyboard_cmd_set_caps, &caps_off, sizeof(caps_off));
    REQUIRE(keyboard_state().caps_lock == 0);

    const JoystickTrimPayload_t trim{true, 0, 40};
    send(JOY_CMD_SET_TRIM, &trim, sizeof(trim));

    CHECK(keyboard_state().caps_lock == 0);
    CHECK(joystick_state().trim_x == 40);
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
    const JoystickConfig_t config_before = joystick_config();

    // Verify dispatch rejects invalid payload lengths.
    const OneByteLong_t<JoystickConfig_t> config{a_config(), 0};
    REQUIRE(peripheral_command(0, JOY_CMD_SET_CONFIG, &config,
                               sizeof(JoystickConfig_t) - 1) == peripheral_ok);
    REQUIRE(peripheral_command(0, JOY_CMD_SET_CONFIG, &config,
                               sizeof(JoystickConfig_t) + 1) == peripheral_ok);
    const uint8_t trailing = 0;
    REQUIRE(peripheral_command(0, JOY_CMD_RESET, &trailing, sizeof(trailing)) ==
            peripheral_ok);
    KeyboardModifiers_t mods{};
    mods.shift = 1;
    REQUIRE(peripheral_command(0, keyboard_cmd_set_mods, &mods,
                               sizeof(mods) - 1) == peripheral_ok);
    settle();

    const KeyboardSaveState_t keyboard_after = keyboard_state();
    const JoystickSaveState_t joystick_after = joystick_state();
    CHECK(std::memcmp(&keyboard_before, &keyboard_after,
                      sizeof(KeyboardSaveState_t)) == 0);
    CHECK(std::memcmp(&joystick_before, &joystick_after,
                      sizeof(JoystickSaveState_t)) == 0);
    const JoystickConfig_t config_after = joystick_config();
    CHECK(std::memcmp(&config_before, &config_after,
                      sizeof(JoystickConfig_t)) == 0);
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

  const OneByteLong_t<JoystickConfig_t> config{a_config(), 0};
  constexpr size_t config_size = sizeof(JoystickConfig_t);
  CHECK(joystick->command(joy, JOY_CMD_SET_CONFIG, &config, config_size) ==
        peripheral_ok);
  CHECK(joystick->command(joy, JOY_CMD_SET_CONFIG, &config, config_size - 1) ==
        peripheral_error);
  CHECK(joystick->command(joy, JOY_CMD_SET_CONFIG, &config, config_size + 1) ==
        peripheral_error);
  CHECK(joystick->command(joy, JOY_CMD_SET_CONFIG, nullptr, config_size) ==
        peripheral_error);
  CHECK(joystick->command(joy, JOY_CMD_RESET, nullptr, 0) == peripheral_ok);
  CHECK(joystick->command(joy, JOY_CMD_RESET, &byte, sizeof(byte)) ==
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
    const JoystickConfig_t config_before = joystick_config();

    // Verify commands belonging to foreign subsystems are rejected.
    const std::initializer_list<uint32_t> foreign = {
        PERIPHERAL_SUBSYSTEM_DISK | 0x0003,
        PERIPHERAL_SUBSYSTEM_HARDDISK | 0x0004,
        PERIPHERAL_SUBSYSTEM_MOUSE | 0x0001,
        PERIPHERAL_SUBSYSTEM_SERIAL | 0x0002,
    };
    const JoystickConfig_t config = a_config();
    for (uint32_t cmd_id : foreign) {
      REQUIRE(peripheral_command(0, cmd_id, &config, sizeof(config)) ==
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
    const JoystickConfig_t config_after = joystick_config();
    CHECK(std::memcmp(&config_before, &config_after,
                      sizeof(JoystickConfig_t)) == 0);
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
  CHECK(keyboard->query(kbd, JOY_QUERY_EXIT_EVENT, &answer, &size) ==
        peripheral_incompatible);
  size = sizeof(answer);
  CHECK(joystick->query(joy, keyboard_query_rocker, &answer, &size) ==
        peripheral_incompatible);
  CHECK(keyboard->command(kbd, JOY_CMD_RESET, nullptr, 0) ==
        peripheral_incompatible);
  CHECK(joystick->command(joy, keyboard_cmd_set_rocker, &answer,
                          sizeof(answer)) == peripheral_incompatible);

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
