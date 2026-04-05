#include "stdafx.h"
#include "Joystick.h"
#include <iostream>

#define  DEVICE_NONE      0
#define  DEVICE_JOYSTICK  1
#define  DEVICE_KEYBOARD  2
#define  DEVICE_MOUSE     3

#define  MODE_NONE        0
#define  MODE_STANDARD    1
#define  MODE_CENTERING   2
#define  MODE_SMOOTH      3

typedef struct _joyinforec {
  int device;
  int mode;
} joyinforec, *joyinfoptr;

static const joyinforec joyinfo[5] = {{DEVICE_NONE,     MODE_NONE},
                                      {DEVICE_JOYSTICK, MODE_STANDARD},
                                      {DEVICE_KEYBOARD, MODE_STANDARD},
                                      {DEVICE_KEYBOARD, MODE_CENTERING},
                                      {DEVICE_MOUSE,    MODE_STANDARD}};

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

const unsigned int PDL_CENTRAL = 127;
const unsigned int PDL_MAX = 255;

static bool keydown[JK_MAX] = {false};
const int PDL_SMAX = 127;
const int PDL_SCENTRAL = 0;
const int PDL_SMIN = -127;

static POINT keyvalue[9] = {{PDL_SMIN,     PDL_SMAX},
                            {PDL_SCENTRAL, PDL_SMAX},
                            {PDL_SMAX,     PDL_SMAX},
                            {PDL_SMIN,     PDL_SCENTRAL},
                            {PDL_SCENTRAL, PDL_SCENTRAL},
                            {PDL_SMAX,     PDL_SCENTRAL},
                            {PDL_SMIN,     PDL_SMIN},
                            {PDL_SCENTRAL, PDL_SMIN},
                            {PDL_SMAX,     PDL_SMIN}};

static int joyshrx[2] = {8, 8};
static int joyshry[2] = {8, 8};
static int joysubx[2] = {0, 0};
static int joysuby[2] = {0, 0};

SDL_Joystick *joy1 = NULL;
SDL_Joystick *joy2 = NULL;

void JoyFrontend_Initialize() {
  #define AXIS_MIN        -32768  /* minimum value for axis coordinate */
  #define AXIS_MAX        32767   /* maximum value for axis coordinate */

  if (joy1) {
    SDL_CloseJoystick(joy1);
    joy1 = NULL;
  }
  if (joy2) {
    SDL_CloseJoystick(joy2);
    joy2 = NULL;
  }
  int number_of_joysticks = 0;
  SDL_JoystickID *joysticks = SDL_GetJoysticks(&number_of_joysticks);

  if (joyinfo[joytype[0]].device == DEVICE_JOYSTICK) {
    if (number_of_joysticks > 0 && (int)joy1index < number_of_joysticks) {
      joy1 = SDL_OpenJoystick(joysticks[joy1index]);
      joyshrx[0] = 0;
      joyshry[0] = 0;
      joysubx[0] = AXIS_MIN;
      joysuby[0] = AXIS_MIN;
      unsigned int xrange = AXIS_MAX - AXIS_MIN;
      unsigned int yrange = AXIS_MAX - AXIS_MIN;
      while (xrange > 256) {
        xrange >>= 1;
        ++joyshrx[0];
      }
      while (yrange > 256) {
        yrange >>= 1;
        ++joyshry[0];
      }
    } else {
      joytype[0] = DEVICE_MOUSE;
    }
  }

  if (joyinfo[joytype[1]].device == DEVICE_JOYSTICK) {
    if (number_of_joysticks > 1 && (int)joy2index < number_of_joysticks) {
      joy2 = SDL_OpenJoystick(joysticks[joy2index]);
      joyshrx[1] = 0;
      joyshry[1] = 0;
      joysubx[1] = AXIS_MIN;
      joysuby[1] = AXIS_MIN;
      unsigned int xrange = AXIS_MAX - AXIS_MIN;
      unsigned int yrange = AXIS_MAX - AXIS_MIN;
      while (xrange > 256) {
        xrange >>= 1;
        ++joyshrx[1];
      }
      while (yrange > 256) {
        yrange >>= 1;
        ++joyshry[1];
      }
    } else {
      joytype[1] = DEVICE_NONE;
    }
  }
  if (joysticks) {
    SDL_free(joysticks);
  }
}

void JoyFrontend_ShutDown() {
  if (joy1) {
    SDL_CloseJoystick(joy1);
    joy1 = NULL;
  }
  if (joy2) {
    SDL_CloseJoystick(joy2);
    joy2 = NULL;
  }
}

void JoyFrontend_CheckExit() {
  if (!joy1) return;
  SDL_UpdateJoysticks();
  joyquitevent = SDL_GetJoystickButton(joy1, joyexitbutton0) && SDL_GetJoystickButton(joy1, joyexitbutton1);
}

void JoyFrontend_Update() {
  // Joystick 0
  if (joy1 && joyinfo[joytype[0]].device == DEVICE_JOYSTICK) {
    static uint32_t lastcheck = 0;
    uint32_t currtime = SDL_GetTicks();
    if (currtime - lastcheck >= 10) {
      lastcheck = currtime;
      SDL_UpdateJoysticks();

      bool b0 = SDL_GetJoystickButton(joy1, joy1button1);
      bool b1 = false;
      if (joyinfo[joytype[1]].device == DEVICE_NONE) {
        b1 = SDL_GetJoystickButton(joy1, joy1button2);
      }
      JoySetRawButton(0, b0);
      JoySetRawButton(1, b1);

      int x = (SDL_GetJoystickAxis(joy1, joy1axis0) - joysubx[0]) >> joyshrx[0];
      int y = (SDL_GetJoystickAxis(joy1, joy1axis1) - joysuby[0]) >> joyshry[0];

      // "Square" a modern analog stick
      if (y < (int)PDL_CENTRAL / 2) {
        if (x < (int)PDL_CENTRAL / 2) {
          x = x - (PDL_CENTRAL / 2 - y) / 2;
          y = y - (PDL_CENTRAL / 2 - x) / 2;
        } else if (x > (int)(PDL_CENTRAL + PDL_CENTRAL / 2)) {
          x = x + (PDL_CENTRAL / 2 - y) / 2;
          y = y - (x - (PDL_CENTRAL + PDL_CENTRAL / 2)) / 2;
        }
      } else if (y > (int)(PDL_CENTRAL + PDL_CENTRAL / 2)) {
        if (x < (int)PDL_CENTRAL / 2) {
          x = x - (y - (PDL_CENTRAL + PDL_CENTRAL / 2)) / 2;
          y = y + (PDL_CENTRAL / 2 - x) / 2;
        } else if (x > (int)(PDL_CENTRAL + PDL_CENTRAL / 2)) {
          x = x + (y - (PDL_CENTRAL + PDL_CENTRAL / 2)) / 2;
          y = y + (x - (PDL_CENTRAL + PDL_CENTRAL / 2)) / 2;
        }
      }
      if (x < 0) x = 0;
      if (x > 255) x = 255;
      if (y < 0) y = 0;
      if (y > 255) y = 255;

      JoySetRawPosition(0, x + JoyGetTrim(true), y + JoyGetTrim(false));
    }
  }

  // Joystick 1
  if (joy2 && joyinfo[joytype[1]].device == DEVICE_JOYSTICK) {
    static uint32_t lastcheck = 0;
    uint32_t currtime = SDL_GetTicks();
    if (currtime - lastcheck >= 10) {
      lastcheck = currtime;
      SDL_UpdateJoysticks();

      bool b2 = SDL_GetJoystickButton(joy2, joy2button1);
      JoySetRawButton(2, b2);
      if (joyinfo[joytype[1]].device != DEVICE_NONE) {
        JoySetRawButton(1, b2); // Remap for 2nd joystick
      }

      int x = (SDL_GetJoystickAxis(joy2, joy2axis0) - joysubx[1]) >> joyshrx[1];
      int y = (SDL_GetJoystickAxis(joy2, joy2axis1) - joysuby[1]) >> joyshry[1];

      if (x == 127 || x == 128) x += JoyGetTrim(true);
      if (y == 127 || y == 128) y += JoyGetTrim(false);
      
      if (x < 0) x = 0;
      if (x > 255) x = 255;
      if (y < 0) y = 0;
      if (y > 255) y = 255;

      JoySetRawPosition(1, x, y);
    }
  }
}

void JoyFrontend_UpdateTrimViaKey(SDL_Keycode virtkey) {
  short tx = JoyGetTrim(true);
  short ty = JoyGetTrim(false);
  switch (virtkey) {
    case SDLK_DOWN:
    case SDLK_KP_2:
      if (ty < 64) ty++;
      break;
    case SDLK_KP_4:
    case SDLK_LEFT:
      if (tx > -64) tx--;
      break;
    case SDLK_KP_6:
    case SDLK_RIGHT:
      if (tx < 64) tx++;
      break;
    case SDLK_KP_8:
    case SDLK_UP:
      if (ty > -64) ty--;
      break;
    case SDLK_KP_5:
    case SDLK_CLEAR:
      tx = ty = 0;
      break;
    default:
      break;
  }
  JoySetTrim(tx, true);
  JoySetTrim(ty, false);
}

bool JoyFrontend_ProcessKey(SDL_Keycode virtkey, bool extended, bool down, bool autorep) {
  int nJoyNum = (joyinfo[joytype[0]].device == DEVICE_KEYBOARD) ? 0 : 1;
  int nCenteringType = joyinfo[joytype[nJoyNum]].mode;

  bool keychange = !extended;
  if (!extended) {
    if ((virtkey >= SDLK_KP_1) && (virtkey <= SDLK_KP_9)) {
      keydown[virtkey - SDLK_KP_1] = down;
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
        if (joyinfo[joytype[1]].device != DEVICE_KEYBOARD) {
          JoySetRawButton(0, true);
        } else if (joyinfo[joytype[1]].device != DEVICE_NONE) {
          JoySetRawButton(2, true);
          JoySetRawButton(1, true);
        }
      } else {
         if (joyinfo[joytype[1]].device != DEVICE_KEYBOARD) {
          JoySetRawButton(0, false);
        } else if (joyinfo[joytype[1]].device != DEVICE_NONE) {
          JoySetRawButton(2, false);
          JoySetRawButton(1, false);
        }
      }
    } else if ((virtkey == SDLK_KP_PERIOD) || (virtkey == SDLK_DELETE)) {
      if (down) {
        if (joyinfo[joytype[1]].device != DEVICE_KEYBOARD) {
          JoySetRawButton(1, true);
        }
      } else {
        if (joyinfo[joytype[1]].device != DEVICE_KEYBOARD) {
          JoySetRawButton(1, false);
        }
      }
    } else if ((down && !autorep) || (nCenteringType == MODE_CENTERING)) {
      int xsum = 0, ysum = 0, keydown_count = 0;
      static int corner_convert_lookup[16] = {-1, -1, -1, 8, -1, 6, -1, -1, -1, -1, 2, -1, 0, -1, -1, -1};
      int corner_idx = ((int)(0==keydown[1])) | ((int)(0==keydown[3])<<1) | ((int)(0==keydown[5])<<2) | ((int)(0==keydown[7])<<3);
      int corner_override_idx = corner_convert_lookup[corner_idx];
      if (corner_override_idx >= 0) {
        xsum = keyvalue[corner_override_idx].x;
        ysum = keyvalue[corner_override_idx].y;
        keydown_count = 1;
      } else {
        for (int i=0; i<9; i++) {
          if (keydown[i]) {
            keydown_count++;
            xsum += keyvalue[i].x;
            ysum += keyvalue[i].y;
          }
        }
      }
      int x, y;
      if (keydown_count) {
        x = (xsum / keydown_count) + PDL_CENTRAL + JoyGetTrim(true);
        y = (ysum / keydown_count) + PDL_CENTRAL + JoyGetTrim(false);
      } else {
        x = PDL_CENTRAL + JoyGetTrim(true);
        y = PDL_CENTRAL + JoyGetTrim(false);
      }
      JoySetRawPosition(nJoyNum, x, y);
    }
  }
  return keychange;
}
