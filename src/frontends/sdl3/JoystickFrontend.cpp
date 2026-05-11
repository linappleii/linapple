#include "core/Common.h"
#include <iostream>
#include <cstring>
#include <array>
#include "apple2/peripherals/JoystickCommands.h"
#include "SDL3/SDL.h"
#include "apple2/Structs.h"
#include "core/Log.h"
#include "core/Common_Globals.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "frontends/sdl3/JoystickFrontend.h"

enum {
DEVICE_NONE =      0,
DEVICE_JOYSTICK =  1,
DEVICE_KEYBOARD =  2,
DEVICE_MOUSE =     3
};

enum {
MODE_NONE =        0,
MODE_STANDARD =    1,
MODE_CENTERING =   2,
MODE_SMOOTH =      3
};

using joyinforec = struct joyinforec {
  int device;
  int mode;
};

static const std::array<joyinforec, 5> joyinfo = {{
  {DEVICE_NONE,     MODE_NONE},
  {DEVICE_JOYSTICK, MODE_STANDARD},
  {DEVICE_KEYBOARD, MODE_STANDARD},
  {DEVICE_KEYBOARD, MODE_CENTERING},
  {DEVICE_MOUSE,    MODE_STANDARD}
}};

// Key pad [1..9]; Key pad 0,Key pad '.'; Left ALT,Right ALT
enum JOYKEY {
  JK_DOWNLEFT = 0,
  JK_DOWN,
  JK_DOWNRIGHT,
  JK_LEFT,
  JK_CENTRE,
  JK_RIGHT,
  JK_UPLEFT,
  JK_UP,
  JK_UPRIGHT,
  JK_BUTTON0,
  JK_BUTTON1,
  JK_OPENAPPLE,
  JK_CLOSEDAPPLE,
  JK_MAX
};

const uint32_t PDL_CENTRAL = 127;
const uint32_t PDL_MAX = 255;

static std::array<bool, JK_MAX> keydown = {false};
const int PDL_SMAX = 127;
const int PDL_SCENTRAL = 0;
const int PDL_SMIN = -127;

static std::array<Point_t, 9> keyvalue = {{
  {PDL_SMIN,     PDL_SMAX},
  {PDL_SCENTRAL, PDL_SMAX},
  {PDL_SMAX,     PDL_SMAX},
  {PDL_SMIN,     PDL_SCENTRAL},
  {PDL_SCENTRAL, PDL_SCENTRAL},
  {PDL_SMAX,     PDL_SCENTRAL},
  {PDL_SMIN,     PDL_SMIN},
  {PDL_SCENTRAL, PDL_SMIN},
  {PDL_SMAX,     PDL_SMIN}
}};

static std::array<int, 2> joyshrx = {8, 8};
static std::array<int, 2> joyshry = {8, 8};
static std::array<int, 2> joysubx = {0, 0};
static std::array<int, 2> joysuby = {0, 0};

static SDL_Joystick *joy1 = nullptr;
static SDL_Joystick *joy2 = nullptr;

static int g_frontend_pdl_trim_x = 0;
static int g_frontend_pdl_trim_y = 0;

static JoystickConfig_t g_joyConfig;

void JoyFrontend_Initialize() {
  #define AXIS_MIN        -32768  /* minimum value for axis coordinate */
  #define AXIS_MAX        32767   /* maximum value for axis coordinate */

  if (joy1) {
    SDL_CloseJoystick(joy1);
    joy1 = nullptr;
  }
  if (joy2) {
    SDL_CloseJoystick(joy2);
    joy2 = nullptr;
  }

  // Load config from registry
  memset(&g_joyConfig, 0, sizeof(g_joyConfig));
  uint32_t val = 0;
  if (LOAD(REGVALUE_JOY_TYPE1, &val)) g_joyConfig.joytype[0] = val;
  if (LOAD(REGVALUE_JOY_TYPE2, &val)) g_joyConfig.joytype[1] = val;
  if (LOAD(REGVALUE_JOY_INDEX1, &val)) g_joyConfig.joyindex[0] = val;
  if (LOAD(REGVALUE_JOY_INDEX2, &val)) g_joyConfig.joyindex[1] = val;
  if (LOAD(REGVALUE_JOY_BUTTON1_1, &val)) g_joyConfig.joybutton[0] = val;
  if (LOAD(REGVALUE_JOY_BUTTON1_2, &val)) g_joyConfig.joybutton[1] = val;
  if (LOAD(REGVALUE_JOY_BUTTON2_1, &val)) g_joyConfig.joy2button1 = val;
  if (LOAD(REGVALUE_JOY_AXIS1_0, &val)) g_joyConfig.joyaxis[0][0] = val;
  if (LOAD(REGVALUE_JOY_AXIS1_1, &val)) g_joyConfig.joyaxis[0][1] = val;
  if (LOAD(REGVALUE_JOY_AXIS2_0, &val)) g_joyConfig.joyaxis[1][0] = val;
  if (LOAD(REGVALUE_JOY_AXIS2_1, &val)) g_joyConfig.joyaxis[1][1] = val;
  if (LOAD(REGVALUE_JOY_EXIT_ENABLE, &val)) g_joyConfig.joyexitenable = val;
  if (LOAD(REGVALUE_JOY_EXIT_BUTTON0, &val)) g_joyConfig.joyexitbutton[0] = val;
  if (LOAD(REGVALUE_JOY_EXIT_BUTTON1, &val)) g_joyConfig.joyexitbutton[1] = val;

  // Sync to peripheral
  Peripheral_Command(0, JOY_CMD_SET_CONFIG, &g_joyConfig, sizeof(g_joyConfig));

  int number_of_joysticks = 0;
  SDL_JoystickID *joysticks = SDL_GetJoysticks(&number_of_joysticks);

  if (joyinfo[g_joyConfig.joytype[0]].device == DEVICE_JOYSTICK) {
    if (number_of_joysticks > 0 && static_cast<int>(g_joyConfig.joyindex[0]) < number_of_joysticks) {
      joy1 = SDL_OpenJoystick(joysticks[g_joyConfig.joyindex[0]]);
      joyshrx[0] = 0;
      joyshry[0] = 0;
      joysubx[0] = AXIS_MIN;
      joysuby[0] = AXIS_MIN;
      uint32_t xrange = AXIS_MAX - AXIS_MIN;
      uint32_t yrange = AXIS_MAX - AXIS_MIN;
      while (xrange > 256) {
        xrange >>= 1;
        ++joyshrx[0];
      }
      while (yrange > 256) {
        yrange >>= 1;
        ++joyshry[0];
      }
    } else {
      g_joyConfig.joytype[0] = DEVICE_MOUSE;
    }
  }

  if (joyinfo[g_joyConfig.joytype[1]].device == DEVICE_JOYSTICK) {
    if (number_of_joysticks > 1 && static_cast<int>(g_joyConfig.joyindex[1]) < number_of_joysticks) {
      joy2 = SDL_OpenJoystick(joysticks[g_joyConfig.joyindex[1]]);
      joyshrx[1] = 0;
      joyshry[1] = 0;
      joysubx[1] = AXIS_MIN;
      joysuby[1] = AXIS_MIN;
      uint32_t xrange = AXIS_MAX - AXIS_MIN;
      uint32_t yrange = AXIS_MAX - AXIS_MIN;
      while (xrange > 256) {
        xrange >>= 1;
        ++joyshrx[1];
      }
      while (yrange > 256) {
        yrange >>= 1;
        ++joyshry[1];
      }
    } else {
      g_joyConfig.joytype[1] = DEVICE_NONE;
    }
  }
  if (joysticks) {
    SDL_free(joysticks);
  }
}

void JoyFrontend_ShutDown() {
  if (joy1) {
    SDL_CloseJoystick(joy1);
    joy1 = nullptr;
  }
  if (joy2) {
    SDL_CloseJoystick(joy2);
    joy2 = nullptr;
  }
}

void JoyFrontend_CheckExit() {
  if (!joy1 || !g_joyConfig.joyexitenable) return;
  SDL_UpdateJoysticks();
  bool quit = SDL_GetJoystickButton(joy1, static_cast<int>(g_joyConfig.joyexitbutton[0])) && 
              SDL_GetJoystickButton(joy1, static_cast<int>(g_joyConfig.joyexitbutton[1]));
  
  // We can push this back to the peripheral via a command if needed, but for now we just use a local bool if it's strictly for frontend exit.
  // Wait, the core might need to know if we want to quit.
  if (quit) {
    g_state.mode = MODE_EXIT;
  }
}

void JoyFrontend_Update() {
  // Joystick 0
  if (joy1 && joyinfo[g_joyConfig.joytype[0]].device == DEVICE_JOYSTICK) {
    static uint32_t lastcheck = 0;
    uint32_t currtime = SDL_GetTicks();
    if (currtime - lastcheck >= 10) {
      lastcheck = currtime;
      SDL_UpdateJoysticks();

      bool b0 = SDL_GetJoystickButton(joy1, static_cast<int>(g_joyConfig.joybutton[0]));
      bool b1 = false;
      if (joyinfo[g_joyConfig.joytype[1]].device == DEVICE_NONE) {
        b1 = SDL_GetJoystickButton(joy1, static_cast<int>(g_joyConfig.joybutton[1]));
      }
      
      JoystickButtonPayload_t pb0 = {0, b0};
      Peripheral_Command(0, JOY_CMD_SET_BUTTON, &pb0, sizeof(pb0));
      JoystickButtonPayload_t pb1 = {1, b1};
      Peripheral_Command(0, JOY_CMD_SET_BUTTON, &pb1, sizeof(pb1));

      int x = (static_cast<int>(SDL_GetJoystickAxis(joy1, static_cast<int>(g_joyConfig.joyaxis[0][0]))) - joysubx[0]) >> joyshrx[0];
      int y = (static_cast<int>(SDL_GetJoystickAxis(joy1, static_cast<int>(g_joyConfig.joyaxis[0][1]))) - joysuby[0]) >> joyshry[0];

      // "Square" a modern analog stick
      if (y < static_cast<int>(PDL_CENTRAL) / 2) {
        if (x < static_cast<int>(PDL_CENTRAL) / 2) {
          x = x - (static_cast<int>(PDL_CENTRAL) / 2 - y) / 2;
          y = y - (static_cast<int>(PDL_CENTRAL) / 2 - x) / 2;
        } else if (x > static_cast<int>(PDL_CENTRAL) + static_cast<int>(PDL_CENTRAL) / 2) {
          x = x + (static_cast<int>(PDL_CENTRAL) / 2 - y) / 2;
          y = y - (x - (static_cast<int>(PDL_CENTRAL) + static_cast<int>(PDL_CENTRAL) / 2)) / 2;
        }
      } else if (y > static_cast<int>(PDL_CENTRAL) + static_cast<int>(PDL_CENTRAL) / 2) {
        if (x < static_cast<int>(PDL_CENTRAL) / 2) {
          x = x - (y - (static_cast<int>(PDL_CENTRAL) + static_cast<int>(PDL_CENTRAL) / 2)) / 2;
          y = y + (static_cast<int>(PDL_CENTRAL) / 2 - x) / 2;
        } else if (x > static_cast<int>(PDL_CENTRAL) + static_cast<int>(PDL_CENTRAL) / 2) {
          x = x + (y - (static_cast<int>(PDL_CENTRAL) + static_cast<int>(PDL_CENTRAL) / 2)) / 2;
          y = y + (x - (static_cast<int>(PDL_CENTRAL) + static_cast<int>(PDL_CENTRAL) / 2)) / 2;
        }
      }
      if (x < 0) x = 0;
      if (x > 255) x = 255;
      if (y < 0) y = 0;
      if (y > 255) y = 255;

      JoystickAxisPayload_t px = {0, 0, static_cast<uint8_t>(x + g_frontend_pdl_trim_x)};
      Peripheral_Command(0, JOY_CMD_SET_AXIS, &px, sizeof(px));
      JoystickAxisPayload_t py = {0, 1, static_cast<uint8_t>(y + g_frontend_pdl_trim_y)};
      Peripheral_Command(0, JOY_CMD_SET_AXIS, &py, sizeof(py));
    }
  }

  // Joystick 1
  if (joy2 && joyinfo[g_joyConfig.joytype[1]].device == DEVICE_JOYSTICK) {
    static uint32_t lastcheck = 0;
    uint32_t currtime = SDL_GetTicks();
    if (currtime - lastcheck >= 10) {
      lastcheck = currtime;
      SDL_UpdateJoysticks();

      bool b2 = SDL_GetJoystickButton(joy2, static_cast<int>(g_joyConfig.joy2button1));
      JoystickButtonPayload_t pb2 = {2, b2};
      Peripheral_Command(0, JOY_CMD_SET_BUTTON, &pb2, sizeof(pb2));
      if (joyinfo[g_joyConfig.joytype[1]].device != DEVICE_NONE) {
        JoystickButtonPayload_t pb1 = {1, b2};
        Peripheral_Command(0, JOY_CMD_SET_BUTTON, &pb1, sizeof(pb1));
      }

      int x = (static_cast<int>(SDL_GetJoystickAxis(joy2, static_cast<int>(g_joyConfig.joyaxis[1][0]))) - joysubx[1]) >> joyshrx[1];
      int y = (static_cast<int>(SDL_GetJoystickAxis(joy2, static_cast<int>(g_joyConfig.joyaxis[1][1]))) - joysuby[1]) >> joyshry[1];

      if (x == 127 || x == 128) x += g_frontend_pdl_trim_x;
      if (y == 127 || y == 128) y += g_frontend_pdl_trim_y;

      if (x < 0) x = 0;
      if (x > 255) x = 255;
      if (y < 0) y = 0;
      if (y > 255) y = 255;

      JoystickAxisPayload_t px = {1, 0, static_cast<uint8_t>(x)};
      Peripheral_Command(0, JOY_CMD_SET_AXIS, &px, sizeof(px));
      JoystickAxisPayload_t py = {1, 1, static_cast<uint8_t>(y)};
      Peripheral_Command(0, JOY_CMD_SET_AXIS, &py, sizeof(py));
    }
  }
}

void JoyFrontend_UpdateTrimViaKey(SDL_Keycode virtkey) {
  switch (virtkey) {
    case SDLK_DOWN:
    case SDLK_KP_2:
      if (g_frontend_pdl_trim_y < 64) g_frontend_pdl_trim_y++;
      break;
    case SDLK_KP_4:
    case SDLK_LEFT:
      if (g_frontend_pdl_trim_x > -64) g_frontend_pdl_trim_x--;
      break;
    case SDLK_KP_6:
    case SDLK_RIGHT:
      if (g_frontend_pdl_trim_x < 64) g_frontend_pdl_trim_x++;
      break;
    case SDLK_KP_8:
    case SDLK_UP:
      if (g_frontend_pdl_trim_y > -64) g_frontend_pdl_trim_y--;
      break;
    case SDLK_KP_5:
    case SDLK_CLEAR:
      g_frontend_pdl_trim_x = g_frontend_pdl_trim_y = 0;
      break;
    default:
      break;
  }
}

auto JoyFrontend_ProcessKey(SDL_Keycode virtkey, bool extended, bool down, bool autorep) -> bool {
  int nJoyNum = (joyinfo[g_joyConfig.joytype[0]].device == DEVICE_KEYBOARD) ? 0 : 1;
  int nCenteringType = joyinfo[g_joyConfig.joytype[static_cast<size_t>(nJoyNum)]].mode;

  bool keychange = !extended;
  if (!extended) {
    if ((virtkey >= SDLK_KP_1) && (virtkey <= SDLK_KP_9)) {
      keydown[static_cast<size_t>(virtkey - SDLK_KP_1)] = down;
    } else {
      switch (virtkey) {
        case SDLK_KP_1: case SDLK_END:      keydown[0] = down; break;
        case SDLK_KP_2: case SDLK_DOWN:     keydown[1] = down; break;
        case SDLK_KP_3: case SDLK_PAGEDOWN: keydown[2] = down; break;
        case SDLK_KP_4: case SDLK_LEFT:     keydown[3] = down; break;
        case SDLK_KP_5: case SDLK_CLEAR:    keydown[4] = down; break;
        case SDLK_KP_6: case SDLK_RIGHT:    keydown[5] = down; break;
        case SDLK_KP_7: case SDLK_HOME:     keydown[6] = down; break;
        case SDLK_KP_8: case SDLK_UP:       keydown[7] = down; break;
        case SDLK_KP_9: case SDLK_PAGEUP:   keydown[8] = down; break;
        case SDLK_KP_0: case SDLK_INSERT:   keydown[9] = down; break;
        case SDLK_KP_PERIOD: case SDLK_DELETE: keydown[10] = down; break;
        default: keychange = false; break;
      }
    }
  }

  if (keychange) {
    if ((virtkey == SDLK_KP_0) || (virtkey == SDLK_INSERT)) {
      if (down) {
        if (joyinfo[g_joyConfig.joytype[1]].device != DEVICE_KEYBOARD) {
          JoystickButtonPayload_t p = {0, true};
          Peripheral_Command(0, JOY_CMD_SET_BUTTON, &p, sizeof(p));
        } else if (joyinfo[g_joyConfig.joytype[1]].device != DEVICE_NONE) {
          JoystickButtonPayload_t p2 = {2, true};
          Peripheral_Command(0, JOY_CMD_SET_BUTTON, &p2, sizeof(p2));
          JoystickButtonPayload_t p1 = {1, true};
          Peripheral_Command(0, JOY_CMD_SET_BUTTON, &p1, sizeof(p1));
        }
      } else {
         if (joyinfo[g_joyConfig.joytype[1]].device != DEVICE_KEYBOARD) {
          JoystickButtonPayload_t p = {0, false};
          Peripheral_Command(0, JOY_CMD_SET_BUTTON, &p, sizeof(p));
        } else if (joyinfo[g_joyConfig.joytype[1]].device != DEVICE_NONE) {
          JoystickButtonPayload_t p2 = {2, false};
          Peripheral_Command(0, JOY_CMD_SET_BUTTON, &p2, sizeof(p2));
          JoystickButtonPayload_t p1 = {1, false};
          Peripheral_Command(0, JOY_CMD_SET_BUTTON, &p1, sizeof(p1));
        }
      }
    } else if ((virtkey == SDLK_KP_PERIOD) || (virtkey == SDLK_DELETE)) {
      if (down) {
        if (joyinfo[g_joyConfig.joytype[1]].device != DEVICE_KEYBOARD) {
          JoystickButtonPayload_t p = {1, true};
          Peripheral_Command(0, JOY_CMD_SET_BUTTON, &p, sizeof(p));
        }
      } else {
        if (joyinfo[g_joyConfig.joytype[1]].device != DEVICE_KEYBOARD) {
          JoystickButtonPayload_t p = {1, false};
          Peripheral_Command(0, JOY_CMD_SET_BUTTON, &p, sizeof(p));
        }
      }
    } else if ((down && !autorep) || (nCenteringType == MODE_CENTERING)) {
      int xsum = 0, ysum = 0, keydown_count = 0;
      static std::array<int, 16> corner_convert_lookup = {{-1, -1, -1, 8, -1, 6, -1, -1, -1, -1, 2, -1, 0, -1, -1, -1}};
      int corner_idx = (static_cast<int>(0==keydown[1])) | (static_cast<int>(0==keydown[3])<<1) | (static_cast<int>(0==keydown[5])<<2) | (static_cast<int>(0==keydown[7])<<3);
      int corner_override_idx = corner_convert_lookup[static_cast<size_t>(corner_idx)];
      if (corner_override_idx >= 0) {
        xsum = keyvalue[static_cast<size_t>(corner_override_idx)].x;
        ysum = keyvalue[static_cast<size_t>(corner_override_idx)].y;
        keydown_count = 1;
      } else {
        for (int i=0; i<9; i++) {
          if (keydown[static_cast<size_t>(i)]) {
            keydown_count++;
            xsum += keyvalue[static_cast<size_t>(i)].x;
            ysum += keyvalue[static_cast<size_t>(i)].y;
          }
        }
      }
      int x = 0, y = 0;
      if (keydown_count) {
        x = (xsum / keydown_count) + static_cast<int>(PDL_CENTRAL) + g_frontend_pdl_trim_x;
        y = (ysum / keydown_count) + static_cast<int>(PDL_CENTRAL) + g_frontend_pdl_trim_y;
      } else {
        x = static_cast<int>(PDL_CENTRAL) + g_frontend_pdl_trim_x;
        y = static_cast<int>(PDL_CENTRAL) + g_frontend_pdl_trim_y;
      }
      JoystickAxisPayload_t px = {static_cast<uint8_t>(nJoyNum), 0, static_cast<uint8_t>(x)};
      Peripheral_Command(0, JOY_CMD_SET_AXIS, &px, sizeof(px));
      JoystickAxisPayload_t py = {static_cast<uint8_t>(nJoyNum), 1, static_cast<uint8_t>(y)};
      Peripheral_Command(0, JOY_CMD_SET_AXIS, &py, sizeof(py));
    }
  }
  return keychange;
}
