#include <SDL2/SDL.h>

#include "Debugger/Debug.h"
#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/AudioMixer.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/sdl2/Frame.h"
#include "frontends/sdl2/Frontend.h"
#include "frontends/sdl2/JoystickFrontend.h"

// Forward declarations for functions still in Frame.cpp
extern void process_button_click(int button, int mod);
extern void frame_quick_state(int state, int mod);
extern auto is_modifier_key(SDL_Keycode key) -> bool;
extern void set_using_cursor(bool);
extern void draw_status_area(int);
extern int g_buttondown;
extern bool g_usingcursor;
extern int x, y;

void sdl_handle_event(SDL_Event* e) {
  int x_local = 0;
  int y_local = 0;

  switch (e->type) {
    case SDL_QUIT:
      g_state.mode = MODE_EXIT;
      break;

    case SDL_WINDOWEVENT:
      switch (e->window.event) {
        case SDL_WINDOWEVENT_RESIZED:
          Frame_OnResize(e->window.data1, e->window.data2);
          break;

        case SDL_WINDOWEVENT_EXPOSED:
          Frame_OnExpose();
          break;

        case SDL_WINDOWEVENT_FOCUS_GAINED:
          Frame_OnFocus(true);
          break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
          Frame_OnFocus(false);
          break;
        default:
          break;
      }
      break;

    case SDL_KEYDOWN: {
      SDL_Keycode mysym = e->key.keysym.sym;
      auto mymod = static_cast<SDL_Keymod>(e->key.keysym.mod);
      SDL_Scancode myscancode = e->key.keysym.scancode;

      if (e->key.repeat == 0) {
        if (frontend_handle_key_event(mysym, true)) {
          break;
        }

        int qs_slot = 0;
        bool qs_is_save = false;
        if (keyboard_is_quicksave_combo(mysym, mymod, &qs_slot, &qs_is_save)) {
          frame_quick_state(qs_slot, qs_is_save ? KMOD_SHIFT : 0);
          break;
        }

        if (keyboard_get_hotkeys_enabled() && (mysym >= SDLK_F1) &&
            (mysym <= SDLK_F12) && (g_buttondown == -1)) {
          set_using_cursor(false);
          g_buttondown = mysym - SDLK_F1;
        } else if (mysym == SDLK_KP_PLUS) {
          g_state.speed = g_state.speed + 2;
          if (g_state.speed > emulation_speed_max) {
            g_state.speed = emulation_speed_max;
          }
          printf("Now speed=%d\n", static_cast<int>(g_state.speed));
          set_current_clk_6502();
        } else if (mysym == SDLK_KP_MINUS) {
          if (g_state.speed > SPEED_MIN) {
            g_state.speed = g_state.speed - 1;
          }
          printf("Now speed=%d\n", static_cast<int>(g_state.speed));
          set_current_clk_6502();
        } else if (mysym == SDLK_KP_MULTIPLY) {
          constexpr uint32_t default_speed = 10;
          g_state.speed = default_speed;
          printf("Now speed=%d\n", static_cast<int>(g_state.speed));
          set_current_clk_6502();
        } else if (mysym == SDLK_CAPSLOCK) {
          if (keyboard_get_caps_mode() == CAPS_MODE_HOST) {
            uint8_t caps = ((mymod & KMOD_CAPS) != 0) ? 1 : 0;
            peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
          } else {
            linapple_toggle_caps_lock_state();
          }
        } else if (mysym == SDLK_PAUSE) {
          set_using_cursor(false);
          switch (g_state.mode) {
            case MODE_RUNNING:
              g_state.mode = MODE_PAUSED;
              audio_mixer_set_fade(fade_out);
              break;
            case MODE_PAUSED:
              g_state.mode = MODE_RUNNING;
              audio_mixer_set_fade(fade_in);
              break;
            case MODE_STEPPING:
#if ENABLE_DEBUGGER
              debugger_input_console_char(DEBUG_EXIT_KEY);
#endif
              break;
            case MODE_LOGO:
            case MODE_DEBUG:
            default:
              break;
          }
          draw_status_area(DRAW_TITLE);
          if ((g_state.mode != MODE_LOGO) && (g_state.mode != MODE_DEBUG)) {
            video_redraw_screen();
          }
          g_state.reset_timing = true;
        } else if (mysym == SDLK_SCROLLLOCK) {
          g_scroll_lock_full_speed = !g_scroll_lock_full_speed;
        } else if ((g_state.mode == MODE_RUNNING) ||
                   (g_state.mode == MODE_LOGO) ||
                   (g_state.mode == MODE_STEPPING)) {
#if ENABLE_DEBUGGER
          g_debugger_eat_key = false;
#endif
          bool extended = (myscancode >= SDL_SCANCODE_INSERT &&
                           myscancode <= SDL_SCANCODE_UP) ||
                          (myscancode == SDL_SCANCODE_DELETE);
          if ((mymod & KMOD_RCTRL) != 0) {
            JoyFrontend_UpdateTrimViaKey(mysym);
          } else {
            if (!joy_frontend_process_key(mysym, extended, true, false)) {
              Frontend_DispatchKeyEvent(myscancode, mysym, mymod, true);
            }
          }
#if ENABLE_DEBUGGER
        } else if (g_state.mode == MODE_DEBUG) {
          LinAppleKey core_key = frontend_to_core_key(mysym, mymod);
          if (core_key != LINAPPLE_KEY_UNKNOWN) {
            debugger_process_key(core_key);
          }
#endif
        }
      }
      break;
    }

    case SDL_KEYUP: {
      SDL_Keycode mysym = e->key.keysym.sym;
      auto mymod = static_cast<SDL_Keymod>(e->key.keysym.mod);
      SDL_Scancode myscancode = e->key.keysym.scancode;

      if ((mysym >= SDLK_F1) && (mysym <= SDLK_F12) &&
          (static_cast<SDL_Keycode>(g_buttondown) == mysym - SDLK_F1)) {
        g_buttondown = -1;
        process_button_click(mysym - SDLK_F1, mymod);
      } else if (frontend_handle_key_event(mysym, false)) {
        break;
      } else if (mysym == SDLK_CAPSLOCK) {
        if (keyboard_get_caps_mode() == CAPS_MODE_HOST) {
          uint8_t caps = ((mymod & KMOD_CAPS) != 0) ? 1 : 0;
          peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
        }
      } else {
        bool extended = (myscancode >= SDL_SCANCODE_INSERT &&
                         myscancode <= SDL_SCANCODE_UP) ||
                        (myscancode == SDL_SCANCODE_DELETE);
        if (!joy_frontend_process_key(mysym, extended, false, false)) {
          Frontend_DispatchKeyEvent(myscancode, mysym, mymod, false);
        }
      }
      break;
    }

    case SDL_MOUSEBUTTONDOWN: {
      SDL_Keymod mymod = SDL_GetModState();
      if (e->button.button == SDL_BUTTON_MIDDLE) {
        set_using_cursor(!g_usingcursor);
        break;
      }
      if (e->button.button == SDL_BUTTON_LEFT) {
        if (g_buttondown == -1) {
          x_local = static_cast<int>(e->button.x);
          y_local = static_cast<int>(e->button.y);
#if ENABLE_DEBUGGER
          if (g_state.mode == MODE_DEBUG) {
            debugger_mouse_click(x_local, y_local);
          } else
#endif
              if (g_usingcursor) {
            if ((mymod & (KMOD_SHIFT | KMOD_CTRL)) != 0) {
              set_using_cursor(false);
            } else {
              uint8_t mouse_active = 0;
              size_t qsize = 1;
              peripheral_query(4, mouse_query_is_active, &mouse_active, &qsize);
              if (mouse_active != 0) {
                MouseButtonPayload_t payload = {0, true};
                peripheral_command(4, mouse_cmd_set_button, &payload,
                                   sizeof(payload));
              }
              if (JoyFrontend_IsMouseEmulationActive()) {
                JoyFrontend_ProcessMouseButton(0, true);
              }
            }
          } else {
            bool mouse_capture_cfg = true;
            config_load_bool("Configuration", REGVALUE_MOUSE_CAPTURE,
                             &mouse_capture_cfg);
            if (mouse_capture_cfg) {
              uint8_t mouse_active = 0;
              size_t qsize = 1;
              peripheral_query(4, mouse_query_is_active, &mouse_active, &qsize);
              bool mouse_in_use =
                  (mouse_active != 0) || JoyFrontend_IsMouseEmulationActive();
              if (mouse_in_use && ((g_state.mode == MODE_RUNNING) ||
                                   (g_state.mode == MODE_STEPPING))) {
                set_using_cursor(true);
              }
            }
          }
        }
      } else if (e->button.button == SDL_BUTTON_RIGHT) {
        if (g_usingcursor) {
          uint8_t mouse_active = 0;
          size_t qsize = 1;
          peripheral_query(4, mouse_query_is_active, &mouse_active, &qsize);
          if (mouse_active != 0) {
            MouseButtonPayload_t payload = {1, true};
            peripheral_command(4, mouse_cmd_set_button, &payload,
                               sizeof(payload));
          }
          if (JoyFrontend_IsMouseEmulationActive()) {
            JoyFrontend_ProcessMouseButton(1, true);
          }
        }
      }

      break;
    }

    case SDL_MOUSEBUTTONUP:
      if (e->button.button == SDL_BUTTON_LEFT) {
        if (g_usingcursor) {
          uint8_t mouse_active = 0;
          size_t qsize = 1;
          peripheral_query(4, mouse_query_is_active, &mouse_active, &qsize);
          if (mouse_active != 0) {
            MouseButtonPayload_t payload = {0, false};
            peripheral_command(4, mouse_cmd_set_button, &payload,
                               sizeof(payload));
          }
          if (JoyFrontend_IsMouseEmulationActive()) {
            JoyFrontend_ProcessMouseButton(0, false);
          }
        }
      } else if (e->button.button == SDL_BUTTON_RIGHT) {
        if (g_usingcursor) {
          uint8_t mouse_active = 0;
          size_t qsize = 1;
          peripheral_query(4, mouse_query_is_active, &mouse_active, &qsize);
          if (mouse_active != 0) {
            MouseButtonPayload_t payload = {1, false};
            peripheral_command(4, mouse_cmd_set_button, &payload,
                               sizeof(payload));
          }
          if (JoyFrontend_IsMouseEmulationActive()) {
            JoyFrontend_ProcessMouseButton(1, false);
          }
        }
      }
      break;

    case SDL_MOUSEMOTION:
      x_local = static_cast<int>(e->motion.x);
      y_local = static_cast<int>(e->motion.y);
      if (g_usingcursor) {
        uint8_t mouse_active = 0;
        size_t qsize = 1;
        peripheral_query(4, mouse_query_is_active, &mouse_active, &qsize);
        if (mouse_active != 0) {
          MousePosPayload_t payload = {x_local, VIEWPORTCX, y_local,
                                       VIEWPORTCY};
          peripheral_command(4, mouse_cmd_set_pos, &payload, sizeof(payload));
        }
        if (JoyFrontend_IsMouseEmulationActive()) {
          JoyFrontend_ProcessMouseMotion(x_local, VIEWPORTCX, y_local,
                                         VIEWPORTCY);
        }
      }
      break;

    case SDL_USEREVENT:
      if (e->user.code == 1) {
        process_button_click(btn_run, KMOD_LCTRL);
      }
      break;

    default:
      break;
  }
}
