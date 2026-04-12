#include "core/Common.h"
#include <SDL3/SDL.h>
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/Frontend.h"
#include "apple2/Keyboard.h"
#include "apple2/Joystick.h"
#include "apple2/Video.h"
#include "apple2/SoundCore.h"
#include "apple2/MouseInterface.h"
#include "frontends/sdl3/JoystickFrontend.h"
#include "Debugger/Debug.h"
#include "core/LinAppleCore.h"
#include "frontends/sdl3/DiskChoose.h"

// Forward declarations for functions still in Frame.cpp
extern void ProcessButtonClick(int button, int mod);
extern void FrameQuickState(int state, int mod);
extern bool IsModifierKey(SDL_Keycode key);
extern void SetUsingCursor(bool);
extern void DrawStatusArea(int);
extern int buttondown;
extern bool usingcursor;
extern int x, y;

void SDL_HandleEvent(SDL_Event *e) {
  int x_local, y_local;

  switch (e->type) {
    case SDL_EVENT_QUIT:
      g_state.mode = MODE_EXIT;
      break;

    case SDL_EVENT_WINDOW_RESIZED:
      Frame_OnResize(e->window.data1, e->window.data2);
      break;

    case SDL_EVENT_WINDOW_EXPOSED:
      Frame_OnExpose();
      break;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      Frame_OnFocus(true);
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      Frame_OnFocus(false);
      break;

    case SDL_EVENT_KEY_DOWN:
    {
      SDL_Keycode mysym = e->key.key;
      SDL_Keymod mymod = e->key.mod;
      SDL_Scancode myscancode = e->key.scancode;

      if (e->key.repeat == 0) {
        if (!IsModifierKey(mysym)) {
          KeybSetAnyKeyDownStatus(true);
        }

        if (Frontend_HandleKeyEvent(mysym, true)) {
          break;
        }

        if (mysym >= SDLK_0 && mysym <= SDLK_9 && mymod & SDL_KMOD_LCTRL) {
          FrameQuickState(mysym - SDLK_0, mymod);
          break;
        }

        if ((mysym >= SDLK_F1) && (mysym <= SDLK_F12) && (buttondown == -1)) {
          SetUsingCursor(false);
          buttondown = mysym - SDLK_F1;
        } else if (mysym == SDLK_KP_PLUS) {
          g_state.dwSpeed = g_state.dwSpeed + 2;
          if (g_state.dwSpeed > SPEED_MAX) {
            g_state.dwSpeed = SPEED_MAX;
          }
          printf("Now speed=%d\n", (int) g_state.dwSpeed);
          SetCurrentCLK6502();
        } else if (mysym == SDLK_KP_MINUS) {
          if (g_state.dwSpeed > SPEED_MIN) {
            g_state.dwSpeed = g_state.dwSpeed - 1;
          }
          printf("Now speed=%d\n", (int) g_state.dwSpeed);
          SetCurrentCLK6502();
        } else if (mysym == SDLK_KP_MULTIPLY) {
          g_state.dwSpeed = 10;
          printf("Now speed=%d\n", (int) g_state.dwSpeed);
          SetCurrentCLK6502();
        } else if (mysym == SDLK_CAPSLOCK) {
          Linapple_SetCapsLockState((mymod & SDL_KMOD_CAPS) != 0);
        } else if (mysym == SDLK_PAUSE) {
          SetUsingCursor(false);
          switch (g_state.mode) {
            case MODE_RUNNING:
              g_state.mode = MODE_PAUSED;
              SoundCore_SetFade(FADE_OUT);
              break;
            case MODE_PAUSED:
              g_state.mode = MODE_RUNNING;
              SoundCore_SetFade(FADE_IN);
              break;
            case MODE_STEPPING:
              DebuggerInputConsoleChar(DEBUG_EXIT_KEY);
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
        } else if (mysym == SDLK_SCROLLLOCK) {
          g_bScrollLock_FullSpeed = !g_bScrollLock_FullSpeed;
        } else if ((g_state.mode == MODE_RUNNING) || (g_state.mode == MODE_LOGO) || (g_state.mode == MODE_STEPPING)) {
          g_bDebuggerEatKey = false;
          bool extended = (myscancode >= SDL_SCANCODE_INSERT && myscancode <= SDL_SCANCODE_UP) || (myscancode == SDL_SCANCODE_DELETE);
          if (mymod & SDL_KMOD_RCTRL)
          {
            JoyFrontend_UpdateTrimViaKey(mysym);
          } else {
            if (!JoyFrontend_ProcessKey(mysym, extended, true, false)) {
              uint8_t apple_code = Frontend_TranslateKey(mysym, mymod);
              Linapple_SetKeyState(apple_code, true);
            }
          }
        } else if (g_state.mode == MODE_DEBUG) {
          uint8_t apple_code = Frontend_TranslateKey(mysym, mymod);
          if (apple_code) DebuggerProcessKey(apple_code);
        }
      }
      break;
    }

    case SDL_EVENT_KEY_UP:
    {
      SDL_Keycode mysym = e->key.key;
      SDL_Keymod mymod = e->key.mod;
      SDL_Scancode myscancode = e->key.scancode;

      if (!IsModifierKey(mysym)) {
        KeybSetAnyKeyDownStatus(false);
      }

      if ((mysym >= SDLK_F1) && (mysym <= SDLK_F12) && ((SDL_Keycode)buttondown == mysym - SDLK_F1)) {
        buttondown = -1;
        ProcessButtonClick(mysym - SDLK_F1, mymod);
      } else if (Frontend_HandleKeyEvent(mysym, false)) {
        break;
      } else if (mysym == SDLK_CAPSLOCK) {
        Linapple_SetCapsLockState((mymod & SDL_KMOD_CAPS) != 0);
      } else {
        bool extended = (myscancode >= SDL_SCANCODE_INSERT && myscancode <= SDL_SCANCODE_UP) || (myscancode == SDL_SCANCODE_DELETE);
        if (!JoyFrontend_ProcessKey(mysym, extended, false, false)) {
          uint8_t apple_code = Frontend_TranslateKey(mysym, mymod);
          Linapple_SetKeyState(apple_code, false);
        }
      }
      break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
      SDL_Keymod mymod = SDL_GetModState();
      if (e->button.button == SDL_BUTTON_LEFT) {
        if (buttondown == -1) {
          x_local = (int)e->button.x;
          y_local = (int)e->button.y;
          if (g_state.mode == MODE_DEBUG)
            DebuggerMouseClick(x_local, y_local);
          else
          if (usingcursor) {
            if (mymod & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL)) {
              SetUsingCursor(false);
            } else {
              if (Mouse_Active()) {
                Mouse_SetButton(BUTTON0, BUTTON_DOWN);
              } else {
                JoySetButton(BUTTON0, BUTTON_DOWN);
              }
            }
          }
          else
          if ((((g_state.mode == MODE_RUNNING) || (g_state.mode == MODE_STEPPING))) ||
              (Mouse_Active())) {
            SetUsingCursor(true);
          }
        }
      }
      else if (e->button.button == SDL_BUTTON_RIGHT) {
        if (usingcursor) {
          if (Mouse_Active()) {
            Mouse_SetButton(BUTTON1, BUTTON_DOWN);
          } else {
            JoySetButton(BUTTON1, BUTTON_DOWN);
          }
        }
      }

      break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (e->button.button == SDL_BUTTON_LEFT) {
        if (usingcursor) {
          if (Mouse_Active()) {
            Mouse_SetButton(BUTTON0, BUTTON_UP);
          } else {
            JoySetButton(BUTTON0, BUTTON_UP);
          }
        }
      } else if (e->button.button == SDL_BUTTON_RIGHT) {
        if (usingcursor) {
          if (Mouse_Active()) {
            Mouse_SetButton(BUTTON1, BUTTON_UP);
          } else {
            JoySetButton(BUTTON1, BUTTON_UP);
          }
        }
      }
      break;

    case SDL_EVENT_MOUSE_MOTION:
      x_local = (int)e->motion.x;
      y_local = (int)e->motion.y;
      if (usingcursor) {
        if (Mouse_Active()) {
          Mouse_SetPosition(x_local, VIEWPORTCX - 4, y_local, VIEWPORTCY - 4);
        } else {
          JoySetPosition(x_local, VIEWPORTCX - 4, y_local, VIEWPORTCY - 4);
        }
      }
      break;

    case SDL_EVENT_USER:
      if (e->user.code == 1) {
        ProcessButtonClick(BTN_RUN, SDL_KMOD_LCTRL);
      }
      break;

  }
}
