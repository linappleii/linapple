#include "Common.h"
#include <iostream>
#include <cstdint>
#include "apple2/Joystick.h"
#include "Structs.h"
#include "apple2/Memory.h"
#include "apple2/CPU.h"
#include "Common_Globals.h"
#include "Log.h"

#define  BUTTONTIME       5000

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

unsigned int joytype[2] = {DEVICE_JOYSTICK, DEVICE_NONE};  // Emulation Type for joysticks #0 & #1

static uint32_t buttonlatch[3] = {0, 0, 0};
static bool joybutton[3] = {false, false, false};

static int xpos[2] = {127, 127};
static int ypos[2] = {127, 127};

static uint64_t g_nJoyCntrResetCycle = 0;  // Abs cycle that joystick counters were reset

static int g_nPdlTrimX = 0;
static int g_nPdlTrimY = 0;

unsigned int joy1index = 0;
unsigned int joy2index = 1;
unsigned int joy1button1 = 0;
unsigned int joy1button2 = 1;
unsigned int joy2button1 = 0;
unsigned int joy1axis0 = 0;
unsigned int joy1axis1 = 1;
unsigned int joy2axis0 = 0;
unsigned int joy2axis1 = 1;
unsigned int joyexitenable = 0;
unsigned int joyexitbutton0 = 8;
unsigned int joyexitbutton1 = 9;
bool joyquitevent = false;

// All globally accessible functions are below this line

void JoyShutDown() {
}

void JoyInitialize() {
}

void JoyReset() {
  for (int i = 0; i < 3; i++) {
    buttonlatch[i] = 0;
    joybutton[i] = false;
  }
  for (int i = 0; i < 2; i++) {
    xpos[i] = 127;
    ypos[i] = 127;
  }
}

unsigned char JoyReadButton(unsigned short, unsigned short address, unsigned char, unsigned char, uint32_t nCyclesLeft) {
  address &= 0xFF;
  bool pressed = false;
  int idx = address - 0x61;
  if (idx >= 0 && idx < 3) {
    pressed = (buttonlatch[idx] || joybutton[idx]);
    buttonlatch[idx] = 0;
  }
  return MemReadFloatingBus(pressed, nCyclesLeft);
}

// PREAD:    ; $FB1E
//  AD 70 C0 : (4) LDA $C070
//  A0 00    : (2) LDA #$00
//  EA       : (2) NOP
//  EA       : (2) NOP
// Lbl1:            ; 11 cycles is the normal duration of the loop
//  BD 64 C0 : (4) LDA $C064,X
//  10 04    : (2) BPL Lbl2    ; NB. 3 cycles if branch taken (not likely)
//  C8       : (2) INY
//  D0 F8    : (3) BNE Lbl1    ; NB. 2 cycles if branck not taken (not likely)
//  88       : (2) DEY
// Lbl2:
//  60       : (6) RTS

static const double PDL_CNTR_INTERVAL = 2816.0 / 255.0;  // 11.04 (From KEGS)

unsigned char JoyReadPosition(unsigned short programcounter, unsigned short address, unsigned char, unsigned char, uint32_t nCyclesLeft) {
  (void)programcounter;
  int nJoyNum = (address & 2) ? 1 : 0;  // $C064..$C067

  CpuCalcCycles(nCyclesLeft);

  uint32_t nPdlPos = (address & 1) ? ypos[nJoyNum] : xpos[nJoyNum];

  bool nPdlCntrActive = (g_nCumulativeCycles <= (g_nJoyCntrResetCycle + (uint64_t) ((double) nPdlPos * PDL_CNTR_INTERVAL)));

  return MemReadFloatingBus(nPdlCntrActive, nCyclesLeft);
}

unsigned char JoyResetPosition(unsigned short, unsigned short, unsigned char, unsigned char, uint32_t nCyclesLeft) {
  CpuCalcCycles(nCyclesLeft);
  g_nJoyCntrResetCycle = g_nCumulativeCycles;
  return MemReadFloatingBus(nCyclesLeft);
}

void JoySetRawPosition(int joy, int x, int y) {
  if (joy < 2) {
    xpos[joy] = x;
    ypos[joy] = y;
  }
}

void JoySetRawButton(int button_idx, bool down) {
  if (button_idx < 3) {
    if (down && !joybutton[button_idx]) {
      buttonlatch[button_idx] = BUTTONTIME;
    }
    joybutton[button_idx] = down;
  }
}

void JoyUpdatePosition(uint32_t dwExecutedCycles) {
  (void)dwExecutedCycles;
  for (int i = 0; i < 3; i++) {
    if (buttonlatch[i]) {
      --buttonlatch[i];
    }
  }
}

unsigned int JoyGetSnapshot(SS_IO_Joystick *pSS) {
  pSS->g_nJoyCntrResetCycle = g_nJoyCntrResetCycle;
  return 0;
}

unsigned int JoySetSnapshot(SS_IO_Joystick *pSS) {
  g_nJoyCntrResetCycle = pSS->g_nJoyCntrResetCycle;
  return 0;
}

void JoySetButton(eBUTTON number, eBUTTONSTATE down) {
  JoySetRawButton(number, down == BUTTON_DOWN);
}

void JoySetPosition(int xvalue, int xrange, int yvalue, int yrange) {
  if (xrange == 0 || yrange == 0) return;
  JoySetRawPosition(0, (xvalue * 255) / xrange, (yvalue * 255) / yrange);
}

bool JoySetEmulationType(unsigned int newType, int nJoystickNumber) {
  if (nJoystickNumber < 2) {
    joytype[nJoystickNumber] = newType;
    return true;
  }
  return false;
}

bool JoyUsingMouse() {
  return (joyinfo[joytype[0]].device == DEVICE_MOUSE) || (joyinfo[joytype[1]].device == DEVICE_MOUSE);
}

void JoySetTrim(short nValue, bool bAxisX) {
  if (bAxisX) {
    g_nPdlTrimX = nValue;
  } else {
    g_nPdlTrimY = nValue;
  }
}

short JoyGetTrim(bool bAxisX) {
  return bAxisX ? g_nPdlTrimX : g_nPdlTrimY;
}
