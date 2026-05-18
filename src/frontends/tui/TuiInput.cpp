#include "TuiInput.h"

#include <fcntl.h>
#include <linux/joystick.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <vector>

#include "TuiTerminal.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"

static int g_joy_fd = -1;
static std::vector<uint8_t> g_input_queue;

static constexpr uint8_t A2_KEY_UP = 0x0B;
static constexpr uint8_t A2_KEY_DOWN = 0x0A;
static constexpr uint8_t A2_KEY_LEFT = 0x08;
static constexpr uint8_t A2_KEY_RIGHT = 0x15;
static constexpr uint8_t A2_KEY_ESC = 0x1B;
static constexpr uint8_t A2_KEY_ENTER = 0x0D;
static constexpr uint8_t A2_KEY_BACKSPACE = 0x08;
static constexpr uint8_t A2_KEY_DELETE = 0x7F;
static constexpr uint8_t A2_KEY_CTRL_C = 0x03;

static constexpr int F12_CODE = 24;

auto TuiInput_Initialize() -> void {
  // Enable Mouse Tracking (Any Event + SGR)
  printf("\x1b[?1003h\x1b[?1006h");
  fflush(stdout);
  g_joy_fd = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
}

auto TuiInput_Shutdown() -> void {
  // Disable Mouse Tracking
  printf("\x1b[?1006l\x1b[?1003l");
  fflush(stdout);
  if (g_joy_fd != -1) {
    close(g_joy_fd);
    g_joy_fd = -1;
  }
}

static auto MapKey(uint8_t a2_code) -> void {
  Linapple_SetKeyState(a2_code, true);
  Linapple_SetKeyState(a2_code, false);
}

static auto ProcessSequences() -> void {
  size_t i = 0;
  while (i < g_input_queue.size()) {
    if (g_input_queue.at(i) == A2_KEY_ESC) {
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
            MapKey(A2_KEY_UP);
          } else if (cmd == 'B') {
            MapKey(A2_KEY_DOWN);
          } else if (cmd == 'D') {
            MapKey(A2_KEY_LEFT);
          } else if (cmd == 'C') {
            MapKey(A2_KEY_RIGHT);
          } else if (cmd == '~') {
            if (end > i + 2) {
              size_t len = end - (i + 2);
              std::vector<char> buf(len + 1, 0);
              memcpy(buf.data(), &g_input_queue.at(i + 2), len);
              try {
                int val = std::stoi(buf.data());
                if (val == F12_CODE) {
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

      MapKey(A2_KEY_ESC);
      i++;
      continue;
    }

    uint8_t b = g_input_queue.at(i);
    if (b >= 32 && b < 127) {
      MapKey(b);
    } else if (b == A2_KEY_ENTER) {
      MapKey(A2_KEY_ENTER);
    } else if (b == A2_KEY_BACKSPACE || b == A2_KEY_DELETE) {
      MapKey(A2_KEY_BACKSPACE);
    } else if (b == A2_KEY_CTRL_C) {
      raise(SIGINT);
    }

    i++;
  }
  g_input_queue.erase(g_input_queue.begin(), g_input_queue.begin() + static_cast<long>(i));
}

auto TuiInput_Poll() -> void {
  std::array<uint8_t, 256> buf{};
  ssize_t n = read(STDIN_FILENO, buf.data(), buf.size());
  if (n > 0) {
    for (ssize_t j = 0; j < n; ++j) {
      g_input_queue.push_back(buf.at(static_cast<size_t>(j)));
    }
    ProcessSequences();
  }

  if (g_joy_fd != -1) {
    struct js_event js {};
    while (read(g_joy_fd, &js, sizeof(js)) > 0) {
      if ((js.type & JS_EVENT_AXIS) != 0) {
        Linapple_SetJoystickAxis(js.number, js.value);
      } else if ((js.type & JS_EVENT_BUTTON) != 0) {
        Linapple_SetJoystickButton(js.number, js.value);
      }
    }
  }
}
