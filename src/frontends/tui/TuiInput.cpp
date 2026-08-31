#include "TuiInput.h"

#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Debugger/Debug.h"
#include "TuiDiskSelect.h"
#include "TuiVideo.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
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
static constexpr int f3_vt_code = 13;
static constexpr int f4_vt_code = 14;
static constexpr int f5_vt_code = 15;
static constexpr int f6_vt_code = 17;
static constexpr int f7_vt_code = 18;
static constexpr int f8_vt_code = 19;
static constexpr int f9_vt_code = 20;
static constexpr int f10_vt_code = 21;
static constexpr int f12_code = 24;
static constexpr int disk_select_page_size = 14;

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

static auto soft_reset_machine() -> void {
  if (!IS_APPLE2()) {
    mem_reset_paging();
  }
  peripheral_manager_reset();
  if (!IS_APPLE2()) {
    video_reset_state();
  }
  cpu_reset();
  g_state.mode = MODE_RUNNING;
  g_state.reset_timing = true;
}

static auto restart_machine() -> void { AppController_SetRestart(true); }

static auto swap_drives() -> void {
  peripheral_command(disk_default_slot, disk_cmd_swap_drives, nullptr, 0);
}

static auto toggle_keyboard_rocker() -> void {
  if ((g_apple2_type == A2TYPE_APPLE2E) ||
      (g_apple2_type == A2TYPE_APPLE2EENHANCED)) {
    uint8_t cur_rocker = 0;
    size_t rocker_sz = sizeof(cur_rocker);
    peripheral_query(0, keyboard_query_rocker, &cur_rocker, &rocker_sz);
    uint8_t new_rocker = (cur_rocker != 0) ? 0 : 1;
    peripheral_command(0, keyboard_cmd_set_rocker, &new_rocker, 1);
  }
}

static auto toggle_debugger() -> void {
  if (g_state.disable_debugger) {
    return;
  }
  if (g_state.mode != MODE_DEBUG) {
    debug_begin();
  } else {
    debug_end();
  }
}

static auto save_configuration() -> void {
  Configuration_t::instance().set_int("Configuration", "Video Emulation",
                                      static_cast<int>(g_videotype));
  Configuration_t::instance().set_int("Configuration", "Emulation Speed",
                                      g_state.speed);
  Configuration_t::instance().set_int("Configuration", "Fullscreen",
                                      g_state.fullscreen ? 1 : 0);
  Configuration_t::instance().save();
}

static auto cycle_video_mode() -> void {
  g_videotype++;
  if (g_videotype >= VT_NUM_MODES) {
    g_videotype = 0;
  }
  video_reinitialize();
  if (g_state.mode != MODE_LOGO) {
    if (g_state.mode == MODE_DEBUG) {
#if ENABLE_DEBUGGER
      uint32_t debug_video_mode = 0;
      if (debug_get_video_mode(&debug_video_mode)) {
        video_refresh_screen();
      }
#endif
    } else {
      video_refresh_screen();
    }
  }
}

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
        struct pollfd pfd{};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, 25);
        if (pr > 0 && (pfd.revents & POLLIN) != 0) {
          std::array<uint8_t, INPUT_BUFFER_SIZE> extra_buf{};
          ssize_t extra_n =
              read(STDIN_FILENO, extra_buf.data(), extra_buf.size());
          if (extra_n > 0) {
            for (ssize_t j = 0; j < extra_n; ++j) {
              g_input_queue.push_back(extra_buf.at(static_cast<size_t>(j)));
            }
          }
        }
      }

      if (i + 1 >= g_input_queue.size()) {
        if (tui_disk_select_is_active()) {
          tui_disk_select_close();
        } else if (tui_video_is_help_visible()) {
          tui_video_close_help();
        } else {
          map_key(a2_key_esc);
        }
        i++;
        continue;
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
        } else if (ss3_cmd == 'R') {  // F3
          tui_video_close_help();
          tui_disk_select_open(6, 0);
        } else if (ss3_cmd == 'S') {  // F4
          tui_video_close_help();
          tui_disk_select_open(6, 1);
        } else if (ss3_cmd == 'T') {  // F5
          swap_drives();
        } else if (ss3_cmd == 'U') {  // F6
          tui_video_toggle_fullscreen();
        } else if (ss3_cmd == 'V') {  // F7
          toggle_debugger();
        } else if (ss3_cmd == 'W') {  // F8
          tui_video_save_screenshot();
        } else if (ss3_cmd == 'X') {  // F9
          cycle_video_mode();
        } else if (ss3_cmd == 'H') {  // Home
          if (tui_disk_select_is_active()) tui_disk_select_home();
        } else if (ss3_cmd == 'F') {  // End
          if (tui_disk_select_is_active())
            tui_disk_select_end(disk_select_page_size);
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
            } else if (g_input_queue.at(i + 3) == 'C') {  // Linux Console F3
              tui_video_close_help();
              tui_disk_select_open(6, 0);
            } else if (g_input_queue.at(i + 3) == 'D') {  // Linux Console F4
              tui_video_close_help();
              tui_disk_select_open(6, 1);
            } else if (g_input_queue.at(i + 3) == 'E') {  // Linux Console F5
              swap_drives();
            } else if (g_input_queue.at(i + 3) == 'F') {  // Linux Console F6
              tui_video_toggle_fullscreen();
            } else if (g_input_queue.at(i + 3) == 'G') {  // Linux Console F7
              toggle_debugger();
            } else if (g_input_queue.at(i + 3) == 'H') {  // Linux Console F8
              tui_video_save_screenshot();
            } else if (g_input_queue.at(i + 3) == 'I') {  // Linux Console F9
              cycle_video_mode();
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
          } else if (cmd == 'R') {  // xterm F3 / Shift+F3 (\x1b[R / \x1b[1;2R)
            tui_video_close_help();
            const std::string token(
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
            if (token.find(";2") != std::string::npos || token == "1;2") {
              tui_disk_select_open(7, 0);
            } else {
              tui_disk_select_open(6, 0);
            }
          } else if (cmd == 'S') {  // xterm F4 / Shift+F4 (\x1b[S / \x1b[1;2S)
            tui_video_close_help();
            const std::string token(
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
            if (token.find(";2") != std::string::npos || token == "1;2") {
              tui_disk_select_open(7, 1);
            } else {
              tui_disk_select_open(6, 1);
            }
          } else if (cmd == 'W') {  // xterm F8 / Shift+F8 (\x1b[W / \x1b[1;2W)
            const std::string token(
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
            if (token.find(";2") != std::string::npos || token == "1;2") {
              save_configuration();
            } else {
              tui_video_save_screenshot();
            }
          } else if (cmd == 'H') {  // Home (\x1b[H)
            if (tui_disk_select_is_active()) tui_disk_select_home();
          } else if (cmd == 'F') {  // End (\x1b[F)
            if (tui_disk_select_is_active())
              tui_disk_select_end(disk_select_page_size);
          } else if (cmd == '^') {  // rxvt Ctrl modifier
            const std::string token(
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
            if (token == "12") {  // rxvt Ctrl+F2 (\x1b[12^)
              reset_machine();
            } else if (token == "21") {  // rxvt Ctrl+F10 (\x1b[21^)
              soft_reset_machine();
            }
          } else if (cmd == '$' || cmd == '@') {  // rxvt Shift modifier
            const std::string token(
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
            if (token == "12") {  // rxvt Shift+F2 (\x1b[12$)
              restart_machine();
            } else if (token == "13" || token == "25") {
              tui_video_close_help();
              tui_disk_select_open(7, 0);
            } else if (token == "14" || token == "26") {
              tui_video_close_help();
              tui_disk_select_open(7, 1);
            } else if (token == "17" || token == "28") {
              toggle_keyboard_rocker();
            } else if (token == "19" || token == "32") {
              save_configuration();
            }
          } else if (cmd == '~') {
            if (end > i + 2) {
              const std::string token(
                  g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                  g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
              if (token == "12;2") {  // VT Shift+F2 (\x1b[12;2~)
                restart_machine();
              } else if (token == "13;2" || token == "25" || token == "25;2") {
                tui_video_close_help();
                tui_disk_select_open(7, 0);
              } else if (token == "14;2" || token == "26" || token == "26;2") {
                tui_video_close_help();
                tui_disk_select_open(7, 1);
              } else if (token == "17;2" || token == "28" || token == "28;2") {
                toggle_keyboard_rocker();
              } else if (token == "19;2" || token == "32" || token == "32;2") {
                save_configuration();
              } else if (token == "21;5" ||
                         token ==
                             "21") {  // F10 / Ctrl+F10 (\x1b[21;5~ / \x1b[21~)
                soft_reset_machine();
              } else if (token == "5") {  // Page Up (\x1b[5~)
                if (tui_disk_select_is_active())
                  tui_disk_select_page(-1, disk_select_page_size);
              } else if (token == "6") {  // Page Down (\x1b[6~)
                if (tui_disk_select_is_active())
                  tui_disk_select_page(1, disk_select_page_size);
              } else if (token == "1" || token == "7") {  // Home (\x1b[1~)
                if (tui_disk_select_is_active()) tui_disk_select_home();
              } else if (token == "4" || token == "8") {  // End (\x1b[4~)
                if (tui_disk_select_is_active())
                  tui_disk_select_end(disk_select_page_size);
              } else {
                try {
                  int val = std::stoi(token);
                  if (val == f1_vt_code) {
                    tui_video_toggle_help();
                  } else if (val == f2_vt_code) {
                    reset_machine();
                  } else if (val == f3_vt_code) {
                    tui_video_close_help();
                    tui_disk_select_open(6, 0);
                  } else if (val == f4_vt_code) {
                    tui_video_close_help();
                    tui_disk_select_open(6, 1);
                  } else if (val == f5_vt_code) {
                    swap_drives();
                  } else if (val == f6_vt_code) {
                    tui_video_toggle_fullscreen();
                  } else if (val == f7_vt_code) {
                    toggle_debugger();
                  } else if (val == f8_vt_code) {
                    tui_video_save_screenshot();
                  } else if (val == f9_vt_code) {
                    cycle_video_mode();
                  } else if (val == f10_vt_code) {
                    soft_reset_machine();
                  } else if (val == f12_code) {
                    raise(SIGINT);
                  } else if (tui_video_is_help_visible()) {
                    tui_video_close_help();
                  }
                } catch (...) {
                }
              }
            }
          } else if (cmd == 'A') {
            if (tui_disk_select_is_active()) {
              tui_disk_select_move(-1, disk_select_page_size);
            } else if (tui_video_is_help_visible()) {
              tui_video_close_help();
            } else if (g_state.mode == MODE_DEBUG) {
              debugger_process_key(LINAPPLE_KEY_UP);
            } else {
              map_key(a2_key_up);
            }
          } else if (cmd == 'B') {
            if (tui_disk_select_is_active()) {
              tui_disk_select_move(1, disk_select_page_size);
            } else if (tui_video_is_help_visible()) {
              tui_video_close_help();
            } else if (g_state.mode == MODE_DEBUG) {
              debugger_process_key(LINAPPLE_KEY_DOWN);
            } else {
              map_key(a2_key_down);
            }
          } else if (cmd == 'D') {
            if (tui_disk_select_is_active()) {
              tui_disk_select_move(-1, disk_select_page_size);
            } else if (tui_video_is_help_visible()) {
              tui_video_close_help();
            } else if (g_state.mode == MODE_DEBUG) {
              debugger_process_key(LINAPPLE_KEY_LEFT);
            } else {
              map_key(a2_key_left);
            }
          } else if (cmd == 'C') {
            if (tui_disk_select_is_active()) {
              tui_disk_select_move(1, disk_select_page_size);
            } else if (tui_video_is_help_visible()) {
              tui_video_close_help();
            } else if (g_state.mode == MODE_DEBUG) {
              debugger_process_key(LINAPPLE_KEY_RIGHT);
            } else {
              map_key(a2_key_right);
            }
          }

          i = end + 1;
          continue;
        }

        break;
      }

      if (tui_disk_select_is_active()) {
        tui_disk_select_close();
      } else if (tui_video_is_help_visible()) {
        tui_video_close_help();
      } else if (g_state.mode == MODE_DEBUG) {
        debugger_process_key(LINAPPLE_KEY_ESCAPE);
      } else {
        map_key(a2_key_esc);
      }
      i++;
      continue;
    }

    uint8_t b = g_input_queue.at(i);
    if (b == a2_key_ctrl_c) {
      raise(SIGINT);
    } else if (tui_disk_select_is_active()) {
      if (b == a2_key_enter || b == '\n') {
        tui_disk_select_confirm();
      } else if (b == a2_key_esc) {
        tui_disk_select_close();
      } else if (b >= ASCII_PRINTABLE_MIN && b < ASCII_PRINTABLE_MAX) {
        tui_disk_select_jump_char(static_cast<char>(b), disk_select_page_size);
      }
    } else if (tui_video_is_help_visible()) {
      tui_video_close_help();
    } else if (g_state.mode == MODE_DEBUG) {
      if (b == a2_key_enter || b == '\n') {
        debugger_process_key(LINAPPLE_KEY_RETURN);
      } else if (b == a2_key_backspace || b == a2_key_delete) {
        debugger_process_key(LINAPPLE_KEY_BACKSPACE);
      } else if (b == a2_key_esc) {
        debugger_process_key(LINAPPLE_KEY_ESCAPE);
      } else if (b >= ASCII_PRINTABLE_MIN && b < ASCII_PRINTABLE_MAX) {
        debugger_process_key(static_cast<int>(b));
      }
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
