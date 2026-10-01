// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/sdl/JoystickFrontend.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/sdl/SdlCompat.h"

enum {
  DEVICE_NONE = 0,
  DEVICE_JOYSTICK = 1,
  DEVICE_KEYBOARD = 2,
  DEVICE_MOUSE = 3
};

enum { MODE_NONE = 0, MODE_STANDARD = 1, MODE_CENTERING = 2, MODE_SMOOTH = 3 };

using JoyInfoRec_t = struct JoyInfoRec_t {
  int device;
  int mode;
};

inline auto clamp_val(int val, int low, int high) -> int {
  return (val < low) ? low : (val > high) ? high : val;
}

struct JoyCoord_t {
  int x;
  int y;
};

static const std::array<JoyInfoRec_t, 5> k_joy_info = {
    {{DEVICE_NONE, MODE_NONE},
     {DEVICE_JOYSTICK, MODE_STANDARD},
     {DEVICE_KEYBOARD, MODE_STANDARD},
     {DEVICE_KEYBOARD, MODE_CENTERING},
     {DEVICE_MOUSE, MODE_STANDARD}}};

// Key pad [1..9]; Key pad 0,Key pad '.'; Left ALT,Right ALT
enum JoyKey_t {
  JK_DOWNLEFT = 0,
  JK_DOWN,
  JK_DOWNRIGHT,
  JK_LEFT,
  JK_CENTRE,
  JK_RIGHT,
  JK_UPLEFT,
  JK_UP,
  JK_UPRIGHT,
  JK_BUTTON0,
  JK_BUTTON1,
  JK_OPENAPPLE,
  JK_CLOSEDAPPLE,
  JK_MAX
};

constexpr uint32_t k_pdl_central = 127;
constexpr uint32_t k_pdl_max = 255;

static std::array<bool, JK_MAX> g_key_down = {false};
constexpr int k_pdl_smax = 127;
constexpr int k_pdl_scentral = 0;
constexpr int k_pdl_smin = -127;

static const std::array<JoyCoord_t, 9> k_key_value = {
    {{k_pdl_smin, k_pdl_smax},
     {k_pdl_scentral, k_pdl_smax},
     {k_pdl_smax, k_pdl_smax},
     {k_pdl_smin, k_pdl_scentral},
     {k_pdl_scentral, k_pdl_scentral},
     {k_pdl_smax, k_pdl_scentral},
     {k_pdl_smin, k_pdl_smin},
     {k_pdl_scentral, k_pdl_smin},
     {k_pdl_smax, k_pdl_smin}}};

static std::array<int, 2> g_joy_shr_x = {8, 8};
static std::array<int, 2> g_joy_shr_y = {8, 8};
static std::array<int, 2> g_joy_sub_x = {0, 0};
static std::array<int, 2> g_joy_sub_y = {0, 0};

static SdlJoystickPtr_t g_joy1;
static SdlJoystickPtr_t g_joy2;

static int g_frontend_pdl_trim_x = 0;
static int g_frontend_pdl_trim_y = 0;

static JoystickConfig_t g_joy_config;

auto joy_frontend_initialize() -> void {
  constexpr int16_t k_axis_min = -32768; /* minimum value for axis coordinate */
  constexpr int16_t k_axis_max = 32767;  /* maximum value for axis coordinate */

  g_joy1.reset();
  g_joy2.reset();

  // Load config from registry
  g_joy_config = {};
  uint32_t val = 0;
  if (load(REGVALUE_JOY_TYPE1, &val)) {
    g_joy_config.joy_type[0] = (val < k_joy_info.size()) ? val : 0;
  }
  if (load(REGVALUE_JOY_TYPE2, &val)) {
    g_joy_config.joy_type[1] = (val < k_joy_info.size()) ? val : 0;
  }
  if (load(REGVALUE_JOY_INDEX1, &val)) g_joy_config.joy_index[0] = val;
  if (load(REGVALUE_JOY_INDEX2, &val)) g_joy_config.joy_index[1] = val;
  if (load(REGVALUE_JOY_BUTTON1_1, &val)) g_joy_config.joy0_button_map[0] = val;
  if (load(REGVALUE_JOY_BUTTON1_2, &val)) g_joy_config.joy0_button_map[1] = val;
  if (load(REGVALUE_JOY_BUTTON2_1, &val)) g_joy_config.joy1_button_map = val;
  if (load(REGVALUE_JOY_AXIS1_0, &val)) g_joy_config.joy_axis[0][0] = val;
  if (load(REGVALUE_JOY_AXIS1_1, &val)) g_joy_config.joy_axis[0][1] = val;
  if (load(REGVALUE_JOY_AXIS2_0, &val)) g_joy_config.joy_axis[1][0] = val;
  if (load(REGVALUE_JOY_AXIS2_1, &val)) g_joy_config.joy_axis[1][1] = val;
  if (load(REGVALUE_JOY_EXIT_ENABLE, &val)) g_joy_config.joy_exit_enable = val;
  if (load(REGVALUE_JOY_EXIT_BUTTON0, &val))
    g_joy_config.joy_exit_button[0] = val;
  if (load(REGVALUE_JOY_EXIT_BUTTON1, &val))
    g_joy_config.joy_exit_button[1] = val;

  // Sync to peripheral
  peripheral_command(0, JOY_CMD_SET_CONFIG, &g_joy_config,
                     sizeof(g_joy_config));

  int number_of_joysticks = sdl_compat_num_joysticks();

  if (k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[0])).device ==
      DEVICE_JOYSTICK) {
    if (number_of_joysticks > 0 &&
        static_cast<int>(g_joy_config.joy_index[0]) < number_of_joysticks) {
      g_joy1 =
          sdl_compat_open_joystick(static_cast<int>(g_joy_config.joy_index[0]));
      g_joy_shr_x.at(0) = 0;
      g_joy_shr_y.at(0) = 0;
      g_joy_sub_x.at(0) = k_axis_min;
      g_joy_sub_y.at(0) = k_axis_min;
      uint32_t xrange = k_axis_max - k_axis_min;
      uint32_t yrange = k_axis_max - k_axis_min;
      while (xrange > 256) {
        xrange >>= 1;
        ++g_joy_shr_x.at(0);
      }
      while (yrange > 256) {
        yrange >>= 1;
        ++g_joy_shr_y.at(0);
      }
    } else {
      g_joy_config.joy_type[0] = 4;
    }
  }

  if (k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1])).device ==
      DEVICE_JOYSTICK) {
    if (number_of_joysticks > 1 &&
        static_cast<int>(g_joy_config.joy_index[1]) < number_of_joysticks) {
      g_joy2 =
          sdl_compat_open_joystick(static_cast<int>(g_joy_config.joy_index[1]));
      g_joy_shr_x.at(1) = 0;
      g_joy_shr_y.at(1) = 0;
      g_joy_sub_x.at(1) = k_axis_min;
      g_joy_sub_y.at(1) = k_axis_min;
      uint32_t xrange = k_axis_max - k_axis_min;
      uint32_t yrange = k_axis_max - k_axis_min;
      while (xrange > 256) {
        xrange >>= 1;
        ++g_joy_shr_x.at(1);
      }
      while (yrange > 256) {
        yrange >>= 1;
        ++g_joy_shr_y.at(1);
      }
    } else {
      g_joy_config.joy_type[1] = DEVICE_NONE;
    }
  }
}

auto joy_frontend_shutdown() -> void {
  g_joy1.reset();
  g_joy2.reset();
}

auto joy_frontend_check_exit() -> void {
  if (!g_joy1 || !g_joy_config.joy_exit_enable) return;
  sdl_compat_update_joysticks();
  bool quit =
      sdl_compat_get_joystick_button(
          g_joy1.get(), static_cast<int>(g_joy_config.joy_exit_button[0])) &&
      sdl_compat_get_joystick_button(
          g_joy1.get(), static_cast<int>(g_joy_config.joy_exit_button[1]));

  if (quit) {
    system_state.mode = app_mode_exit;
  }
}

auto joy_frontend_update() -> void {
  // Joystick 0
  if (g_joy1 &&
      k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[0])).device ==
          DEVICE_JOYSTICK) {
    static uint32_t lastcheck = 0;
    uint32_t currtime = SDL_GetTicks();
    if (currtime - lastcheck >= 10) {
      lastcheck = currtime;
      sdl_compat_update_joysticks();

      bool b0 = sdl_compat_get_joystick_button(
          g_joy1.get(), static_cast<int>(g_joy_config.joy0_button_map[0]));
      bool b1 = false;
      if (k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1])).device ==
          DEVICE_NONE) {
        b1 = sdl_compat_get_joystick_button(
            g_joy1.get(), static_cast<int>(g_joy_config.joy0_button_map[1]));
      }

      JoystickButtonPayload_t pb0 = {0, b0, {0, 0}};
      peripheral_command(0, JOY_CMD_SET_BUTTON, &pb0, sizeof(pb0));
      JoystickButtonPayload_t pb1 = {1, b1, {0, 0}};
      peripheral_command(0, JOY_CMD_SET_BUTTON, &pb1, sizeof(pb1));

      int x =
          (static_cast<int>(sdl_compat_get_joystick_axis(
               g_joy1.get(), static_cast<int>(g_joy_config.joy_axis[0][0]))) -
           g_joy_sub_x.at(0)) >>
          g_joy_shr_x.at(0);
      int y =
          (static_cast<int>(sdl_compat_get_joystick_axis(
               g_joy1.get(), static_cast<int>(g_joy_config.joy_axis[0][1]))) -
           g_joy_sub_y.at(0)) >>
          g_joy_shr_y.at(0);

      // "Square" a modern analog stick
      if (y < static_cast<int>(k_pdl_central) / 2) {
        if (x < static_cast<int>(k_pdl_central) / 2) {
          x = x - (static_cast<int>(k_pdl_central) / 2 - y) / 2;
          y = y - (static_cast<int>(k_pdl_central) / 2 - x) / 2;
        } else if (x > static_cast<int>(k_pdl_central) +
                           static_cast<int>(k_pdl_central) / 2) {
          x = x + (static_cast<int>(k_pdl_central) / 2 - y) / 2;
          y = y - (x - (static_cast<int>(k_pdl_central) +
                        static_cast<int>(k_pdl_central) / 2)) /
                      2;
        }
      } else if (y > static_cast<int>(k_pdl_central) +
                         static_cast<int>(k_pdl_central) / 2) {
        if (x < static_cast<int>(k_pdl_central) / 2) {
          x = x - (y - (static_cast<int>(k_pdl_central) +
                        static_cast<int>(k_pdl_central) / 2)) /
                      2;
          y = y + (static_cast<int>(k_pdl_central) / 2 - x) / 2;
        } else if (x > static_cast<int>(k_pdl_central) +
                           static_cast<int>(k_pdl_central) / 2) {
          x = x + (y - (static_cast<int>(k_pdl_central) +
                        static_cast<int>(k_pdl_central) / 2)) /
                      2;
          y = y + (x - (static_cast<int>(k_pdl_central) +
                        static_cast<int>(k_pdl_central) / 2)) /
                      2;
        }
      }
      if (x < 0) x = 0;
      if (x > 255) x = 255;
      if (y < 0) y = 0;
      if (y > 255) y = 255;

      const auto clamped_x =
          static_cast<uint8_t>(clamp_val(x + g_frontend_pdl_trim_x, 0, 255));
      const auto clamped_y =
          static_cast<uint8_t>(clamp_val(y + g_frontend_pdl_trim_y, 0, 255));
      JoystickAxisPayload_t px = {0, 0, clamped_x, 0};
      peripheral_command(0, JOY_CMD_SET_AXIS, &px, sizeof(px));
      JoystickAxisPayload_t py = {0, 1, clamped_y, 0};
      peripheral_command(0, JOY_CMD_SET_AXIS, &py, sizeof(py));
    }
  }

  // Joystick 1
  if (g_joy2 &&
      k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1])).device ==
          DEVICE_JOYSTICK) {
    static uint32_t lastcheck = 0;
    uint32_t currtime = SDL_GetTicks();
    if (currtime - lastcheck >= 10) {
      lastcheck = currtime;
      sdl_compat_update_joysticks();

      bool b2 = sdl_compat_get_joystick_button(
          g_joy2.get(), static_cast<int>(g_joy_config.joy1_button_map));
      JoystickButtonPayload_t pb2 = {2, b2, {0, 0}};
      peripheral_command(0, JOY_CMD_SET_BUTTON, &pb2, sizeof(pb2));
      if (k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1])).device !=
          DEVICE_NONE) {
        JoystickButtonPayload_t pb1 = {1, b2, {0, 0}};
        peripheral_command(0, JOY_CMD_SET_BUTTON, &pb1, sizeof(pb1));
      }

      int x =
          (static_cast<int>(sdl_compat_get_joystick_axis(
               g_joy2.get(), static_cast<int>(g_joy_config.joy_axis[1][0]))) -
           g_joy_sub_x.at(1)) >>
          g_joy_shr_x.at(1);
      int y =
          (static_cast<int>(sdl_compat_get_joystick_axis(
               g_joy2.get(), static_cast<int>(g_joy_config.joy_axis[1][1]))) -
           g_joy_sub_y.at(1)) >>
          g_joy_shr_y.at(1);

      if (x == 127 || x == 128) {
        x += g_frontend_pdl_trim_x;
      }
      if (y == 127 || y == 128) {
        y += g_frontend_pdl_trim_y;
      }

      x = clamp_val(x, 0, 255);
      y = clamp_val(y, 0, 255);

      JoystickAxisPayload_t px = {1, 0, static_cast<uint8_t>(x), 0};
      peripheral_command(0, JOY_CMD_SET_AXIS, &px, sizeof(px));
      JoystickAxisPayload_t py = {1, 1, static_cast<uint8_t>(y), 0};
      peripheral_command(0, JOY_CMD_SET_AXIS, &py, sizeof(py));
    }
  }
}

auto joy_frontend_update_trim_via_key(SdlKeycode_t virtkey) -> void {
  switch (virtkey) {
    case SDLK_DOWN:
    case SDLK_KP_2:
      if (g_frontend_pdl_trim_y < 64) g_frontend_pdl_trim_y++;
      break;
    case SDLK_KP_4:
    case SDLK_LEFT:
      if (g_frontend_pdl_trim_x > -64) g_frontend_pdl_trim_x--;
      break;
    case SDLK_KP_6:
    case SDLK_RIGHT:
      if (g_frontend_pdl_trim_x < 64) g_frontend_pdl_trim_x++;
      break;
    case SDLK_KP_8:
    case SDLK_UP:
      if (g_frontend_pdl_trim_y > -64) g_frontend_pdl_trim_y--;
      break;
    case SDLK_KP_5:
    case SDLK_CLEAR:
      g_frontend_pdl_trim_x = g_frontend_pdl_trim_y = 0;
      break;
    default:
      break;
  }
}

auto joy_frontend_process_key(SdlKeycode_t virtkey, bool extended, bool down,
                              bool autorep) -> bool {
  int joy_num = -1;
  if (g_joy_config.joy_type[0] < k_joy_info.size() &&
      k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[0])).device ==
          DEVICE_KEYBOARD) {
    joy_num = 0;
  } else if (g_joy_config.joy_type[1] < k_joy_info.size() &&
             k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1]))
                     .device == DEVICE_KEYBOARD) {
    joy_num = 1;
  }
  if (joy_num == -1) {
    return false;
  }
  int centering_type =
      k_joy_info
          .at(static_cast<size_t>(
              g_joy_config.joy_type[static_cast<size_t>(joy_num)]))
          .mode;

  bool keychange = !extended;
  if (!extended) {
    if ((virtkey >= SDLK_KP_1) && (virtkey <= SDLK_KP_9)) {
      g_key_down.at(static_cast<size_t>(virtkey - SDLK_KP_1)) = down;
    } else {
      switch (virtkey) {
        case SDLK_END:
          g_key_down.at(0) = down;
          break;
        case SDLK_DOWN:
          g_key_down.at(1) = down;
          break;
        case SDLK_PAGEDOWN:
          g_key_down.at(2) = down;
          break;
        case SDLK_LEFT:
          g_key_down.at(3) = down;
          break;
        case SDLK_CLEAR:
          g_key_down.at(4) = down;
          break;
        case SDLK_RIGHT:
          g_key_down.at(5) = down;
          break;
        case SDLK_HOME:
          g_key_down.at(6) = down;
          break;
        case SDLK_UP:
          g_key_down.at(7) = down;
          break;
        case SDLK_PAGEUP:
          g_key_down.at(8) = down;
          break;
        case SDLK_KP_0:
        case SDLK_INSERT:
          g_key_down.at(9) = down;
          break;
        case SDLK_KP_PERIOD:
        case SDLK_DELETE:
          g_key_down.at(10) = down;
          break;
        default:
          keychange = false;
          break;
      }
    }
  }

  if (keychange) {
    if ((virtkey == SDLK_KP_0) || (virtkey == SDLK_INSERT)) {
      if (down) {
        if (k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1]))
                .device != DEVICE_KEYBOARD) {
          JoystickButtonPayload_t p = {0, true, {0, 0}};
          peripheral_command(0, JOY_CMD_SET_BUTTON, &p, sizeof(p));
        } else if (k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1]))
                       .device != DEVICE_NONE) {
          JoystickButtonPayload_t p2 = {2, true, {0, 0}};
          peripheral_command(0, JOY_CMD_SET_BUTTON, &p2, sizeof(p2));
          JoystickButtonPayload_t p1 = {1, true, {0, 0}};
          peripheral_command(0, JOY_CMD_SET_BUTTON, &p1, sizeof(p1));
        }
      } else {
        if (k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1]))
                .device != DEVICE_KEYBOARD) {
          JoystickButtonPayload_t p = {0, false, {0, 0}};
          peripheral_command(0, JOY_CMD_SET_BUTTON, &p, sizeof(p));
        } else if (k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1]))
                       .device != DEVICE_NONE) {
          JoystickButtonPayload_t p2 = {2, false, {0, 0}};
          peripheral_command(0, JOY_CMD_SET_BUTTON, &p2, sizeof(p2));
          JoystickButtonPayload_t p1 = {1, false, {0, 0}};
          peripheral_command(0, JOY_CMD_SET_BUTTON, &p1, sizeof(p1));
        }
      }
    } else if ((virtkey == SDLK_KP_PERIOD) || (virtkey == SDLK_DELETE)) {
      if (down) {
        if (k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1]))
                .device != DEVICE_KEYBOARD) {
          JoystickButtonPayload_t p = {1, true, {0, 0}};
          peripheral_command(0, JOY_CMD_SET_BUTTON, &p, sizeof(p));
        }
      } else {
        if (k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1]))
                .device != DEVICE_KEYBOARD) {
          JoystickButtonPayload_t p = {1, false, {0, 0}};
          peripheral_command(0, JOY_CMD_SET_BUTTON, &p, sizeof(p));
        }
      }
    } else if ((down && !autorep) || (centering_type == MODE_CENTERING)) {
      int xsum = 0;
      int ysum = 0;
      int keydown_count = 0;
      static constexpr std::array<int, 16> corner_convert_lookup = {
          {-1, -1, -1, 8, -1, 6, -1, -1, -1, -1, 2, -1, 0, -1, -1, -1}};
      int corner_idx = (static_cast<int>(0 == g_key_down.at(1))) |
                       (static_cast<int>(0 == g_key_down.at(3)) << 1) |
                       (static_cast<int>(0 == g_key_down.at(5)) << 2) |
                       (static_cast<int>(0 == g_key_down.at(7)) << 3);
      int corner_override_idx =
          corner_convert_lookup.at(static_cast<size_t>(corner_idx));
      if (corner_override_idx >= 0) {
        xsum = k_key_value.at(static_cast<size_t>(corner_override_idx)).x;
        ysum = k_key_value.at(static_cast<size_t>(corner_override_idx)).y;
        keydown_count = 1;
      } else {
        for (size_t i = 0; i < 9; i++) {
          if (g_key_down.at(i)) {
            keydown_count++;
            xsum += k_key_value.at(i).x;
            ysum += k_key_value.at(i).y;
          }
        }
      }
      int x = 0;
      int y = 0;
      if (keydown_count != 0) {
        x = (xsum / keydown_count) + static_cast<int>(k_pdl_central) +
            g_frontend_pdl_trim_x;
        y = (ysum / keydown_count) + static_cast<int>(k_pdl_central) +
            g_frontend_pdl_trim_y;
      } else {
        x = static_cast<int>(k_pdl_central) + g_frontend_pdl_trim_x;
        y = static_cast<int>(k_pdl_central) + g_frontend_pdl_trim_y;
      }
      x = clamp_val(x, 0, 255);
      y = clamp_val(y, 0, 255);
      JoystickAxisPayload_t px = {static_cast<uint8_t>(joy_num), 0,
                                  static_cast<uint8_t>(x), 0};
      peripheral_command(0, JOY_CMD_SET_AXIS, &px, sizeof(px));
      JoystickAxisPayload_t py = {static_cast<uint8_t>(joy_num), 1,
                                  static_cast<uint8_t>(y), 0};
      peripheral_command(0, JOY_CMD_SET_AXIS, &py, sizeof(py));
    }
  }
  return keychange;
}

auto joy_frontend_is_mouse_emulation_active() -> bool {
  if (g_joy_config.joy_type[0] == 4 ||
      (g_joy_config.joy_type[0] < k_joy_info.size() &&
       k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[0])).device ==
           DEVICE_MOUSE)) {
    return true;
  }
  if (g_joy_config.joy_type[1] == 4 ||
      (g_joy_config.joy_type[1] < k_joy_info.size() &&
       k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[1])).device ==
           DEVICE_MOUSE)) {
    return true;
  }
  return false;
}

auto joy_frontend_process_mouse_motion(int x, int max_x, int y, int max_y)
    -> void {
  int range_x = (max_x > 1) ? (max_x - 1) : 1;
  int range_y = (max_y > 1) ? (max_y - 1) : 1;
  int joy_x = ((x * 255) + (range_x / 2)) / range_x;
  int joy_y = ((y * 255) + (range_y / 2)) / range_y;
  if (joy_x < 0) {
    joy_x = 0;
  }
  if (joy_x > 255) {
    joy_x = 255;
  }
  if (joy_y < 0) {
    joy_y = 0;
  }
  if (joy_y > 255) {
    joy_y = 255;
  }

  for (uint8_t joy_num = 0; joy_num < 2; ++joy_num) {
    if (g_joy_config.joy_type[joy_num] == 4 ||
        (g_joy_config.joy_type[joy_num] < k_joy_info.size() &&
         k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[joy_num]))
                 .device == DEVICE_MOUSE)) {
      JoystickAxisPayload_t px = {joy_num, 0, static_cast<uint8_t>(joy_x), 0};
      peripheral_command(0, JOY_CMD_SET_AXIS, &px, sizeof(px));
      JoystickAxisPayload_t py = {joy_num, 1, static_cast<uint8_t>(joy_y), 0};
      peripheral_command(0, JOY_CMD_SET_AXIS, &py, sizeof(py));
    }
  }
}

auto joy_frontend_process_mouse_button(int button, bool down) -> void {
  for (uint8_t joy_num = 0; joy_num < 2; ++joy_num) {
    if (g_joy_config.joy_type[joy_num] == 4 ||
        (g_joy_config.joy_type[joy_num] < k_joy_info.size() &&
         k_joy_info.at(static_cast<size_t>(g_joy_config.joy_type[joy_num]))
                 .device == DEVICE_MOUSE)) {
      if (button == 0) {
        JoystickButtonPayload_t pb0 = {0, down, {0, 0}};
        peripheral_command(0, JOY_CMD_SET_BUTTON, &pb0, sizeof(pb0));
      } else if (button == 1) {
        JoystickButtonPayload_t pb1 = {1, down, {0, 0}};
        peripheral_command(0, JOY_CMD_SET_BUTTON, &pb1, sizeof(pb1));
      }
    }
  }
}
