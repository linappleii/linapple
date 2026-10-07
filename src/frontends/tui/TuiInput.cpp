// SPDX-License-Identifier: GPL-2.0-only
#include "TuiInput.h"

#include <asm-generic/ioctls.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
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
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/HarddiskFrontend.h"
#include "frontends/common/JoystickConfig.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/MouseFrontend.h"
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
static constexpr uint8_t k_eighth_bit = 0x80;
static constexpr uint8_t k_seven_bits = 0x7F;

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

// A terminal has no key-up, so the release is deferred one poll, long enough
// for a program polling $C010 to see the key for a frame. Alt+key is Open
// Apple held around the key.
struct HeldKey_t {
  uint32_t host_key;
  uint8_t code;
  bool open_apple;
};

static std::vector<HeldKey_t> g_held_keys;

static auto release_held_keys() -> void {
  for (const HeldKey_t& key : g_held_keys) {
    linapple_set_key(key.host_key, key.code, false);
    if (key.open_apple) {
      linapple_set_modifiers(false, false, false, false);
    }
  }
  g_held_keys.clear();
}

// A terminal has no scancodes, so the byte is read symbolically and doubles
// as the key's identity.
static auto map_key(uint8_t a2_code, bool open_apple = false) -> void {
  const KeyboardHostKey_t key = {0, a2_code, false, false};
  uint8_t code = 0;
  if (!keyboard_translate(&key, &code)) {
    return;
  }
  if (open_apple) {
    linapple_set_modifiers(false, false, true, false);
  }
  linapple_set_key(a2_code, code, true);
  g_held_keys.push_back({a2_code, code, open_apple});
}

// 0x7F is what most terminals send for Backspace, the Apple's left arrow;
// every other seven-bit byte is its own code.
static auto terminal_byte_to_apple(uint8_t byte) -> uint8_t {
  return byte == k_a2_key_delete ? k_a2_key_backspace : byte;
}

// One keystroke can be split across reads; a few milliseconds tells its tail
// from the next keystroke.
static auto read_more_input() -> void {
  struct pollfd pfd{};
  pfd.fd = STDIN_FILENO;
  pfd.events = POLLIN;
  const int pr = poll(&pfd, 1, k_esc_poll_timeout_ms);
  if (pr <= 0 || (pfd.revents & POLLIN) == 0) {
    return;
  }
  std::array<uint8_t, k_input_buffer_size> extra_buf{};
  const ssize_t extra_n =
      read(STDIN_FILENO, extra_buf.data(), extra_buf.size());
  for (ssize_t j = 0; j < extra_n; ++j) {
    g_input_queue.push_back(extra_buf.at(static_cast<size_t>(j)));
  }
}

static constexpr uint8_t k_utf8_lead2_min = 0xC2;
static constexpr uint8_t k_utf8_lead2_max = 0xDF;
static constexpr uint8_t k_utf8_lead3_max = 0xEF;
static constexpr uint8_t k_utf8_lead4_max = 0xF4;
static constexpr uint8_t k_utf8_continuation_min = 0x80;
static constexpr uint8_t k_utf8_continuation_max = 0xBF;

static auto utf8_continuation_count(uint8_t lead) -> size_t {
  if (lead < k_utf8_lead2_min) {
    return 0;
  }
  if (lead <= k_utf8_lead2_max) {
    return 1;
  }
  if (lead <= k_utf8_lead3_max) {
    return 2;
  }
  if (lead <= k_utf8_lead4_max) {
    return 3;
  }
  return 0;
}

static auto utf8_sequence_at(size_t i, size_t continuation) -> bool {
  if (i + continuation >= g_input_queue.size()) {
    return false;
  }
  for (size_t k = 1; k <= continuation; ++k) {
    const uint8_t byte = g_input_queue.at(i + k);
    if (byte < k_utf8_continuation_min || byte > k_utf8_continuation_max) {
      return false;
    }
  }
  return true;
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
    linapple_set_rocker_switch(!linapple_get_rocker_switch());
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

// The terminal's mouse is asked for only while a card can take it. A terminal
// cannot hide its pointer, so the card's pointer is put under the host's: each
// report's position within the drawn Apple screen is mapped onto the card's
// clamp window, in pixels once the terminal has said it reports them and a
// cell size is known, in cells otherwise.
static bool g_tracking = false;
static bool g_pixel_reports = false;
static int g_pixel_mode_setting = -1;
static int g_cell_width_px = 0;
static int g_cell_height_px = 0;
static bool g_left_held = false;

static constexpr int k_sgr_left_button = 0;
static constexpr int k_pixel_report_mode = 1016;
static constexpr int k_mode_set = 1;
static constexpr int k_mode_reset = 2;
static constexpr int k_mode_set_permanently = 3;

static auto write_terminal(const char* seq) -> void {
  fputs(seq, stdout);
  fflush(stdout);
}

// The TUI has no joystick-from-mouse path, so the card alone is a consumer.
static auto tracking_wanted() -> bool {
  return mouse_frontend_card_present() && mouse_frontend_capture_enabled();
}

static auto cell_size_known() -> bool {
  return g_cell_width_px > 0 && g_cell_height_px > 0;
}

// xterm fills ws_xpixel and ws_ypixel; many terminals leave them zero.
static auto read_cell_size_from_window() -> void {
  struct winsize w{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0 || w.ws_col == 0 ||
      w.ws_row == 0 || w.ws_xpixel == 0 || w.ws_ypixel == 0) {
    return;
  }
  g_cell_width_px = w.ws_xpixel / w.ws_col;
  g_cell_height_px = w.ws_ypixel / w.ws_row;
}

// An unconditional ?1016 could bring pixel reports with no cell size to scale
// them by, so pixels wait until the terminal has answered both queries.
static auto decide_report_unit() -> void {
  if (g_pixel_reports) {
    return;
  }
  if (cell_size_known()) {
    if (g_pixel_mode_setting == k_mode_reset) {
      write_terminal("\x1b[?1016h");
      g_pixel_reports = true;
    } else if (g_pixel_mode_setting == k_mode_set ||
               g_pixel_mode_setting == k_mode_set_permanently) {
      g_pixel_reports = true;
    }
    return;
  }
  if (g_pixel_mode_setting == k_mode_set ||
      g_pixel_mode_setting == k_mode_set_permanently) {
    // Another program left pixel reporting on.
    write_terminal("\x1b[?1016l");
    g_pixel_mode_setting = k_mode_reset;
  }
}

// The DECRQM and XTWINOPS replies arrive through the input queue.
static auto start_tracking() -> void {
  g_tracking = true;
  g_pixel_reports = false;
  g_pixel_mode_setting = -1;
  g_cell_width_px = 0;
  g_cell_height_px = 0;
  g_left_held = false;
  read_cell_size_from_window();
  write_terminal("\x1b[?1003h\x1b[?1006h\x1b[?1016$p\x1b[16t");
}

static auto stop_tracking() -> void {
  if (!g_tracking) {
    return;
  }
  if (g_left_held) {
    g_left_held = false;
    mouse_frontend_button(false);
  }
  write_terminal("\x1b[?1016l\x1b[?1006l\x1b[?1003l");
  g_tracking = false;
  g_pixel_reports = false;
}

static auto follow_machine() -> void {
  if (tracking_wanted() == g_tracking) {
    return;
  }
  if (g_tracking) {
    stop_tracking();
  } else {
    start_tracking();
  }
}

// A load can move the mouse card or take it away.
static auto load_state() -> void {
  save_state_load();
  follow_machine();
}

static auto picture_in_report_units() -> MousePictureRect_t {
  MousePictureRect_t box = tui_video_picture_box();
  if (g_pixel_reports) {
    box.x *= g_cell_width_px;
    box.y *= g_cell_height_px;
    box.w *= g_cell_width_px;
    box.h *= g_cell_height_px;
  }
  return box;
}

// A press or release moves nothing: under any-event tracking the pointer's
// travel to that spot has already arrived as motion reports. Coordinates are
// one-based.
static auto handle_mouse_report(const MouseSgrEvent_t& event) -> void {
  if (event.motion) {
    mouse_frontend_follow(event.x - 1, event.y - 1, picture_in_report_units());
  } else if (event.button == k_sgr_left_button) {
    if (event.pressed && !g_left_held) {
      g_left_held = true;
      mouse_frontend_button(true);
    } else if (event.released && g_left_held) {
      g_left_held = false;
      mouse_frontend_button(false);
    }
  }
}

static auto handle_terminal_reply(const uint8_t* seq, size_t len) -> void {
  int mode = 0;
  int setting = 0;
  if (mouse_frontend_decode_mode_report(seq, len, &mode, &setting)) {
    if (mode == k_pixel_report_mode) {
      g_pixel_mode_setting = setting;
      decide_report_unit();
    }
    return;
  }
  int width = 0;
  int height = 0;
  if (mouse_frontend_decode_cell_size_report(seq, len, &width, &height) &&
      width > 0 && height > 0) {
    g_cell_width_px = width;
    g_cell_height_px = height;
    decide_report_unit();
  }
}

static auto process_sequences() -> void {
  size_t i = 0;
  while (i < g_input_queue.size()) {
    if (g_input_queue.at(i) == k_a2_key_esc) {
      if (i + 1 >= g_input_queue.size()) {
        read_more_input();
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
              load_state();
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
            MouseSgrEvent_t event{};
            if (mouse_frontend_sgr_decode(&g_input_queue.at(i), end - i + 1,
                                          &event)) {
              handle_mouse_report(event);
            }
          } else if (cmd == 'y' || cmd == 't') {
            handle_terminal_reply(&g_input_queue.at(i), end - i + 1);
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
              tui_disk_select_open(harddisk_frontend_slot(), 0);
            } else {
              tui_disk_select_open(6, 0);
            }
          } else if (cmd == 'S') {  // xterm F4 / Shift+F4 (\x1b[S / \x1b[1;2S)
            tui_video_close_help();
            const std::string token(
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(i + 2),
                g_input_queue.begin() + static_cast<std::ptrdiff_t>(end));
            if (token.find(";2") != std::string::npos || token == "1;2") {
              tui_disk_select_open(harddisk_frontend_slot(), 1);
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
              tui_disk_select_open(harddisk_frontend_slot(), 0);
            } else if (token == "14" || token == "26") {
              tui_video_close_help();
              tui_disk_select_open(harddisk_frontend_slot(), 1);
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
                tui_disk_select_open(harddisk_frontend_slot(), 0);
              } else if (token == "14;2" || token == "26" || token == "26;2") {
                tui_video_close_help();
                tui_disk_select_open(harddisk_frontend_slot(), 1);
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
              } else if (token == "3") {  // Delete (\x1b[3~)
                if (tui_disk_select_is_active()) {
                } else if (tui_video_is_help_visible()) {
                  tui_video_close_help();
#if ENABLE_DEBUGGER
                } else if (system_state.mode == app_mode_debug) {
                  debugger_process_key(linapple_key_delete);
#endif
                } else {
                  map_key(k_a2_key_delete);
                }
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
                    load_state();
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

      // A terminal with metaSendsEscape sends Alt+key as ESC then the byte:
      // Open Apple with that key. A second ESC or an eighth-bit byte leaves
      // this ESC a key of its own.
      const uint8_t after_esc = g_input_queue.at(i + 1);
      if (after_esc != k_a2_key_esc && after_esc < k_eighth_bit) {
        if (!tui_disk_select_is_active() && !tui_video_is_help_visible() &&
            system_state.mode != app_mode_debug) {
          map_key(terminal_byte_to_apple(after_esc), true);
        }
        i += 2;
        continue;
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

    // A UTF-8 terminal sends Alt+key as the ESC prefix, so a valid UTF-8
    // sequence is a character the Apple has no code for; only a high byte that
    // begins no sequence is a non-UTF-8 terminal's Alt+key.
    const size_t continuation = utf8_continuation_count(b);
    if (continuation > 0) {
      if (i + continuation >= g_input_queue.size()) {
        read_more_input();
      }
      if (utf8_sequence_at(i, continuation)) {
        i += 1 + continuation;
        continue;
      }
    }

    if (tui_disk_select_is_active()) {
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
    } else if (b >= k_eighth_bit) {
      // Stock xterm outside UTF-8 sends Alt+key with the eighth bit set.
      map_key(terminal_byte_to_apple(b & k_seven_bits), true);
    } else {
      // Every control byte reaches the Apple, Ctrl-C included, so Applesoft's
      // break and DOS's Ctrl-D work from a terminal; F12 is the way out.
      map_key(terminal_byte_to_apple(b));
    }

    i++;
  }
  g_input_queue.erase(g_input_queue.begin(),
                      g_input_queue.begin() + static_cast<std::ptrdiff_t>(i));
}

}  // namespace

auto tui_input_initialize() -> void {
  if (tracking_wanted()) {
    start_tracking();
  }
  g_joy_fd = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
  if (g_joy_fd == -1) {
    return;
  }
  // A second plug on the connector, with a 560 ohm pull-down on each line it
  // has a button for (Sather, Understanding the Apple II, 7-9 and 7-11).
  uint8_t buttons = 0;
  const bool three_buttons =
      ioctl(g_joy_fd, JSIOCGBUTTONS, &buttons) == 0 && buttons >= 3;
  linapple_set_game_pulldowns(joystick_config_pulldown_mask() |
                              joystick_line_pb0 | joystick_line_pb1 |
                              (three_buttons ? joystick_line_pb2 : 0));
}

auto tui_input_on_resize() -> void {
  if (!g_tracking) {
    return;
  }
  read_cell_size_from_window();
  decide_report_unit();
  write_terminal("\x1b[16t");
}

auto tui_input_shutdown() -> void {
  stop_tracking();
  if (g_joy_fd != -1) {
    close(g_joy_fd);
    g_joy_fd = -1;
  }
  g_input_queue.clear();
  g_input_queue.shrink_to_fit();
  g_held_keys.clear();
  g_held_keys.shrink_to_fit();
}

auto tui_input_poll() -> void {
  release_held_keys();
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
        linapple_set_game_switch(js.number, js.value != 0);
      }
    }
  }
}
