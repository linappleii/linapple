// SPDX-License-Identifier: GPL-2.0-only
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <cstdio>

#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/common/sdl/MouseInput.h"
#include "frontends/sdl3/Frame.h"

#if ENABLE_DEBUGGER
#include "Debugger/Debug.h"
#endif

namespace {

constexpr auto is_extended_scancode(SDL_Scancode scancode) noexcept -> bool {
  return (scancode >= SDL_SCANCODE_INSERT && scancode <= SDL_SCANCODE_UP) ||
         (scancode == SDL_SCANCODE_DELETE);
}

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
                              SDL_Keymod key_mod) -> void {
  const bool release_modifier =
      (key_mod & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL)) != 0;
  const bool debugger_click = mouse_input_button_down(
      host_button(button.button), release_modifier, g_buttondown != -1);
#if ENABLE_DEBUGGER
  if (debugger_click) {
    debugger_mouse_click(static_cast<int>(button.x),
                         static_cast<int>(button.y));
  }
#else
  (void)debugger_click;
#endif
}

auto handle_mouse_button_up(const SDL_MouseButtonEvent& button) -> void {
  mouse_input_button_up(host_button(button.button));
}

// SDL3 reports motion in floats and relative mode on a scaled display delivers
// fractions of a pixel; truncating each event would lose a slow drag.
auto handle_mouse_motion(const SDL_MouseMotionEvent& motion) -> void {
  static float carry_x = 0.0F;
  static float carry_y = 0.0F;
  carry_x += motion.xrel;
  carry_y += motion.yrel;
  const int dx = static_cast<int>(carry_x);
  const int dy = static_cast<int>(carry_y);
  carry_x -= static_cast<float>(dx);
  carry_y -= static_cast<float>(dy);
  mouse_input_motion(dx, dy, static_cast<int>(motion.x),
                     static_cast<int>(motion.y));
}

}  // namespace

auto sdl_handle_event(SDL_Event* event) -> void {
  if (event == nullptr) {
    return;
  }

  switch (event->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      system_state.mode = app_mode_exit;
      break;

    case SDL_EVENT_WINDOW_RESIZED:
      frame_on_resize(event->window.data1, event->window.data2);
      break;

    case SDL_EVENT_WINDOW_EXPOSED:
      frame_on_expose();
      break;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      frame_on_focus(true);
      break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
      frame_on_focus(false);
      g_buttondown = -1;
      mouse_input_release();
      break;

    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_REMOVED:
      joy_frontend_initialize();
      break;

    case SDL_EVENT_KEY_DOWN: {
      if (event->key.repeat) {
        break;
      }

      const SDL_Keycode mysym = event->key.key;
      const SDL_Keymod mymod = event->key.mod;
      const SDL_Scancode myscancode = event->key.scancode;

      if (frontend_handle_event(mysym, true)) {
        break;
      }

      int qs_slot = 0;
      bool qs_is_save = false;
      if (keyboard_is_quicksave_combo(mysym, mymod, &qs_slot, &qs_is_save)) {
        frame_quick_state(qs_slot, qs_is_save ? SDL_KMOD_SHIFT : 0);
        break;
      }

      if (keyboard_get_hotkeys_enabled() && (mysym >= SDLK_F1) &&
          (mysym <= SDLK_F12) && (g_buttondown == -1)) {
        mouse_input_release();
        g_buttondown = static_cast<int>(mysym - SDLK_F1);
      } else if (mysym == SDLK_KP_PLUS) {
        const uint32_t speed = linapple_speed_increase();
        std::printf("Now speed=%u\n", speed);
      } else if (mysym == SDLK_KP_MINUS) {
        const uint32_t speed = linapple_speed_decrease();
        std::printf("Now speed=%u\n", speed);
      } else if (mysym == SDLK_KP_MULTIPLY) {
        const uint32_t speed = linapple_speed_reset();
        std::printf("Now speed=%u\n", speed);
      } else if (mysym == SDLK_CAPSLOCK) {
        keyboard_press_caps_lock(mymod);
      } else if (mysym == SDLK_PAUSE) {
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
      } else if (mysym == SDLK_SCROLLLOCK) {
        const bool turbo = linapple_toggle_turbo();
        std::printf("Turbo mode: %s\n", turbo ? "ON" : "OFF");
      } else if ((system_state.mode == app_mode_running) ||
                 (system_state.mode == app_mode_logo) ||
                 (system_state.mode == app_mode_stepping)) {
#if ENABLE_DEBUGGER
        g_debugger_eat_key = false;
#endif
        const bool extended = is_extended_scancode(myscancode);
        if ((mymod & SDL_KMOD_RCTRL) != 0) {
          joy_frontend_update_trim_via_key(mysym);
        } else {
          if (!joy_frontend_process_key(mysym, extended, true, false)) {
            frontend_dispatch_key_event(myscancode, mysym, mymod, true);
          }
        }
#if ENABLE_DEBUGGER
      } else if (system_state.mode == app_mode_debug) {
        const LinAppleKey_t core_key = frontend_to_core_key(mysym, mymod);
        if (core_key != linapple_key_unknown) {
          debugger_process_key(core_key);
        }
#endif
      }
      break;
    }

    case SDL_EVENT_KEY_UP: {
      const SDL_Keycode mysym = event->key.key;
      const SDL_Keymod mymod = event->key.mod;
      const SDL_Scancode myscancode = event->key.scancode;

      if ((mysym >= SDLK_F1) && (mysym <= SDLK_F12) &&
          (g_buttondown == static_cast<int>(mysym - SDLK_F1))) {
        g_buttondown = -1;
        process_button_click(static_cast<int>(mysym - SDLK_F1), mymod);
      } else if (frontend_handle_event(mysym, false)) {
        break;
      } else if (mysym == SDLK_CAPSLOCK) {
        keyboard_sync_host_caps(mymod);
      } else {
        const bool extended = is_extended_scancode(myscancode);
        if (!joy_frontend_process_key(mysym, extended, false, false)) {
          frontend_dispatch_key_event(myscancode, mysym, mymod, false);
        }
      }
      break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      handle_mouse_button_down(event->button, SDL_GetModState());
      break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
      handle_mouse_button_up(event->button);
      break;

    case SDL_EVENT_MOUSE_MOTION:
      handle_mouse_motion(event->motion);
      break;

    case SDL_EVENT_USER:
      if (event->user.code == 1) {
        process_button_click(k_btn_run, SDL_KMOD_LCTRL);
      }
      break;

    default:
      break;
  }
}
