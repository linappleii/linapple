// SPDX-License-Identifier: GPL-2.0-only
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <cstdint>
#include <cstdio>

#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/MouseFrontend.h"
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

auto handle_mouse_button_down(const SDL_MouseButtonEvent& button,
                              SDL_Keymod key_mod) -> void {
  if (button.button == SDL_BUTTON_MIDDLE) {
    set_using_cursor(!g_usingcursor);
    return;
  }

  if (button.button == SDL_BUTTON_RIGHT) {
    if (g_usingcursor) {
      mouse_input_dispatch_button(k_mouse_button_right, true);
    }
    return;
  }

  if (button.button != SDL_BUTTON_LEFT || g_buttondown != -1) {
    return;
  }

  const int x = static_cast<int>(button.x);
  const int y = static_cast<int>(button.y);

#if ENABLE_DEBUGGER
  if (system_state.mode == app_mode_debug) {
    debugger_mouse_click(x, y);
    return;
  }
#endif

  if (!g_usingcursor) {
    if (mouse_input_should_auto_capture()) {
      set_using_cursor(true);
    }
    return;
  }

  if ((key_mod & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL)) != 0) {
    set_using_cursor(false);
    return;
  }

  mouse_input_dispatch_button(k_mouse_button_left, true);
}

auto handle_mouse_button_up(const SDL_MouseButtonEvent& button) -> void {
  if (!g_usingcursor) {
    return;
  }
  if (button.button == SDL_BUTTON_LEFT) {
    mouse_input_dispatch_button(k_mouse_button_left, false);
  } else if (button.button == SDL_BUTTON_RIGHT) {
    mouse_input_dispatch_button(k_mouse_button_right, false);
  }
}

auto handle_mouse_motion(const SDL_MouseMotionEvent& motion) -> void {
  if (!g_usingcursor) {
    return;
  }
  mouse_input_dispatch_motion(static_cast<int>(motion.x),
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
      set_using_cursor(false);
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
        set_using_cursor(false);
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
        if (keyboard_get_caps_mode() == caps_mode_host) {
          const uint8_t caps = ((mymod & SDL_KMOD_CAPS) != 0) ? 1 : 0;
          peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
        } else {
          linapple_toggle_caps_lock_state();
        }
      } else if (mysym == SDLK_PAUSE) {
        set_using_cursor(false);
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
        if (keyboard_get_caps_mode() == caps_mode_host) {
          const uint8_t caps = ((mymod & SDL_KMOD_CAPS) != 0) ? 1 : 0;
          peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
        }
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
