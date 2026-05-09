#include <cstdint>
#include <array>
#include "core/Common.h"
#pragma once

typedef struct tagSS_IO_Joystick SS_IO_Joystick;

enum JOYNUM {
  JN_JOYSTICK0 = 0, JN_JOYSTICK1
};

extern std::array<uint32_t, 2> joytype;

extern uint32_t joy1index;
extern uint32_t joy2index;
extern uint32_t joy1button1;
extern uint32_t joy1button2;
extern uint32_t joy2button1;
extern uint32_t joy1axis0;
extern uint32_t joy1axis1;
extern uint32_t joy2axis0;
extern uint32_t joy2axis1;
extern uint32_t joyexitenable;
extern uint32_t joyexitbutton0;
extern uint32_t joyexitbutton1;
extern bool joyquitevent;

void JoyInitialize();

void JoyShutDown();

void JoyReset();

bool JoySetEmulationType(uint32_t, int);

void JoyUpdatePosition(uint32_t dwExecutedCycles);

bool JoyUsingMouse();

uint32_t JoyGetSnapshot(SS_IO_Joystick *pSS);

uint32_t JoySetSnapshot(SS_IO_Joystick *pSS);

uint8_t JoyReadButton(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft);

uint8_t JoyReadPosition(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft);

uint8_t JoyResetPosition(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft);
