// SPDX-License-Identifier: GPL-2.0-only

#include <SDL/SDL_active.h>
#include <SDL/SDL_events.h>
#include <SDL/SDL_keyboard.h>
#include <SDL/SDL_keysym.h>
#include <SDL/SDL_mouse.h>
#include <SDL/SDL_stdinc.h>

#include <cstdint>
#include <cstdio>

#if ENABLE_DEBUGGER
#include "Debugger/Debug.h"
#endif
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/common/sdl/MouseInput.h"
#include "frontends/sdl1/Frame.h"

namespace {

constexpr int k_user_event_reboot = 1;

auto host_button(Uint8 button) -> MouseHostButton_t {
  switch (button) {
    case SDL_BUTTON_LEFT:
      return MouseHostButton_t::left;
    case SDL_BUTTON_MIDDLE:
      return MouseHostButton_t::middle;
    case SDL_BUTTON_RIGHT:
      return MouseHostButton_t::right;
    default:
      return MouseHostButton_t::other;
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
    if (keyboard_get_caps_mode() == caps_mode_host) {
      const uint8_t caps = ((key_mod & KMOD_CAPS) != 0) ? 1 : 0;
      peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
    } else {
      linapple_toggle_caps_lock_state();
    }
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
    const LinAppleKey_t core_key = frontend_to_core_key(key_sym, key_mod);
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
    if (keyboard_get_caps_mode() == caps_mode_host) {
      const uint8_t caps = ((key_mod & KMOD_CAPS) != 0) ? 1 : 0;
      peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
    }
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
                      event->key.keysym.scancode);
      break;

    case SDL_KEYUP:
      handle_key_up(event->key.keysym.sym, event->key.keysym.mod,
                    event->key.keysym.scancode);
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
      if (event->user.code == k_user_event_reboot) {
        process_button_click(k_btn_run, KMOD_LCTRL);
      }
      break;

    default:
      break;
  }
}