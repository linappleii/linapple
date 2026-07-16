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
extern void ProcessButtonClick(int button, int mod);
extern void FrameQuickState(int state, int mod);
extern auto IsModifierKey(SDLKey key) -> bool;
extern void SetUsingCursor(bool);
extern void DrawStatusArea(int);
extern int buttondown;
extern bool usingcursor;
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

      if (Frontend_HandleKeyEvent(mysym, true)) {
        break;
      }

      if (mysym >= SDLK_0 && mysym <= SDLK_9 && (mymod & KMOD_LCTRL) != 0) {
        FrameQuickState(mysym - SDLK_0, mymod);
        break;
      }

      if ((mysym >= SDLK_F1) && (mysym <= SDLK_F12) && (buttondown == -1)) {
        SetUsingCursor(false);
        buttondown = mysym - SDLK_F1;
      } else if (mysym == SDLK_KP_PLUS) {
        g_state.dwSpeed = g_state.dwSpeed + 2;
        if (g_state.dwSpeed > emulation_speed_max) {
          g_state.dwSpeed = emulation_speed_max;
        }
        printf("Now speed=%d\n", static_cast<int>(g_state.dwSpeed));
        SetCurrentCLK6502();
      } else if (mysym == SDLK_KP_MINUS) {
        if (g_state.dwSpeed > SPEED_MIN) {
          g_state.dwSpeed = g_state.dwSpeed - 1;
        }
        printf("Now speed=%d\n", static_cast<int>(g_state.dwSpeed));
        SetCurrentCLK6502();
      } else if (mysym == SDLK_KP_MULTIPLY) {
        constexpr uint32_t DEFAULT_SPEED = 10;
        g_state.dwSpeed = DEFAULT_SPEED;
        printf("Now speed=%d\n", static_cast<int>(g_state.dwSpeed));
        SetCurrentCLK6502();
      } else if (mysym == SDLK_CAPSLOCK) {
        uint8_t caps = ((mymod & KMOD_CAPS) != 0) ? 1 : 0;
        peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
      } else if (mysym == SDLK_PAUSE) {
        SetUsingCursor(false);
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
        DrawStatusArea(DRAW_TITLE);
        if ((g_state.mode != MODE_LOGO) && (g_state.mode != MODE_DEBUG)) {
          VideoRedrawScreen();
        }
        g_state.bResetTiming = true;
      } else if (mysym == SDLK_SCROLLOCK) {
        g_bScrollLock_FullSpeed = !g_bScrollLock_FullSpeed;
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
        LinAppleKey core_key = Frontend_ToCoreKey(mysym, mymod);
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
          (static_cast<int>(buttondown) == mysym - SDLK_F1)) {
        buttondown = -1;
        ProcessButtonClick(mysym - SDLK_F1, mymod);
      } else if (Frontend_HandleKeyEvent(mysym, false)) {
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
        if (buttondown == -1) {
          x_local = static_cast<int>(e->button.x);
          y_local = static_cast<int>(e->button.y);
#if ENABLE_DEBUGGER
          if (g_state.mode == MODE_DEBUG) {
            debugger_mouse_click(x_local, y_local);
          } else
#endif
              if (usingcursor) {
            if ((mymod & (KMOD_SHIFT | KMOD_CTRL)) != 0) {
              SetUsingCursor(false);
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
              SetUsingCursor(true);
            }
          }
        }
      } else if (e->button.button == SDL_BUTTON_RIGHT) {
        if (usingcursor) {
          MouseButtonPayload_t payload = {1, true};
          peripheral_command(0, mouse_cmd_set_button, &payload,
                             sizeof(payload));
        }
      }

      break;
    }

    case SDL_MOUSEBUTTONUP:
      if (e->button.button == SDL_BUTTON_LEFT) {
        if (usingcursor) {
          MouseButtonPayload_t payload = {0, false};
          peripheral_command(0, mouse_cmd_set_button, &payload,
                             sizeof(payload));
        }
      } else if (e->button.button == SDL_BUTTON_RIGHT) {
        if (usingcursor) {
          MouseButtonPayload_t payload = {1, false};
          peripheral_command(0, mouse_cmd_set_button, &payload,
                             sizeof(payload));
        }
      }
      break;

    case SDL_MOUSEMOTION:
      x_local = static_cast<int>(e->motion.x);
      y_local = static_cast<int>(e->motion.y);
      if (usingcursor) {
        MousePosPayload_t payload = {x_local, VIEWPORTCX - 4, y_local,
                                     VIEWPORTCY - 4};
        peripheral_command(0, mouse_cmd_set_pos, &payload, sizeof(payload));
      }
      break;

    case SDL_USEREVENT:
      if (e->user.code == 1) {
        ProcessButtonClick(BTN_RUN, KMOD_LCTRL);
      }
      break;

    default:
      break;
  }
}