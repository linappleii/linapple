// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/sdl/JoystickFrontend.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "SdlBackend.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/JoystickConfig.h"

inline auto clamp_val(int val, int low, int high) -> int {
  return (val < low) ? low : (val > high) ? high : val;
}

struct JoyCoord {
  int x;
  int y;
};

// In keypad order so KP_1-KP_9 index directly; 0 and '.' are the buttons.
enum JoyKey {
  jk_downleft = 0,
  jk_down,
  jk_downright,
  jk_left,
  jk_centre,
  jk_right,
  jk_upleft,
  jk_up,
  jk_upright,
  jk_button0,
  jk_button1,
  jk_max,
};

constexpr int pdl_central = 127;
constexpr int pdl_min = 0;
constexpr int pdl_max = 255;

static std::array<bool, jk_max> key_down = {false};
constexpr int pdl_smax = 127;
constexpr int pdl_scentral = 0;
constexpr int pdl_smin = -127;

static const std::array<JoyCoord, 9> key_value = {
    {
        {pdl_smin, pdl_smax},
        {pdl_scentral, pdl_smax},
        {pdl_smax, pdl_smax},
        {pdl_smin, pdl_scentral},
        {pdl_scentral, pdl_scentral},
        {pdl_smax, pdl_scentral},
        {pdl_smin, pdl_smin},
        {pdl_scentral, pdl_smin},
        {pdl_smax, pdl_smin},
    },
};

static std::array<int, 2> joy_shr_x = {8, 8};
static std::array<int, 2> joy_shr_y = {8, 8};
static std::array<int, 2> joy_sub_x = {0, 0};
static std::array<int, 2> joy_sub_y = {0, 0};

// NOLINTBEGIN(misc-include-cleaner): SdlJoystickPtr is provided across SDL1/2/3 backends via SdlBackend.h
static SdlJoystickPtr joy1;
static SdlJoystickPtr joy2;
// NOLINTEND(misc-include-cleaner)

// Trim is host calibration: one offset per axis, seeded from the two PDL
// keys and adjusted with Right-Ctrl and the arrows, added to every position
// of every device before it reaches the port.
constexpr int trim_min = -128;
constexpr int trim_max = 127;
static int trim_x = 0;
static int trim_y = 0;

// Which host device feeds each Apple joystick and which of its axes and
// buttons. Host input mapping is the frontend's alone; the card only ever
// receives positions and switch levels.
struct JoystickHostConfig {
  std::array<uint32_t, 2> joy_type{};
  std::array<uint32_t, 2> joy_index{};
  std::array<uint32_t, 2> joy0_button_map{};
  uint32_t joy1_button_map = 0;
  std::array<std::array<uint32_t, 2>, 2> joy_axis{};
};

static JoystickHostConfig joy_config;

constexpr uint8_t switch_line_count = 3;

// A press and its release delivered in one SDL pump would reach the port
// with no emulated cycles between them, which no program could see and no
// hand can do. Edges on a line therefore queue: at most one send per line
// per emulation slice, in order. The gamepad poll bypasses the queue, as it
// writes the level it read rather than an edge.
constexpr size_t switch_queue_capacity = 8;

struct SwitchQueue {
  std::array<bool, switch_queue_capacity> levels{};
  size_t head = 0;
  size_t count = 0;
  // The last level accepted, whether sent or still queued.
  bool tail_level = false;
  // A send since the last slice holds the next edge back one slice.
  bool sent_since_slice = false;
};

static std::array<SwitchQueue, switch_line_count> switch_queues;

static auto device_of(size_t joy_num) -> JoystickDevice {
  return joystick_config_device(joy_config.joy_type.at(joy_num));
}

static auto device_button_lines(size_t joy_num, int button) -> uint8_t {
  return joystick_config_button_lines(joy_num, button, joy_config.joy_type[0],
                                      joy_config.joy_type[1]);
}

static auto send_connector_switch(uint8_t line, bool down) -> void {
  linapple_set_game_switch(line, down);
}

static auto send_connector_lines(uint8_t lines, bool down) -> void {
  for (uint8_t line = 0; line < switch_line_count; ++line) {
    if ((lines & (1U << line)) != 0) {
      send_connector_switch(line, down);
    }
  }
}

static auto queue_push(SwitchQueue& queue, bool down) -> void {
  if (queue.count == switch_queue_capacity) {
    // Queued levels alternate, so dropping the two oldest keeps every later
    // edge in order and the final level intact.
    queue.head = (queue.head + 2) % switch_queue_capacity;
    queue.count -= 2;
  }
  queue.levels.at((queue.head + queue.count) % switch_queue_capacity) = down;
  ++queue.count;
}

static auto queue_pop(SwitchQueue& queue) -> bool {
  const bool down = queue.levels.at(queue.head);
  queue.head = (queue.head + 1) % switch_queue_capacity;
  --queue.count;
  return down;
}

static auto queue_connector_switch(uint8_t line, bool down) -> void {
  SwitchQueue& queue = switch_queues.at(line);
  if (down == queue.tail_level) {
    return;
  }
  queue.tail_level = down;
  if (queue.count == 0 && !queue.sent_since_slice) {
    send_connector_switch(line, down);
    queue.sent_since_slice = true;
    return;
  }
  queue_push(queue, down);
}

static auto queue_connector_lines(uint8_t lines, bool down) -> void {
  for (uint8_t line = 0; line < switch_line_count; ++line) {
    if ((lines & (1U << line)) != 0) {
      queue_connector_switch(line, down);
    }
  }
}

// One slice has run since the last call, so each line may send one edge.
static auto drain_switch_queues() -> void {
  for (uint8_t line = 0; line < switch_line_count; ++line) {
    SwitchQueue& queue = switch_queues.at(line);
    if (!queue.sent_since_slice && queue.count > 0) {
      send_connector_switch(line, queue_pop(queue));
    }
    queue.sent_since_slice = false;
  }
}

static auto flush_switch_queues() -> void {
  for (uint8_t line = 0; line < switch_line_count; ++line) {
    SwitchQueue& queue = switch_queues.at(line);
    if (queue.count > 0) {
      send_connector_switch(line, queue.tail_level);
    }
    queue = SwitchQueue{};
  }
}

static auto send_axis(uint8_t joy_num, uint8_t axis, int position) -> void {
  const int trimmed =
      clamp_val(position + (axis == 0 ? trim_x : trim_y), pdl_min, pdl_max);
  const JoystickAxisPayload_t payload = {
      joy_num,
      axis,
      static_cast<uint8_t>(trimmed),
      0,
  };
  peripheral_command(0, JOYSTICK_CMD_SET_AXIS, &payload, sizeof(payload));
}

static auto keypad_joystick() -> int {
  if (device_of(0) == joystick_device_keyboard) {
    return 0;
  }
  if (device_of(1) == joystick_device_keyboard) {
    return 1;
  }
  return -1;
}

// The keypad's position is the mean of the directions held, with a pair of
// adjacent edges read as their corner, so that "up" plus "left" is "up-left".
static auto send_keypad_axes(size_t joy_num) -> void {
  int xsum = 0;
  int ysum = 0;
  int keydown_count = 0;
  static constexpr std::array<int, 16> corner_convert_lookup = {
      {-1, -1, -1, 8, -1, 6, -1, -1, -1, -1, 2, -1, 0, -1, -1, -1},
  };
  int corner_idx = (static_cast<int>(0 == key_down.at(1))) |
                   (static_cast<int>(0 == key_down.at(3)) << 1) |
                   (static_cast<int>(0 == key_down.at(5)) << 2) |
                   (static_cast<int>(0 == key_down.at(7)) << 3);
  int corner_override_idx =
      corner_convert_lookup.at(static_cast<size_t>(corner_idx));
  if (corner_override_idx >= 0) {
    xsum = key_value.at(static_cast<size_t>(corner_override_idx)).x;
    ysum = key_value.at(static_cast<size_t>(corner_override_idx)).y;
    keydown_count = 1;
  } else {
    for (size_t i = 0; i < 9; i++) {
      if (key_down.at(i)) {
        keydown_count++;
        xsum += key_value.at(i).x;
        ysum += key_value.at(i).y;
      }
    }
  }
  int x = pdl_central;
  int y = pdl_central;
  if (keydown_count != 0) {
    x += xsum / keydown_count;
    y += ysum / keydown_count;
  }
  send_axis(static_cast<uint8_t>(joy_num), 0, x);
  send_axis(static_cast<uint8_t>(joy_num), 1, y);
}

static auto refresh_keypad_axes() -> void {
  const int joy_num = keypad_joystick();
  if (joy_num >= 0) {
    send_keypad_axes(static_cast<size_t>(joy_num));
  }
}

static auto clamp_trim(int trim) -> int {
  return clamp_val(trim, trim_min, trim_max);
}

static auto load_trim(const char* key) -> int {
  uint32_t raw = 0;
  if (!load(key, &raw)) {
    return 0;
  }
  // The registry parses through an unsigned type; a negative entry comes
  // back as its two's complement.
  return clamp_trim(static_cast<int32_t>(raw));
}

auto joy_frontend_initialize() -> void {
  constexpr int16_t axis_min = -32768;
  constexpr int16_t axis_max = 32767;

  joy1.reset();
  joy2.reset();

  joy_config = {};
  uint32_t val = 0;
  if (load(cfg_joy_type1, &val)) {
    joy_config.joy_type[0] = (val < joystick_config_type_count) ? val : 0;
  }
  if (load(cfg_joy_type2, &val)) {
    joy_config.joy_type[1] = (val < joystick_config_type_count) ? val : 0;
  }
  if (load(cfg_joy_index1, &val)) {
    joy_config.joy_index[0] = val;
  }
  if (load(cfg_joy_index2, &val)) {
    joy_config.joy_index[1] = val;
  }
  if (load(cfg_joy_button1_1, &val)) {
    joy_config.joy0_button_map[0] = val;
  }
  if (load(cfg_joy_button1_2, &val)) {
    joy_config.joy0_button_map[1] = val;
  }
  if (load(cfg_joy_button2_1, &val)) {
    joy_config.joy1_button_map = val;
  }
  if (load(cfg_joy_axis1_0, &val)) {
    joy_config.joy_axis[0][0] = val;
  }
  if (load(cfg_joy_axis1_1, &val)) {
    joy_config.joy_axis[0][1] = val;
  }
  if (load(cfg_joy_axis2_0, &val)) {
    joy_config.joy_axis[1][0] = val;
  }
  if (load(cfg_joy_axis2_1, &val)) {
    joy_config.joy_axis[1][1] = val;
  }

  trim_x = load_trim(cfg_pdl_xtrim);
  trim_y = load_trim(cfg_pdl_ytrim);

  // The jumper is soldered in or out; it is read with the configuration and
  // never changes while the machine runs.
  linapple_set_shift_key_mod(load(cfg_shift_key_mod, &val) && val != 0);

  const int number_of_joysticks = sdl_compat_num_joysticks();

  auto open_device = [&](size_t joy_id, SdlJoystickPtr& joy_ptr,
                         uint32_t fallback_type) {
    if (device_of(joy_id) != joystick_device_joystick) {
      return;
    }
    const auto joy_idx = static_cast<int>(joy_config.joy_index.at(joy_id));
    if (number_of_joysticks > static_cast<int>(joy_id) &&
        joy_idx < number_of_joysticks) {
      joy_ptr = sdl_compat_open_joystick(joy_idx);
      joy_shr_x.at(joy_id) = 0;
      joy_shr_y.at(joy_id) = 0;
      joy_sub_x.at(joy_id) = axis_min;
      joy_sub_y.at(joy_id) = axis_min;
      uint32_t xrange = axis_max - axis_min;
      uint32_t yrange = axis_max - axis_min;
      while (xrange > 256) {
        xrange >>= 1;
        ++joy_shr_x.at(joy_id);
      }
      while (yrange > 256) {
        yrange >>= 1;
        ++joy_shr_y.at(joy_id);
      }
      return;
    }
    joy_config.joy_type.at(joy_id) = fallback_type;
  };

  open_device(0, joy1, 4);
  open_device(1, joy2, joystick_device_none);

  // The mask follows the devices actually present, so a configured second
  // stick that is not plugged in leaves PB2 open as the hardware would.
  linapple_set_game_pulldowns(joystick_config_pulldown_mask(
      joy_config.joy_type[0], joy_config.joy_type[1]));

  // Start the queues and the port from the same released levels, whatever a
  // previous session left held.
  for (uint8_t line = 0; line < switch_line_count; ++line) {
    switch_queues.at(line) = SwitchQueue{};
    send_connector_switch(line, false);
  }
  key_down.fill(false);
  refresh_keypad_axes();
}

auto joy_frontend_shutdown() -> void {
  flush_switch_queues();
  joy1.reset();
  joy2.reset();
}

// "Square" a modern analog stick, whose circular travel would otherwise
// never reach the corners a self-centring Apple stick reaches.
static auto square_stick(int& x, int& y) -> void {
  const int low_threshold = pdl_central / 2;
  const int high_threshold = pdl_central + (pdl_central / 2);

  if (y < low_threshold && x < low_threshold) {
    x -= (low_threshold - y) / 2;
    y -= (low_threshold - x) / 2;
  } else if (y < low_threshold && x > high_threshold) {
    x += (low_threshold - y) / 2;
    y -= (x - high_threshold) / 2;
  } else if (y > high_threshold && x < low_threshold) {
    x -= (y - high_threshold) / 2;
    y += (low_threshold - x) / 2;
  } else if (y > high_threshold && x > high_threshold) {
    x += (y - high_threshold) / 2;
    y += (x - high_threshold) / 2;
  }
}

// NOLINTNEXTLINE(misc-include-cleaner): SDL_Joystick is provided across SDL1/2/3 backends via SdlBackend.h
static auto poll_gamepad(size_t joy_num, SDL_Joystick* joystick) -> void {
  sdl_compat_update_joysticks();

  if (joy_num == 0) {
    const bool b0 = sdl_compat_get_joystick_button(
        joystick, static_cast<int>(joy_config.joy0_button_map[0]));
    send_connector_lines(device_button_lines(0, 0), b0);
    const uint8_t second_button_lines = device_button_lines(0, 1);
    if (second_button_lines != 0) {
      const bool b1 = sdl_compat_get_joystick_button(
          joystick, static_cast<int>(joy_config.joy0_button_map[1]));
      send_connector_lines(second_button_lines, b1);
    }
  } else {
    const bool b2 = sdl_compat_get_joystick_button(
        joystick, static_cast<int>(joy_config.joy1_button_map));
    send_connector_lines(device_button_lines(1, 0), b2);
  }

  int x = (static_cast<int>(sdl_compat_get_joystick_axis(
               joystick, static_cast<int>(joy_config.joy_axis[joy_num][0]))) -
           joy_sub_x.at(joy_num)) >>
          joy_shr_x.at(joy_num);
  int y = (static_cast<int>(sdl_compat_get_joystick_axis(
               joystick, static_cast<int>(joy_config.joy_axis[joy_num][1]))) -
           joy_sub_y.at(joy_num)) >>
          joy_shr_y.at(joy_num);
  if (joy_num == 0) {
    square_stick(x, y);
  }
  send_axis(static_cast<uint8_t>(joy_num), 0, x);
  send_axis(static_cast<uint8_t>(joy_num), 1, y);
}

auto joy_frontend_update() -> void {
  drain_switch_queues();

  auto poll_if_due = [](size_t joy_id, SdlJoystickPtr& joy_ptr,
                        uint32_t& last_check) {
    if (!joy_ptr || device_of(joy_id) != joystick_device_joystick) {
      return;
    }
    const uint32_t curr_time = sdl_compat_get_ticks();
    if (curr_time - last_check < 10) {
      return;
    }
    last_check = curr_time;
    poll_gamepad(joy_id, joy_ptr.get());
  };

  static uint32_t last_check1 = 0;
  static uint32_t last_check2 = 0;
  poll_if_due(0, joy1, last_check1);
  poll_if_due(1, joy2, last_check2);
}

// NOLINTBEGIN(misc-include-cleaner): Keycodes (SDLK_*) are provided across SDL1/2/3 backends via SdlBackend.h
auto joy_frontend_update_trim_via_key(uint32_t virtkey) -> void {
  switch (virtkey) {
    case SDLK_DOWN:
    case SDLK_KP_2:
      trim_y = clamp_trim(trim_y + 1);
      break;
    case SDLK_KP_4:
    case SDLK_LEFT:
      trim_x = clamp_trim(trim_x - 1);
      break;
    case SDLK_KP_6:
    case SDLK_RIGHT:
      trim_x = clamp_trim(trim_x + 1);
      break;
    case SDLK_KP_8:
    case SDLK_UP:
      trim_y = clamp_trim(trim_y - 1);
      break;
    case SDLK_KP_5:
    case SDLK_CLEAR:
      trim_x = 0;
      trim_y = 0;
      break;
    default:
      return;
  }
  refresh_keypad_axes();
}

auto joy_frontend_process_key(uint32_t virtkey, bool extended, bool down,
                              bool autorep) -> bool {
  const int joy_num = keypad_joystick();
  if (joy_num == -1 || extended) {
    return false;
  }

  const JoystickMode centering_type = joystick_config_mode(
      joy_config.joy_type.at(static_cast<size_t>(joy_num)));

  if ((virtkey >= SDLK_KP_1) && (virtkey <= SDLK_KP_9)) {
    key_down.at(static_cast<size_t>(virtkey - SDLK_KP_1)) = down;
  } else {
    switch (virtkey) {
      case SDLK_END:
        key_down.at(0) = down;
        break;
      case SDLK_DOWN:
        key_down.at(1) = down;
        break;
      case SDLK_PAGEDOWN:
        key_down.at(2) = down;
        break;
      case SDLK_LEFT:
        key_down.at(3) = down;
        break;
      case SDLK_CLEAR:
        key_down.at(4) = down;
        break;
      case SDLK_RIGHT:
        key_down.at(5) = down;
        break;
      case SDLK_HOME:
        key_down.at(6) = down;
        break;
      case SDLK_UP:
        key_down.at(7) = down;
        break;
      case SDLK_PAGEUP:
        key_down.at(8) = down;
        break;
      case SDLK_KP_0:
      case SDLK_INSERT:
        key_down.at(jk_button0) = down;
        break;
      case SDLK_KP_PERIOD:
      case SDLK_DELETE:
        key_down.at(jk_button1) = down;
        break;
      default:
        return false;
    }
  }

  const auto joystick = static_cast<size_t>(joy_num);
  if ((virtkey == SDLK_KP_0) || (virtkey == SDLK_INSERT)) {
    queue_connector_lines(device_button_lines(joystick, 0), down);
  } else if ((virtkey == SDLK_KP_PERIOD) || (virtkey == SDLK_DELETE)) {
    queue_connector_lines(device_button_lines(joystick, 1), down);
  } else if ((down && !autorep) ||
             (centering_type == joystick_mode_centering)) {
    send_keypad_axes(joystick);
  }
  return true;
}
// NOLINTEND(misc-include-cleaner)

auto joy_frontend_is_mouse_emulation_active() -> bool {
  return device_of(0) == joystick_device_mouse ||
         device_of(1) == joystick_device_mouse;
}

auto joy_frontend_process_mouse_motion(int x, int max_x, int y, int max_y)
    -> void {
  int range_x = (max_x > 1) ? (max_x - 1) : 1;
  int range_y = (max_y > 1) ? (max_y - 1) : 1;
  int joy_x = ((x * 255) + (range_x / 2)) / range_x;
  int joy_y = ((y * 255) + (range_y / 2)) / range_y;

  for (uint8_t joy_num = 0; joy_num < 2; ++joy_num) {
    if (device_of(joy_num) == joystick_device_mouse) {
      send_axis(joy_num, 0, joy_x);
      send_axis(joy_num, 1, joy_y);
    }
  }
}

auto joy_frontend_process_mouse_button(int button, bool down) -> void {
  for (size_t joy_num = 0; joy_num < 2; ++joy_num) {
    if (device_of(joy_num) == joystick_device_mouse) {
      queue_connector_lines(device_button_lines(joy_num, button), down);
    }
  }
}
