// SPDX-License-Identifier: GPL-2.0-only

#include <SDL/SDL_active.h>
#include <SDL/SDL_events.h>
#include <SDL/SDL_keyboard.h>
#include <SDL/SDL_keysym.h>
#include <SDL/SDL_mouse.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

#if ENABLE_DEBUGGER
#include "Debugger/Debug.h"
#endif
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/sdl1/Frame.h"

namespace {

constexpr uint8_t k_mouse_button_left = 0;
constexpr uint8_t k_mouse_button_right = 1;
constexpr int k_user_event_reboot = 1;

auto dispatch_mouse_button(uint8_t button, bool is_down) -> void {
  uint8_t mouse_active = 0;
  size_t qsize = 1;
  peripheral_query(mouse_default_slot, mouse_query_is_active, &mouse_active,
                   &qsize);
  if (mouse_active != 0) {
    MouseButtonPayload_t payload{button, is_down, {0, 0}};
    peripheral_command(mouse_default_slot, mouse_cmd_set_button, &payload,
                       sizeof(payload));
  }
  if (joy_frontend_is_mouse_emulation_active()) {
    joy_frontend_process_mouse_button(button, is_down);
  }
}

}  // namespace

auto sdl_handle_event(SDL_Event* event) -> void {
  if (event == nullptr) {
    return;
  }

  int x_local = 0;
  int y_local = 0;

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
      if ((event->active.state & SDL_APPINPUTFOCUS) != 0) {
        if (event->active.gain != 0) {
          frame_on_focus(true);
        } else {
          frame_on_focus(false);
          g_buttondown = -1;
          set_using_cursor(false);
        }
      }
      break;

    case SDL_KEYDOWN: {
      const SDLKey key_sym = event->key.keysym.sym;
      const SDLMod key_mod = event->key.keysym.mod;
      const uint8_t scancode = event->key.keysym.scancode;

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
          (key_sym <= SDLK_F12) && (g_buttondown == -1)) {
        set_using_cursor(false);
        g_buttondown = key_sym - SDLK_F1;
      } else if (key_sym == SDLK_KP_PLUS) {
        const uint32_t speed = linapple_speed_increase();
        printf("Now speed=%u\n", speed);
      } else if (key_sym == SDLK_KP_MINUS) {
        const uint32_t speed = linapple_speed_decrease();
        printf("Now speed=%u\n", speed);
      } else if (key_sym == SDLK_KP_MULTIPLY) {
        const uint32_t speed = linapple_speed_reset();
        printf("Now speed=%u\n", speed);
      } else if (key_sym == SDLK_CAPSLOCK) {
        if (keyboard_get_caps_mode() == caps_mode_host) {
          const uint8_t caps = ((key_mod & KMOD_CAPS) != 0) ? 1 : 0;
          peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
        } else {
          linapple_toggle_caps_lock_state();
        }
      } else if (key_sym == SDLK_PAUSE) {
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
      } else if (key_sym == SDLK_SCROLLOCK) {
        const bool turbo = linapple_toggle_turbo();
        printf("Turbo mode: %s\n", turbo ? "ON" : "OFF");
      } else if ((system_state.mode == app_mode_running) ||
                 (system_state.mode == app_mode_logo) ||
                 (system_state.mode == app_mode_stepping)) {
#if ENABLE_DEBUGGER
        g_debugger_eat_key = false;
#endif
        const bool extended = (key_sym >= SDLK_UP && key_sym <= SDLK_INSERT) ||
                              (key_sym == SDLK_DELETE);
        if ((key_mod & KMOD_RCTRL) != 0) {
          joy_frontend_update_trim_via_key(key_sym);
        } else {
          if (!joy_frontend_process_key(key_sym, extended, true, false)) {
            frontend_dispatch_key_event(scancode, key_sym, key_mod, true);
          }
        }
#if ENABLE_DEBUGGER
      } else if (system_state.mode == app_mode_debug) {
        const LinAppleKey_t core_key = frontend_to_core_key(key_sym, key_mod);
        if (core_key != linapple_key_unknown) {
          debugger_process_key(core_key);
        }
#endif
      }
      break;
    }

    case SDL_KEYUP: {
      const SDLKey key_sym = event->key.keysym.sym;
      const SDLMod key_mod = event->key.keysym.mod;
      const uint8_t scancode = event->key.keysym.scancode;

      if ((key_sym >= SDLK_F1) && (key_sym <= SDLK_F12) &&
          (g_buttondown == key_sym - SDLK_F1)) {
        g_buttondown = -1;
        process_button_click(key_sym - SDLK_F1, key_mod);
      } else if (frontend_handle_key_event(key_sym, false)) {
        break;
      } else if (key_sym == SDLK_CAPSLOCK) {
        if (keyboard_get_caps_mode() == caps_mode_host) {
          const uint8_t caps = ((key_mod & KMOD_CAPS) != 0) ? 1 : 0;
          peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
        }
      } else {
        const bool extended = (key_sym >= SDLK_UP && key_sym <= SDLK_INSERT) ||
                              (key_sym == SDLK_DELETE);
        if (!joy_frontend_process_key(key_sym, extended, false, false)) {
          frontend_dispatch_key_event(scancode, key_sym, key_mod, false);
        }
      }
      break;
    }

    case SDL_MOUSEBUTTONDOWN: {
      const SDLMod key_mod = SDL_GetModState();
      if (event->button.button == SDL_BUTTON_MIDDLE) {
        set_using_cursor(!g_usingcursor);
        break;
      }
      if (event->button.button == SDL_BUTTON_LEFT) {
        if (g_buttondown == -1) {
          x_local = static_cast<int>(event->button.x);
          y_local = static_cast<int>(event->button.y);
#if ENABLE_DEBUGGER
          if (system_state.mode == app_mode_debug) {
            debugger_mouse_click(x_local, y_local);
          } else
#endif
              if (g_usingcursor) {
            if ((key_mod & (KMOD_SHIFT | KMOD_CTRL)) != 0) {
              set_using_cursor(false);
            } else {
              dispatch_mouse_button(k_mouse_button_left, true);
            }
          } else {
            bool mouse_capture_cfg = true;
            config_load_bool("Configuration", REGVALUE_MOUSE_CAPTURE,
                             &mouse_capture_cfg);
            if (mouse_capture_cfg) {
              uint8_t mouse_active = 0;
              size_t qsize = 1;
              peripheral_query(mouse_default_slot, mouse_query_is_active,
                               &mouse_active, &qsize);
              const bool mouse_in_use =
                  (mouse_active != 0) ||
                  joy_frontend_is_mouse_emulation_active();
              if (mouse_in_use && ((system_state.mode == app_mode_running) ||
                                   (system_state.mode == app_mode_stepping))) {
                set_using_cursor(true);
              }
            }
          }
        }
      } else if (event->button.button == SDL_BUTTON_RIGHT) {
        if (g_usingcursor) {
          dispatch_mouse_button(k_mouse_button_right, true);
        }
      }
      break;
    }

    case SDL_MOUSEBUTTONUP:
      if (event->button.button == SDL_BUTTON_LEFT) {
        if (g_usingcursor) {
          dispatch_mouse_button(k_mouse_button_left, false);
        }
      } else if (event->button.button == SDL_BUTTON_RIGHT) {
        if (g_usingcursor) {
          dispatch_mouse_button(k_mouse_button_right, false);
        }
      }
      break;

    case SDL_MOUSEMOTION:
      x_local = static_cast<int>(event->motion.x);
      y_local = static_cast<int>(event->motion.y);
      if (g_usingcursor) {
        uint8_t mouse_active = 0;
        size_t qsize = 1;
        peripheral_query(mouse_default_slot, mouse_query_is_active,
                         &mouse_active, &qsize);
        if (mouse_active != 0) {
          MousePosPayload_t payload{x_local, VIEWPORTCX, y_local, VIEWPORTCY};
          peripheral_command(mouse_default_slot, mouse_cmd_set_pos, &payload,
                             sizeof(payload));
        }
        if (joy_frontend_is_mouse_emulation_active()) {
          joy_frontend_process_mouse_motion(x_local, VIEWPORTCX, y_local,
                                            VIEWPORTCY);
        }
      }
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