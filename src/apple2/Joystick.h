#include <cstdint>
#include "Common.h"
#pragma once

typedef struct tagSS_IO_Joystick SS_IO_Joystick;

enum JOYNUM {
  JN_JOYSTICK0 = 0, JN_JOYSTICK1
};

extern unsigned int joytype[2];

extern unsigned int joy1index;
extern unsigned int joy2index;
extern unsigned int joy1button1;
extern unsigned int joy1button2;
extern unsigned int joy2button1;
extern unsigned int joy1axis0;
extern unsigned int joy1axis1;
extern unsigned int joy2axis0;
extern unsigned int joy2axis1;
extern unsigned int joyexitenable;
extern unsigned int joyexitbutton0;
extern unsigned int joyexitbutton1;
extern bool joyquitevent;

void JoyInitialize();

void JoyShutDown();

void JoyReset();

void JoySetButton(eBUTTON number, eBUTTONSTATE down);

bool JoySetEmulationType(unsigned int, int);

void JoySetPosition(int, int, int, int);

void JoyUpdatePosition(uint32_t dwExecutedCycles);

bool JoyUsingMouse();

void JoySetTrim(short nValue, bool bAxisX);

short JoyGetTrim(bool bAxisX);

void JoySetRawPosition(int joy, int x, int y);

void JoySetRawButton(int button_idx, bool down);

unsigned int JoyGetSnapshot(SS_IO_Joystick *pSS);

unsigned int JoySetSnapshot(SS_IO_Joystick *pSS);

unsigned char JoyReadButton(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);

unsigned char JoyReadPosition(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);

unsigned char JoyResetPosition(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);
