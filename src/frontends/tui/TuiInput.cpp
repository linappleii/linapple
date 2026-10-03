// SPDX-License-Identifier: GPL-2.0-only
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

#if ENABLE_DEBUGGER
#include "Debugger/Debug.h"
#endif
#include "TuiDiskSelect.h"
#include "TuiVideo.h"
#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/SaveStateManager.h"

namespace {

static int g_joy_fd = -1;
static std::vector<uint8_t> g_input_queue;

static constexpr uint8_t k_a2_key_up = 0x0B;
static constexpr uint8_t k_a2_key_down = 0x0A;
static constexpr uint8_t k_a2_key_left = 0x08;
static constexpr uint8_t k_a2_key_right = 0x15;
static constexpr uint8_t k_a2_key_esc = 0x1B;
static constexpr uint8_t k_a2_key_enter = 0x0D;
static constexpr uint8_t k_a2_key_backspace = 0x08;
static constexpr uint8_t k_a2_key_delete = 0x7F;
static constexpr uint8_t k_a2_key_ctrl_c = 0x03;

static constexpr int k_f1_vt_code = 11;
static constexpr int k_f2_vt_code = 12;
static constexpr int k_f3_vt_code = 13;
static constexpr int k_f4_vt_code = 14;
static constexpr int k_f5_vt_code = 15;
static constexpr int k_f6_vt_code = 17;
static constexpr int k_f7_vt_code = 18;
static constexpr int k_f8_vt_code = 19;
static constexpr int k_f9_vt_code = 20;
static constexpr int k_f10_vt_code = 21;
static constexpr int k_f11_vt_code = 23;
static constexpr int k_f12_code = 24;
static constexpr size_t k_disk_select_page_size = 14;

static constexpr uint8_t k_ansi_final_byte_min = 0x40;
static constexpr uint8_t k_ansi_final_byte_max = 0x7E;
static constexpr uint8_t k_ascii_printable_min = 32;
static constexpr uint8_t k_ascii_printable_max = 127;
static constexpr size_t k_input_buffer_size = 256;
static constexpr size_t k_max_escape_length = 32;
static constexpr int k_esc_poll_timeout_ms = 3;

static auto map_key(uint8_t a2_code) -> void {
  linapple_set_key_state(a2_code, true);
  linapple_set_key_state(a2_code, false);
}

static auto reset_machine() -> void {
  full_speed = false;
  linapple_reset_hard();
  system_state.mode = app_mode_running;
  system_state.reset_timing = true;
}

static auto soft_reset_machine() -> void {
  linapple_reset_soft();
  system_state.mode = app_mode_running;
  system_state.reset_timing = true;
}

static auto restart_machine() -> void { app_controller_set_restart(true); }

static auto swap_drives() -> void {
  if (peripheral_command(disk_default_slot, disk_cmd_swap_drives, nullptr, 0) ==
      peripheral_ok) {
    app_controller_save_disk_config(0);
    app_controller_save_disk_config(1);
  }
}

static auto toggle_keyboard_rocker() -> void {
  if ((current_apple2_type == A2TYPE_APPLE2E) ||
      (current_apple2_type == A2TYPE_APPLE2EENHANCED)) {
    uint8_t cur_rocker = 0;
    size_t rocker_sz = sizeof(cur_rocker);
    peripheral_query_by_id(0, "linapple.keyboard", keyboard_query_rocker,
                           &cur_rocker, &rocker_sz);
    uint8_t new_rocker = (cur_rocker != 0) ? 0 : 1;
    peripheral_command_by_id(0, "linapple.keyboard", keyboard_cmd_set_rocker,
                             &new_rocker, 1);
  }
}

static auto toggle_debugger() -> void {
#if ENABLE_DEBUGGER
  if (system_state.disable_debugger) {
    return;
  }
  if (system_state.mode != app_mode_debug) {
    debug_begin();
  } else {
    debug_end();
  }
#endif
}

static auto save_configuration() -> void {
  Configuration_t::instance().set_int("Configuration", "Video Emulation",
                                      static_cast<int>(g_videotype));
  Configuration_t::instance().set_int("Configuration", "Emulation Speed",
                                      system_state.speed);
  Configuration_t::instance().set_int("Configuration", "Fullscreen",
                                      system_state.fullscreen ? 1 : 0);
  Configuration_t::instance().save();
}

static auto cycle_video_mode() -> void {
  g_videotype++;
  if (g_videotype >= VT_NUM_MODES) {
    g_videotype = 0;
  }
  video_reinitialize();
  if (system_state.mode != app_mode_logo) {
    if (system_state.mode == app_mode_debug) {
#if ENABLE_DEBUGGER
      uint32_t debug_video_mode = 0;
      if (debug_get_video_mode(&debug_video_mode)) {
        video_redraw_screen();
      }
#endif
    } else {
      video_redraw_screen();
    }
  }
}

static auto toggle_pause() -> void {
  switch (system_state.mode) {
    case app_mode_running:
      system_state.mode = app_mode_paused;
      audio_mixer_set_fade(fade_out);
      break;
    case app_mode_paused:
      system_state.mode = app_mode_running;
      audio_mixer_set_fade(fade_in);
      break;
    default:
      break;
  }
  system_state.reset_timing = true;
}

static auto toggle_scroll_lock() -> void { linapple_toggle_turbo(); }

constexpr uint8_t ANSI_FINAL_BYTE_MIN = 0x40;
constexpr uint8_t ANSI_FINAL_BYTE_MAX = 0x7E;
constexpr uint8_t ASCII_PRINTABLE_MIN = 32;
constexpr uint8_t ASCII_PRINTABLE_MAX = 127;
constexpr size_t INPUT_BUFFER_SIZE = 256;

static auto process_sequences() -> void {
  size_t i = 0;
  while (i < g_input_queue.size()) {
    if (g_input_queue.at(i) == k_a2_key_esc) {
      if (i + 1 >= g_input_queue.size()) {
        struct pollfd pfd{};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, k_esc_poll_timeout_ms);
        if (pr > 0 && (pfd.revents & POLLIN) != 0) {
          std::array<uint8_t, k_input_buffer_size> extra_buf{};
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
          map_key(k_a2_key_esc);
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
        } else if (ss3_cmd == 'A') {  // Cursor Up (SS3)
          if (tui_disk_select_is_active()) {
            tui_disk_select_move(-1, k_disk_select_page_size);
          } else if (tui_video_is_help_visible()) {
            tui_video_close_help();
#if ENABLE_DEBUGGER
          } else if (system_state.mode == app_mode_debug) {
            debugger_process_key(linapple_key_up);
#endif
          } else {
            map_key(k_a2_key_up);
          }
        } else if (ss3_cmd == 'B') {  // Cursor Down (SS3)
          if (tui_disk_select_is_active()) {
            tui_disk_select_move(1, k_disk_select_page_size);
          } else if (tui_video_is_help_visible()) {
            tui_video_close_help();
#if ENABLE_DEBUGGER
          } else if (system_state.mode == app_mode_debug) {
            debugger_process_key(linapple_key_down);
#endif
          } else {
            map_key(k_a2_key_down);
          }
        } else if (ss3_cmd == 'C') {  // Cursor Right (SS3)
          if (tui_disk_select_is_active()) {
            tui_disk_select_move(1, k_disk_select_page_size);
          } else if (tui_video_is_help_visible()) {
            tui_video_close_help();
#if ENABLE_DEBUGGER
          } else if (system_state.mode == app_mode_debug) {
            debugger_process_key(linapple_key_right);
#endif
          } else {
            map_key(k_a2_key_right);
          }
        } else if (ss3_cmd == 'D') {  // Cursor Left (SS3)
          if (tui_disk_select_is_active()) {
            tui_disk_select_move(-1, k_disk_select_page_size);
          } else if (tui_video_is_help_visible()) {
            tui_video_close_help();
#if ENABLE_DEBUGGER
          } else if (system_state.mode == app_mode_debug) {
            debugger_process_key(linapple_key_left);
#endif
          } else {
            map_key(k_a2_key_left);
          }
        } else if (ss3_cmd == 'H') {  // Home
          if (tui_disk_select_is_active()) tui_disk_select_home();
        } else if (ss3_cmd == 'F') {  // End
          if (tui_disk_select_is_active())
            tui_disk_select_end(k_disk_select_page_size);
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
            } else if (g_input_queue.at(i + 3) == 'J') {  // Linux Console F10
              save_state_load();
            } else if (g_input_queue.at(i + 3) == 'K') {  // Linux Console F11
              save_state_save();
            } else if (g_input_queue.at(i + 3) == 'L') {  // Linux Console F12
              raise(SIGINT);
            } else if (tui_video_is_help_visible()) {
              tui_video_close_help();
            }
            i += 4;
            continue;
          }
          break;
        }

        size_t end = i + 2;
        while (end < g_input_queue.size() && (end - i) < k_max_escape_length &&
               (g_input_queue.at(end) < k_ansi_final_byte_min ||
                g_input_queue.at(end) > k_ansi_final_byte_max)) {
          end++;
        }

        if (end < g_input_queue.size() &&
            g_input_queue.at(end) >= k_ansi_final_byte_min &&
            g_input_queue.at(end) <= k_ansi_final_byte_max) {
          uint8_t cmd = g_input_queue.at(end);

          if (g_input_queue.at(i + 2) == '<') {
          } else if (cmd == 'P') {  // Pause key (\x1b[P); F1 is \x1bOP (SS3)
            toggle_pause();
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
          } else if (cmd == 'X') {  // xterm F9 / Shift+F9 (\x1b[X / \x1b[1;2X)
            const std::string token(
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
            if (token.find(";2") != std::string::npos || token == "1;2") {
              tui_video_toggle_render_mode();
            } else {
              cycle_video_mode();
            }
          } else if (cmd == 'N') {  // Scroll Lock (\x1b[N)
            toggle_scroll_lock();
          } else if (cmd == 'H') {  // Home (\x1b[H)
            if (tui_disk_select_is_active()) tui_disk_select_home();
          } else if (cmd == 'F') {  // End (\x1b[F)
            if (tui_disk_select_is_active())
              tui_disk_select_end(k_disk_select_page_size);
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
            } else if (token == "20" || token == "33") {
              tui_video_toggle_render_mode();
            } else if (token == "23" || token == "34") {
              save_state_save();
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
              } else if (token == "20;2" || token == "33" || token == "33;2") {
                tui_video_toggle_render_mode();
              } else if (token == "21;5") {  // Ctrl+F10 (\x1b[21;5~)
                soft_reset_machine();
              } else if (token == "23;2" || token == "34" || token == "34;2") {
                save_state_save();
              } else if (token == "5") {  // Page Up (\x1b[5~)
                if (tui_disk_select_is_active())
                  tui_disk_select_page(-1, k_disk_select_page_size);
              } else if (token == "6") {  // Page Down (\x1b[6~)
                if (tui_disk_select_is_active()) {
                  tui_disk_select_page(1, k_disk_select_page_size);
                }
              } else if (token == "1" || token == "7") {  // Home (\x1b[1~)
                if (tui_disk_select_is_active()) {
                  tui_disk_select_home();
                }
              } else if (token == "4" || token == "8") {  // End (\x1b[4~)
                if (tui_disk_select_is_active()) {
                  tui_disk_select_end(k_disk_select_page_size);
                }
              } else if (token.find(';') != std::string::npos) {
              } else {
                try {
                  int val = std::stoi(token);
                  if (val == k_f1_vt_code) {
                    tui_video_toggle_help();
                  } else if (val == k_f2_vt_code) {
                    reset_machine();
                  } else if (val == k_f3_vt_code) {
                    tui_video_close_help();
                    tui_disk_select_open(6, 0);
                  } else if (val == k_f4_vt_code) {
                    tui_video_close_help();
                    tui_disk_select_open(6, 1);
                  } else if (val == k_f5_vt_code) {
                    swap_drives();
                  } else if (val == k_f6_vt_code) {
                    tui_video_toggle_fullscreen();
                  } else if (val == k_f7_vt_code) {
                    toggle_debugger();
                  } else if (val == k_f8_vt_code) {
                    tui_video_save_screenshot();
                  } else if (val == k_f9_vt_code) {
                    cycle_video_mode();
                  } else if (val == k_f10_vt_code) {
                    save_state_load();
                  } else if (val == k_f11_vt_code) {
                    save_state_save();
                  } else if (val == k_f12_code) {
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
              tui_disk_select_move(-1, k_disk_select_page_size);
            } else if (tui_video_is_help_visible()) {
              tui_video_close_help();
#if ENABLE_DEBUGGER
            } else if (system_state.mode == app_mode_debug) {
              debugger_process_key(linapple_key_up);
#endif
            } else {
              map_key(k_a2_key_up);
            }
          } else if (cmd == 'B') {
            if (tui_disk_select_is_active()) {
              tui_disk_select_move(1, k_disk_select_page_size);
            } else if (tui_video_is_help_visible()) {
              tui_video_close_help();
#if ENABLE_DEBUGGER
            } else if (system_state.mode == app_mode_debug) {
              debugger_process_key(linapple_key_down);
#endif
            } else {
              map_key(k_a2_key_down);
            }
          } else if (cmd == 'D') {
            if (tui_disk_select_is_active()) {
              tui_disk_select_move(-1, k_disk_select_page_size);
            } else if (tui_video_is_help_visible()) {
              tui_video_close_help();
#if ENABLE_DEBUGGER
            } else if (system_state.mode == app_mode_debug) {
              debugger_process_key(linapple_key_left);
#endif
            } else {
              map_key(k_a2_key_left);
            }
          } else if (cmd == 'C') {
            if (tui_disk_select_is_active()) {
              tui_disk_select_move(1, k_disk_select_page_size);
            } else if (tui_video_is_help_visible()) {
              tui_video_close_help();
#if ENABLE_DEBUGGER
            } else if (system_state.mode == app_mode_debug) {
              debugger_process_key(linapple_key_right);
#endif
            } else {
              map_key(k_a2_key_right);
            }
          }

          i = end + 1;
          continue;
        }

        if (end - i >= k_max_escape_length) {
          i++;
          continue;
        }

        break;
      }

      if (tui_disk_select_is_active()) {
        tui_disk_select_close();
      } else if (tui_video_is_help_visible()) {
        tui_video_close_help();
#if ENABLE_DEBUGGER
      } else if (system_state.mode == app_mode_debug) {
        debugger_process_key(linapple_key_escape);
#endif
      } else {
        map_key(k_a2_key_esc);
      }
      i++;
      continue;
    }

    uint8_t b = g_input_queue.at(i);
    if (b == k_a2_key_ctrl_c) {
      raise(SIGINT);
    } else if (tui_disk_select_is_active()) {
      if (b == k_a2_key_enter || b == '\n') {
        (void)tui_disk_select_confirm();
      } else if (b == k_a2_key_esc) {
        tui_disk_select_close();
      } else if (b >= k_ascii_printable_min && b < k_ascii_printable_max) {
        tui_disk_select_jump_char(static_cast<char>(b),
                                  k_disk_select_page_size);
      }
    } else if (tui_video_is_help_visible()) {
      tui_video_close_help();
#if ENABLE_DEBUGGER
    } else if (system_state.mode == app_mode_debug) {
      if (b == k_a2_key_enter || b == '\n') {
        debugger_process_key(linapple_key_return);
      } else if (b == k_a2_key_backspace || b == k_a2_key_delete) {
        debugger_process_key(linapple_key_backspace);
      } else if (b == k_a2_key_esc) {
        debugger_process_key(linapple_key_escape);
      } else if (b >= k_ascii_printable_min && b < k_ascii_printable_max) {
        debugger_process_key(static_cast<int>(b));
      }
#endif
    } else if (b >= k_ascii_printable_min && b < k_ascii_printable_max) {
      map_key(b);
    } else if (b == k_a2_key_enter) {
      map_key(k_a2_key_enter);
    } else if (b == k_a2_key_backspace || b == k_a2_key_delete) {
      map_key(k_a2_key_backspace);
    }

    i++;
  }
  g_input_queue.erase(g_input_queue.begin(),
                      g_input_queue.begin() + static_cast<std::ptrdiff_t>(i));
}

}  // namespace

auto tui_input_initialize() -> void {
  // Enable Mouse Tracking (Any Event + SGR)
  fputs("\x1b[?1003h\x1b[?1006h", stdout);
  fflush(stdout);
  g_joy_fd = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
}

auto tui_input_shutdown() -> void {
  // Disable Mouse Tracking
  fputs("\x1b[?1006l\x1b[?1003l", stdout);
  fflush(stdout);
  if (g_joy_fd != -1) {
    close(g_joy_fd);
    g_joy_fd = -1;
  }
  g_input_queue.clear();
  g_input_queue.shrink_to_fit();
}

auto tui_input_poll() -> void {
  std::array<uint8_t, k_input_buffer_size> buf{};
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
      // The port has three pushbutton inputs (Apple II Reference Manual, 1979,
      // p. 100); a device with more buttons keeps the rest to itself.
      if ((js.type & JS_EVENT_BUTTON) != 0 && js.number < 3) {
        const JoystickButtonPayload_t payload = {
            js.number, static_cast<uint8_t>(js.value != 0), 0, 0};
        peripheral_command(0, JOYSTICK_CMD_SET_BUTTON, &payload,
                           sizeof(payload));
      }
    }
  }
}
