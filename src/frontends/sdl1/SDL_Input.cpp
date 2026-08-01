#include <SDL/SDL.h>

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
#include "core/Util_Path.h"
#include "frontends/sdl1/Frame.h"
#include "frontends/sdl1/Frontend.h"
#include "frontends/sdl1/JoystickFrontend.h"

// Forward declarations for functions still in Frame.cpp
extern void process_button_click(int button, int mod);
extern void frame_quick_state(int state, int mod);
extern auto is_modifier_key(SDLKey key) -> bool;
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

    case SDL_VIDEORESIZE:
      Frame_OnResize(e->resize.w, e->resize.h);
      break;

    case SDL_VIDEOEXPOSE:
      Frame_OnExpose();
      break;

    case SDL_ACTIVEEVENT:
      if (e->active.state & SDL_APPINPUTFOCUS) {
        if (e->active.gain) {
          Frame_OnFocus(true);
        } else {
          Frame_OnFocus(false);
        }
      }
      break;

    case SDL_KEYDOWN: {
      SDLKey mysym = e->key.keysym.sym;
      auto mymod = static_cast<SDLMod>(e->key.keysym.mod);
      uint8_t myscancode = e->key.keysym.scancode;

      if (frontend_handle_key_event(mysym, true)) {
        break;
      }

      if (mysym >= SDLK_0 && mysym <= SDLK_9 && (mymod & KMOD_LCTRL) != 0) {
        frame_quick_state(mysym - SDLK_0, mymod);
        break;
      }

      if ((mysym >= SDLK_F1) && (mysym <= SDLK_F12) && (g_buttondown == -1)) {
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
        uint8_t caps = ((mymod & KMOD_CAPS) != 0) ? 1 : 0;
        peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
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
      } else if (mysym == SDLK_SCROLLOCK) {
        g_scroll_lock_full_speed = !g_scroll_lock_full_speed;
      } else if ((g_state.mode == MODE_RUNNING) ||
                 (g_state.mode == MODE_LOGO) ||
                 (g_state.mode == MODE_STEPPING)) {
#if ENABLE_DEBUGGER
        g_debugger_eat_key = false;
#endif
        bool extended = (mysym >= SDLK_UP && mysym <= SDLK_INSERT) ||
                        (mysym == SDLK_DELETE);
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
      break;
    }

    case SDL_KEYUP: {
      SDLKey mysym = e->key.keysym.sym;
      auto mymod = static_cast<SDLMod>(e->key.keysym.mod);
      uint8_t myscancode = e->key.keysym.scancode;

      if ((mysym >= SDLK_F1) && (mysym <= SDLK_F12) &&
          (static_cast<int>(g_buttondown) == mysym - SDLK_F1)) {
        g_buttondown = -1;
        process_button_click(mysym - SDLK_F1, mymod);
      } else if (frontend_handle_key_event(mysym, false)) {
        break;
      } else if (mysym == SDLK_CAPSLOCK) {
        uint8_t caps = ((mymod & KMOD_CAPS) != 0) ? 1 : 0;
        peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
      } else {
        bool extended = (mysym >= SDLK_UP && mysym <= SDLK_INSERT) ||
                        (mysym == SDLK_DELETE);
        if (!joy_frontend_process_key(mysym, extended, false, false)) {
          Frontend_DispatchKeyEvent(myscancode, mysym, mymod, false);
        }
      }
      break;
    }

    case SDL_MOUSEBUTTONDOWN: {
      SDLMod mymod = SDL_GetModState();
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
              MouseButtonPayload_t payload = {0, true};
              peripheral_command(0, mouse_cmd_set_button, &payload,
                                 sizeof(payload));
            }
          } else {
            uint8_t mouse_active = 0;
            size_t qsize = 1;
            peripheral_query(4, mouse_query_is_active, &mouse_active, &qsize);
            if ((((g_state.mode == MODE_RUNNING) ||
                  (g_state.mode == MODE_STEPPING))) ||
                (mouse_active != 0)) {
              set_using_cursor(true);
            }
          }
        }
      } else if (e->button.button == SDL_BUTTON_RIGHT) {
        if (g_usingcursor) {
          MouseButtonPayload_t payload = {1, true};
          peripheral_command(0, mouse_cmd_set_button, &payload,
                             sizeof(payload));
        }
      }

      break;
    }

    case SDL_MOUSEBUTTONUP:
      if (e->button.button == SDL_BUTTON_LEFT) {
        if (g_usingcursor) {
          MouseButtonPayload_t payload = {0, false};
          peripheral_command(0, mouse_cmd_set_button, &payload,
                             sizeof(payload));
        }
      } else if (e->button.button == SDL_BUTTON_RIGHT) {
        if (g_usingcursor) {
          MouseButtonPayload_t payload = {1, false};
          peripheral_command(0, mouse_cmd_set_button, &payload,
                             sizeof(payload));
        }
      }
      break;

    case SDL_MOUSEMOTION:
      x_local = static_cast<int>(e->motion.x);
      y_local = static_cast<int>(e->motion.y);
      if (g_usingcursor) {
        MousePosPayload_t payload = {x_local, VIEWPORTCX - 4, y_local,
                                     VIEWPORTCY - 4};
        peripheral_command(0, mouse_cmd_set_pos, &payload, sizeof(payload));
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