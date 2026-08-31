#include "TuiInput.h"

#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "TuiVideo.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/LinAppleCore.h"
#include "frontends/common/AppController.h"

static int g_joy_fd = -1;
static std::vector<uint8_t> g_input_queue;

static constexpr uint8_t a2_key_up = 0x0B;
static constexpr uint8_t a2_key_down = 0x0A;
static constexpr uint8_t a2_key_left = 0x08;
static constexpr uint8_t a2_key_right = 0x15;
static constexpr uint8_t a2_key_esc = 0x1B;
static constexpr uint8_t a2_key_enter = 0x0D;
static constexpr uint8_t a2_key_backspace = 0x08;
static constexpr uint8_t a2_key_delete = 0x7F;
static constexpr uint8_t a2_key_ctrl_c = 0x03;

static constexpr int f1_vt_code = 11;
static constexpr int f2_vt_code = 12;
static constexpr int f12_code = 24;

auto tui_input_initialize() -> void {
  // Enable Mouse Tracking (Any Event + SGR)
  printf("\x1b[?1003h\x1b[?1006h");
  fflush(stdout);
  g_joy_fd = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
}

auto tui_input_shutdown() -> void {
  // Disable Mouse Tracking
  printf("\x1b[?1006l\x1b[?1003l");
  fflush(stdout);
  if (g_joy_fd != -1) {
    close(g_joy_fd);
    g_joy_fd = -1;
  }
}

static auto map_key(uint8_t a2_code) -> void {
  linapple_set_key_state(a2_code, true);
  linapple_set_key_state(a2_code, false);
}

static auto reset_machine() -> void {
  g_full_speed = false;
  mem_reset();
  peripheral_manager_reset();
  peripheral_command(disk_default_slot, disk_cmd_boot, nullptr, 0);
  video_reset_state();
  peripheral_command(0, JOY_CMD_RESET, nullptr, 0);
  g_state.mode = MODE_RUNNING;
  g_state.reset_timing = true;
}

static auto restart_machine() -> void { AppController_SetRestart(true); }

constexpr uint8_t ANSI_FINAL_BYTE_MIN = 0x40;
constexpr uint8_t ANSI_FINAL_BYTE_MAX = 0x7E;
constexpr uint8_t ASCII_PRINTABLE_MIN = 32;
constexpr uint8_t ASCII_PRINTABLE_MAX = 127;
constexpr size_t INPUT_BUFFER_SIZE = 256;

static auto process_sequences() -> void {
  size_t i = 0;
  while (i < g_input_queue.size()) {
    if (g_input_queue.at(i) == a2_key_esc) {
      if (i + 1 >= g_input_queue.size()) {
        break;
      }

      if (g_input_queue.at(i + 1) == 'O') {
        if (i + 2 >= g_input_queue.size()) {
          break;
        }
        uint8_t ss3_cmd = g_input_queue.at(i + 2);
        if (ss3_cmd == 'P') {  // F1
          tui_video_toggle_help();
        } else if (ss3_cmd == 'Q') {  // F2
          reset_machine();
        } else if (tui_video_is_help_visible()) {
          tui_video_close_help();
        }
        i += 3;
        continue;
      }

      if (g_input_queue.at(i + 1) == '[') {
        if (i + 2 < g_input_queue.size() && g_input_queue.at(i + 2) == '[') {
          if (i + 3 < g_input_queue.size()) {
            if (g_input_queue.at(i + 3) == 'A') {  // Linux Console F1
              tui_video_toggle_help();
            } else if (g_input_queue.at(i + 3) == 'B') {  // Linux Console F2
              reset_machine();
            } else if (tui_video_is_help_visible()) {
              tui_video_close_help();
            }
            i += 4;
            continue;
          }
          break;
        }

        size_t end = i + 2;
        while (end < g_input_queue.size() &&
               (g_input_queue.at(end) < ANSI_FINAL_BYTE_MIN ||
                g_input_queue.at(end) > ANSI_FINAL_BYTE_MAX)) {
          end++;
        }

        if (end < g_input_queue.size()) {
          uint8_t cmd = g_input_queue.at(end);

          if (g_input_queue.at(i + 2) == '<') {
            // Consume mouse, no action yet
          } else if (cmd == 'P') {  // xterm F1 (\x1b[P)
            tui_video_toggle_help();
          } else if (cmd == 'Q') {  // xterm F2 / Shift+F2 / Ctrl+F2
            const std::string token(
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
            if (token.find(";2") != std::string::npos || token == "1;2") {
              restart_machine();
            } else {
              reset_machine();
            }
          } else if (cmd == '^') {  // rxvt Ctrl modifier
            const std::string token(
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
            if (token == "12") {  // rxvt Ctrl+F2 (\x1b[12^)
              reset_machine();
            }
          } else if (cmd == '$' || cmd == '@') {  // rxvt Shift modifier
            const std::string token(
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
            if (token == "12") {  // rxvt Shift+F2 (\x1b[12$)
              restart_machine();
            }
          } else if (cmd == '~') {
            if (end > i + 2) {
              const std::string token(
                  g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                  g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
              if (token == "12;2") {  // VT Shift+F2 (\x1b[12;2~)
                restart_machine();
              } else {
                try {
                  int val = std::stoi(token);
                  if (val == f1_vt_code) {
                    tui_video_toggle_help();
                  } else if (val == f2_vt_code) {
                    reset_machine();
                  } else if (val == f12_code) {
                    raise(SIGINT);
                  } else if (tui_video_is_help_visible()) {
                    tui_video_close_help();
                  }
                } catch (...) {
                }
              }
            }
          } else if (tui_video_is_help_visible()) {
            tui_video_close_help();
          } else if (cmd == 'A') {
            map_key(a2_key_up);
          } else if (cmd == 'B') {
            map_key(a2_key_down);
          } else if (cmd == 'D') {
            map_key(a2_key_left);
          } else if (cmd == 'C') {
            map_key(a2_key_right);
          }

          i = end + 1;
          continue;
        }

        break;
      }

      if (tui_video_is_help_visible()) {
        tui_video_close_help();
      } else {
        map_key(a2_key_esc);
      }
      i++;
      continue;
    }

    uint8_t b = g_input_queue.at(i);
    if (b == a2_key_ctrl_c) {
      raise(SIGINT);
    } else if (tui_video_is_help_visible()) {
      tui_video_close_help();
    } else if (b >= ASCII_PRINTABLE_MIN && b < ASCII_PRINTABLE_MAX) {
      map_key(b);
    } else if (b == a2_key_enter) {
      map_key(a2_key_enter);
    } else if (b == a2_key_backspace || b == a2_key_delete) {
      map_key(a2_key_backspace);
    }

    i++;
  }
  g_input_queue.erase(g_input_queue.begin(),
                      g_input_queue.begin() + static_cast<std::ptrdiff_t>(i));
}

auto tui_input_poll() -> void {
  std::array<uint8_t, INPUT_BUFFER_SIZE> buf{};
  ssize_t n = read(STDIN_FILENO, buf.data(), buf.size());
  if (n > 0) {
    for (ssize_t j = 0; j < n; ++j) {
      g_input_queue.push_back(buf.at(static_cast<size_t>(j)));
    }
    process_sequences();
  }

  if (g_joy_fd != -1) {
    struct js_event js{};
    while (read(g_joy_fd, &js, sizeof(js)) > 0) {
      if ((js.type & JS_EVENT_AXIS) != 0) {
        linapple_set_joystick_axis(js.number, js.value);
      } else if ((js.type & JS_EVENT_BUTTON) != 0) {
        linapple_set_joystick_button(js.number, js.value);
      }
    }
  }
}
