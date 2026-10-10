// SPDX-License-Identifier: GPL-2.0-only
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_video.h>

#include <cstdint>
#include <cstdio>

#if ENABLE_DEBUGGER
#include "Debugger/Debug.h"
#endif
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/common/sdl/MouseInput.h"
#include "frontends/sdl2/Frame.h"

namespace {

constexpr int user_event_reboot = 1;

constexpr auto is_extended_scancode(SDL_Scancode scancode) noexcept -> bool {
  return (scancode >= SDL_SCANCODE_INSERT && scancode <= SDL_SCANCODE_UP) ||
         (scancode == SDL_SCANCODE_DELETE);
}

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
                              SDL_Keymod key_mod) -> void {
  const bool release_modifier = (key_mod & (KMOD_SHIFT | KMOD_CTRL)) != 0;
  const bool debugger_click = mouse_input_button_down(
      host_button(button.button), release_modifier, buttondown != -1);
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

}  // namespace

auto sdl_handle_event(SDL_Event* event) -> void {
  if (event == nullptr) {
    return;
  }

  switch (event->type) {
    case SDL_QUIT:
      system_state.mode = app_mode_exit;
      break;

    case SDL_WINDOWEVENT:
      switch (event->window.event) {
        case SDL_WINDOWEVENT_CLOSE:
          system_state.mode = app_mode_exit;
          break;
        case SDL_WINDOWEVENT_RESIZED:
          frame_on_resize(event->window.data1, event->window.data2);
          break;
        case SDL_WINDOWEVENT_EXPOSED:
          frame_on_expose();
          break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
          frame_on_focus(true);
          break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
          frame_on_focus(false);
          buttondown = -1;
          mouse_input_release();
          break;
        default:
          break;
      }
      break;

    case SDL_KEYDOWN: {
      const SDL_Keycode key_sym = event->key.keysym.sym;
      const auto key_mod = static_cast<SDL_Keymod>(event->key.keysym.mod);
      const SDL_Scancode scancode = event->key.keysym.scancode;

      if (event->key.repeat != 0) {
        break;
      }

      if (frontend_handle_key_event(key_sym, true)) {
        break;
      }

      int qs_slot = 0;
      bool qs_is_save = false;
      if (keyboard_is_quicksave_combo(key_sym, key_mod, &qs_slot,
                                      &qs_is_save)) {
        frame_quick_state(qs_slot, qs_is_save ? KMOD_SHIFT : 0);
        break;
      }

      if (keyboard_get_hotkeys_enabled() && (key_sym >= SDLK_F1) &&
          (key_sym <= SDLK_F12) && (buttondown == -1)) {
        mouse_input_release();
        buttondown = key_sym - SDLK_F1;
      } else if (key_sym == SDLK_KP_PLUS) {
        const uint32_t speed = linapple_speed_increase();
        std::printf("Now speed=%u\n", speed);
      } else if (key_sym == SDLK_KP_MINUS) {
        const uint32_t speed = linapple_speed_decrease();
        std::printf("Now speed=%u\n", speed);
      } else if (key_sym == SDLK_KP_MULTIPLY) {
        const uint32_t speed = linapple_speed_reset();
        std::printf("Now speed=%u\n", speed);
      } else if (key_sym == SDLK_CAPSLOCK) {
        keyboard_press_caps_lock(key_mod);
      } else if (key_sym == SDLK_PAUSE) {
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
        if (system_state.mode != app_mode_logo &&
            system_state.mode != app_mode_debug) {
          video_redraw_screen();
        }
        system_state.reset_timing = true;
      } else if (key_sym == SDLK_SCROLLLOCK) {
        const bool turbo = linapple_toggle_turbo();
        std::printf("Turbo mode: %s\n", turbo ? "ON" : "OFF");
      } else if (system_state.mode == app_mode_running ||
                 system_state.mode == app_mode_logo ||
                 system_state.mode == app_mode_stepping) {
#if ENABLE_DEBUGGER
        debugger_eat_key = false;
#endif
        const bool extended = is_extended_scancode(scancode);
        if ((key_mod & KMOD_RCTRL) != 0) {
          joy_frontend_update_trim_via_key(key_sym);
        } else {
          if (!joy_frontend_process_key(key_sym, extended, true, false)) {
            frontend_dispatch_key_event(scancode, key_sym, key_mod, true);
          }
        }
#if ENABLE_DEBUGGER
      } else if (system_state.mode == app_mode_debug) {
        const LinAppleKey core_key = frontend_to_core_key(key_sym, key_mod);
        if (core_key != linapple_key_unknown) {
          debugger_process_key(core_key);
        }
#endif
      }
      break;
    }

    case SDL_KEYUP: {
      const SDL_Keycode key_sym = event->key.keysym.sym;
      const auto key_mod = static_cast<SDL_Keymod>(event->key.keysym.mod);
      const SDL_Scancode scancode = event->key.keysym.scancode;

      if ((key_sym >= SDLK_F1) && (key_sym <= SDLK_F12) &&
          (buttondown == key_sym - SDLK_F1)) {
        buttondown = -1;
        process_button_click(key_sym - SDLK_F1, key_mod);
      } else if (frontend_handle_key_event(key_sym, false)) {
        break;
      } else if (key_sym == SDLK_CAPSLOCK) {
        keyboard_sync_host_caps(key_mod);
      } else {
        const bool extended = is_extended_scancode(scancode);
        if (!joy_frontend_process_key(key_sym, extended, false, false)) {
          frontend_dispatch_key_event(scancode, key_sym, key_mod, false);
        }
      }
      break;
    }

    case SDL_MOUSEBUTTONDOWN:
      handle_mouse_button_down(event->button,
                               static_cast<SDL_Keymod>(SDL_GetModState()));
      break;

    case SDL_MOUSEBUTTONUP:
      handle_mouse_button_up(event->button);
      break;

    case SDL_MOUSEMOTION:
      handle_mouse_motion(event->motion);
      break;

    case SDL_JOYDEVICEADDED:
    case SDL_JOYDEVICEREMOVED:
      joy_frontend_initialize();
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
