// SPDX-License-Identifier: GPL-2.0-only

#include <SDL/SDL_active.h>
#include <SDL/SDL_events.h>
#include <SDL/SDL_keyboard.h>
#include <SDL/SDL_keysym.h>
#include <SDL/SDL_mouse.h>
#include <SDL/SDL_stdinc.h>
#include <SDL/SDL_video.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if ENABLE_DEBUGGER
#include "Debugger/Debug.h"
#endif
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/KeyboardMaps.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/common/sdl/MouseInput.h"
#include "frontends/sdl1/Frame.h"

namespace {

// SDL 1.2's keysym.scancode is hardware specific; under X11 it is the key's
// evdev code plus the server's minimum keycode of 8, and the kernel's HID
// driver fixes each usage's evdev code (drivers/hid/hid-input.c,
// hid_keyboard[]). This inverts that for usages 4 to 82, the range the maps
// index; usage 50 shares KEY_BACKSLASH with 49 and so reads as 49.
struct X11Usage {
  uint8_t evdev;
  uint8_t usage;
};

constexpr uint8_t x11_min_keycode = 8;

constexpr uint8_t usage_sysrq = 70;
constexpr uint8_t usage_scroll_lock = 71;
constexpr uint8_t usage_pause = 72;
constexpr uint8_t usage_insert = 73;
constexpr uint8_t usage_home = 74;
constexpr uint8_t usage_page_up = 75;
constexpr uint8_t usage_delete = 76;
constexpr uint8_t usage_end = 77;
constexpr uint8_t usage_page_down = 78;

constexpr std::array<X11Usage, 78> x11_usages = {{
    {30, keyb_idx_a},
    {48, keyb_idx_b},
    {46, keyb_idx_c},
    {32, keyb_idx_d},
    {18, keyb_idx_e},
    {33, keyb_idx_f},
    {34, keyb_idx_g},
    {35, keyb_idx_h},
    {23, keyb_idx_i},
    {36, keyb_idx_j},
    {37, keyb_idx_k},
    {38, keyb_idx_l},
    {50, keyb_idx_m},
    {49, keyb_idx_n},
    {24, keyb_idx_o},
    {25, keyb_idx_p},
    {16, keyb_idx_q},
    {19, keyb_idx_r},
    {31, keyb_idx_s},
    {20, keyb_idx_t},
    {22, keyb_idx_u},
    {47, keyb_idx_v},
    {17, keyb_idx_w},
    {45, keyb_idx_x},
    {21, keyb_idx_y},
    {44, keyb_idx_z},
    {2, keyb_idx_1},
    {3, keyb_idx_2},
    {4, keyb_idx_3},
    {5, keyb_idx_4},
    {6, keyb_idx_5},
    {7, keyb_idx_6},
    {8, keyb_idx_7},
    {9, keyb_idx_8},
    {10, keyb_idx_9},
    {11, keyb_idx_0},
    {28, keyb_idx_return},
    {1, keyb_idx_escape},
    {14, keyb_idx_backspace},
    {15, keyb_idx_tab},
    {57, keyb_idx_space},
    {12, keyb_idx_minus},
    {13, keyb_idx_equals},
    {26, keyb_idx_leftbracket},
    {27, keyb_idx_rightbracket},
    {43, keyb_idx_backslash},
    {39, keyb_idx_semicolon},
    {40, keyb_idx_apostrophe},
    {41, keyb_idx_grave},
    {51, keyb_idx_comma},
    {52, keyb_idx_period},
    {53, keyb_idx_slash},
    {58, keyb_idx_capslock},
    {59, keyb_idx_f1},
    {60, keyb_idx_f2},
    {61, keyb_idx_f3},
    {62, keyb_idx_f4},
    {63, keyb_idx_f5},
    {64, keyb_idx_f6},
    {65, keyb_idx_f7},
    {66, keyb_idx_f8},
    {67, keyb_idx_f9},
    {68, keyb_idx_f10},
    {87, keyb_idx_f11},
    {88, keyb_idx_f12},
    {99, usage_sysrq},
    {70, usage_scroll_lock},
    {119, usage_pause},
    {110, usage_insert},
    {102, usage_home},
    {104, usage_page_up},
    {111, usage_delete},
    {107, usage_end},
    {109, usage_page_down},
    {106, keyb_idx_right},
    {105, keyb_idx_left},
    {108, keyb_idx_down},
    {103, keyb_idx_up},
}};

// Under any other video driver the usage comes from the keysym, for the US
// layout's keys.
auto keysym_to_hid(SDLKey sym) -> uint8_t {
  if (sym >= SDLK_a && sym <= SDLK_z) {
    return static_cast<uint8_t>(keyb_idx_a + (sym - SDLK_a));
  }
  if (sym >= SDLK_1 && sym <= SDLK_9) {
    return static_cast<uint8_t>(keyb_idx_1 + (sym - SDLK_1));
  }
  if (sym >= SDLK_F1 && sym <= SDLK_F12) {
    return static_cast<uint8_t>(keyb_idx_f1 + (sym - SDLK_F1));
  }
  switch (sym) {
    case SDLK_0:
      return keyb_idx_0;
    case SDLK_RETURN:
      return keyb_idx_return;
    case SDLK_ESCAPE:
      return keyb_idx_escape;
    case SDLK_BACKSPACE:
      return keyb_idx_backspace;
    case SDLK_TAB:
      return keyb_idx_tab;
    case SDLK_SPACE:
      return keyb_idx_space;
    case SDLK_MINUS:
      return keyb_idx_minus;
    case SDLK_EQUALS:
      return keyb_idx_equals;
    case SDLK_LEFTBRACKET:
      return keyb_idx_leftbracket;
    case SDLK_RIGHTBRACKET:
      return keyb_idx_rightbracket;
    case SDLK_BACKSLASH:
      return keyb_idx_backslash;
    case SDLK_SEMICOLON:
      return keyb_idx_semicolon;
    case SDLK_QUOTE:
      return keyb_idx_apostrophe;
    case SDLK_BACKQUOTE:
      return keyb_idx_grave;
    case SDLK_COMMA:
      return keyb_idx_comma;
    case SDLK_PERIOD:
      return keyb_idx_period;
    case SDLK_SLASH:
      return keyb_idx_slash;
    case SDLK_CAPSLOCK:
      return keyb_idx_capslock;
    case SDLK_RIGHT:
      return keyb_idx_right;
    case SDLK_LEFT:
      return keyb_idx_left;
    case SDLK_DOWN:
      return keyb_idx_down;
    case SDLK_UP:
      return keyb_idx_up;
    default:
      return keyb_idx_unknown;
  }
}

auto video_driver_is_x11() -> bool {
  std::array<char, 16> name{};
  return SDL_VideoDriverName(name.data(), static_cast<int>(name.size())) !=
             nullptr &&
         std::strcmp(name.data(), "x11") == 0;
}

}  // namespace

auto sdl1_x11_keycode_to_hid(uint8_t keycode) -> uint8_t {
  for (const X11Usage& row : x11_usages) {
    if (row.evdev + x11_min_keycode == keycode) {
      return row.usage;
    }
  }
  return keyb_idx_unknown;
}

namespace {

auto sdl1_hid_usage(const SDL_keysym& keysym) -> uint8_t {
  return video_driver_is_x11() ? sdl1_x11_keycode_to_hid(keysym.scancode)
                               : keysym_to_hid(keysym.sym);
}

constexpr int user_event_reboot = 1;

auto host_button(Uint8 button) -> MouseHostButton {
  switch (button) {
    case SDL_BUTTON_LEFT:
      return MouseHostButton::left;
    case SDL_BUTTON_MIDDLE:
      return MouseHostButton::middle;
    case SDL_BUTTON_RIGHT:
      return MouseHostButton::right;
    default:
      return MouseHostButton::other;
  }
}

auto handle_mouse_button_down(const SDL_MouseButtonEvent& button,
                              SDLMod key_mod) -> void {
  const bool release_modifier = (key_mod & (KMOD_SHIFT | KMOD_CTRL)) != 0;
  const bool debugger_click = mouse_input_button_down(
      host_button(button.button), release_modifier, g_buttondown != -1);
#if ENABLE_DEBUGGER
  if (debugger_click) {
    debugger_mouse_click(button.x, button.y);
  }
#else
  (void)debugger_click;
#endif
}

auto handle_mouse_button_up(const SDL_MouseButtonEvent& button) -> void {
  mouse_input_button_up(host_button(button.button));
}

auto handle_mouse_motion(const SDL_MouseMotionEvent& motion) -> void {
  mouse_input_motion(motion.xrel, motion.yrel, motion.x, motion.y);
}

auto handle_pause_key() -> void {
  mouse_input_release();
  switch (system_state.mode) {
    case app_mode_running:
      system_state.mode = app_mode_paused;
      audio_mixer_set_fade(fade_out);
      break;
    case app_mode_paused:
      system_state.mode = app_mode_running;
      audio_mixer_set_fade(fade_in);
      break;
    case app_mode_stepping:
#if ENABLE_DEBUGGER
      debugger_input_console_char(DEBUG_EXIT_KEY);
#endif
      break;
    case app_mode_logo:
    case app_mode_debug:
    default:
      break;
  }
  draw_status_area(draw_title);
  if ((system_state.mode != app_mode_logo) &&
      (system_state.mode != app_mode_debug)) {
    video_redraw_screen();
  }
  system_state.reset_timing = true;
}

auto handle_key_down(SDLKey key_sym, SDLMod key_mod, uint8_t scancode) -> void {
  if (frontend_handle_key_event(key_sym, true)) {
    return;
  }

  int qs_slot = 0;
  bool qs_is_save = false;
  if (keyboard_is_quicksave_combo(key_sym, key_mod, &qs_slot, &qs_is_save)) {
    frame_quick_state(qs_slot, qs_is_save ? KMOD_SHIFT : 0);
    return;
  }

  if (keyboard_get_hotkeys_enabled() && (key_sym >= SDLK_F1) &&
      (key_sym <= SDLK_F12) && (g_buttondown == -1)) {
    mouse_input_release();
    g_buttondown = key_sym - SDLK_F1;
    return;
  }

  if (key_sym == SDLK_KP_PLUS) {
    const uint32_t speed = linapple_speed_increase();
    std::printf("Now speed=%u\n", speed);
    return;
  }
  if (key_sym == SDLK_KP_MINUS) {
    const uint32_t speed = linapple_speed_decrease();
    std::printf("Now speed=%u\n", speed);
    return;
  }
  if (key_sym == SDLK_KP_MULTIPLY) {
    const uint32_t speed = linapple_speed_reset();
    std::printf("Now speed=%u\n", speed);
    return;
  }

  if (key_sym == SDLK_CAPSLOCK) {
    keyboard_press_caps_lock(key_mod);
    return;
  }

  if (key_sym == SDLK_PAUSE) {
    handle_pause_key();
    return;
  }

  if (key_sym == SDLK_SCROLLOCK) {
    const bool turbo = linapple_toggle_turbo();
    std::printf("Turbo mode: %s\n", turbo ? "ON" : "OFF");
    return;
  }

  if ((system_state.mode == app_mode_running) ||
      (system_state.mode == app_mode_logo) ||
      (system_state.mode == app_mode_stepping)) {
#if ENABLE_DEBUGGER
    g_debugger_eat_key = false;
#endif
    const bool extended = (key_sym >= SDLK_UP && key_sym <= SDLK_INSERT) ||
                          (key_sym == SDLK_DELETE);
    if ((key_mod & KMOD_RCTRL) != 0) {
      joy_frontend_update_trim_via_key(key_sym);
    } else if (!joy_frontend_process_key(key_sym, extended, true, false)) {
      frontend_dispatch_key_event(scancode, key_sym, key_mod, true);
    }
    return;
  }

#if ENABLE_DEBUGGER
  if (system_state.mode == app_mode_debug) {
    const LinAppleKey core_key = frontend_to_core_key(key_sym, key_mod);
    if (core_key != linapple_key_unknown) {
      debugger_process_key(core_key);
    }
  }
#endif
}

auto handle_key_up(SDLKey key_sym, SDLMod key_mod, uint8_t scancode) -> void {
  if ((key_sym >= SDLK_F1) && (key_sym <= SDLK_F12) &&
      (g_buttondown == key_sym - SDLK_F1)) {
    g_buttondown = -1;
    process_button_click(key_sym - SDLK_F1, key_mod);
    return;
  }

  if (frontend_handle_key_event(key_sym, false)) {
    return;
  }

  if (key_sym == SDLK_CAPSLOCK) {
    keyboard_sync_host_caps(key_mod);
    return;
  }

  const bool extended = (key_sym >= SDLK_UP && key_sym <= SDLK_INSERT) ||
                        (key_sym == SDLK_DELETE);
  if (!joy_frontend_process_key(key_sym, extended, false, false)) {
    frontend_dispatch_key_event(scancode, key_sym, key_mod, false);
  }
}

auto handle_active_event(const SDL_ActiveEvent& active) -> void {
  if ((active.state & SDL_APPINPUTFOCUS) == 0) {
    return;
  }
  if (active.gain != 0) {
    frame_on_focus(true);
    return;
  }
  frame_on_focus(false);
  g_buttondown = -1;
  mouse_input_release();
}

}  // namespace

auto sdl_handle_event(SDL_Event* event) -> void {
  if (event == nullptr) {
    return;
  }

  switch (event->type) {
    case SDL_QUIT:
      system_state.mode = app_mode_exit;
      break;

    case SDL_VIDEORESIZE:
      frame_on_resize(event->resize.w, event->resize.h);
      break;

    case SDL_VIDEOEXPOSE:
      frame_on_expose();
      break;

    case SDL_ACTIVEEVENT:
      handle_active_event(event->active);
      break;

    case SDL_KEYDOWN:
      handle_key_down(event->key.keysym.sym, event->key.keysym.mod,
                      sdl1_hid_usage(event->key.keysym));
      break;

    case SDL_KEYUP:
      handle_key_up(event->key.keysym.sym, event->key.keysym.mod,
                    sdl1_hid_usage(event->key.keysym));
      break;

    case SDL_MOUSEBUTTONDOWN:
      handle_mouse_button_down(event->button, SDL_GetModState());
      break;

    case SDL_MOUSEBUTTONUP:
      handle_mouse_button_up(event->button);
      break;

    case SDL_MOUSEMOTION:
      handle_mouse_motion(event->motion);
      break;

    case SDL_USEREVENT:
      if (event->user.code == user_event_reboot) {
        process_button_click(btn_run, KMOD_LCTRL);
      }
      break;

    default:
      break;
  }
}