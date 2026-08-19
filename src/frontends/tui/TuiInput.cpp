#include "TuiInput.h"

#include <fcntl.h>
#include <linux/joystick.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstdio>
#include <string>
#include <vector>

#include "TuiTerminal.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

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

static auto process_sequences() -> void {
  size_t i = 0;
  while (i < g_input_queue.size()) {
    if (g_input_queue.at(i) == a2_key_esc) {
      if (i + 1 >= g_input_queue.size()) {
        break;
      }

      if (g_input_queue.at(i + 1) == '[') {
        size_t end = i + 2;
        while (end < g_input_queue.size() &&
               (g_input_queue.at(end) < 0x40 || g_input_queue.at(end) > 0x7E)) {
          end++;
        }

        if (end < g_input_queue.size()) {
          uint8_t cmd = g_input_queue.at(end);

          if (g_input_queue.at(i + 2) == '<') {
            // Consume mouse, no action yet
          } else if (cmd == 'A') {
            map_key(a2_key_up);
          } else if (cmd == 'B') {
            map_key(a2_key_down);
          } else if (cmd == 'D') {
            map_key(a2_key_left);
          } else if (cmd == 'C') {
            map_key(a2_key_right);
          } else if (cmd == '~') {
            if (end > i + 2) {
              const std::string token(
                  g_input_queue.begin() + static_cast<long>(i + 2),
                  g_input_queue.begin() + static_cast<long>(end));
              try {
                int val = std::stoi(token);
                if (val == f12_code) {
                  raise(SIGINT);
                }
              } catch (...) {
              }
            }
          }

          i = end + 1;
          continue;
        }

        break;
      }

      map_key(a2_key_esc);
      i++;
      continue;
    }

    uint8_t b = g_input_queue.at(i);
    if (b >= 32 && b < 127) {
      map_key(b);
    } else if (b == a2_key_enter) {
      map_key(a2_key_enter);
    } else if (b == a2_key_backspace || b == a2_key_delete) {
      map_key(a2_key_backspace);
    } else if (b == a2_key_ctrl_c) {
      raise(SIGINT);
    }

    i++;
  }
  g_input_queue.erase(g_input_queue.begin(), g_input_queue.begin() + static_cast<long>(i));
}

auto tui_input_poll() -> void {
  std::array<uint8_t, 256> buf{};
  ssize_t n = read(STDIN_FILENO, buf.data(), buf.size());
  if (n > 0) {
    for (ssize_t j = 0; j < n; ++j) {
      g_input_queue.push_back(buf.at(static_cast<size_t>(j)));
    }
    process_sequences();
  }

  if (g_joy_fd != -1) {
    struct js_event js {};
    while (read(g_joy_fd, &js, sizeof(js)) > 0) {
      if ((js.type & JS_EVENT_AXIS) != 0) {
        linapple_set_joystick_axis(js.number, js.value);
      } else if ((js.type & JS_EVENT_BUTTON) != 0) {
        linapple_set_joystick_button(js.number, js.value);
      }
    }
  }
}
