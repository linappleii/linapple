#pragma once

enum JOYNUM {
  JN_JOYSTICK0 = 0, JN_JOYSTICK1
};

extern DWORD joytype[2];

extern DWORD joy1index;
extern DWORD joy2index;
extern DWORD joy1button1;
extern DWORD joy1button2;
extern DWORD joy2button1;
extern DWORD joy1axis0;
extern DWORD joy1axis1;
extern DWORD joy2axis0;
extern DWORD joy2axis1;
extern DWORD joyexitenable;
extern DWORD joyexitbutton0;
extern DWORD joyexitbutton1;
extern bool joyquitevent;

void CheckJoyExit();


void JoyInitialize();

void JoyShutDown();

void JoyUpdateTrimViaKey(int);

BOOL JoyProcessKey(int, BOOL, BOOL, BOOL);

void JoyReset();

void JoySetButton(eBUTTON, eBUTTONSTATE);

BOOL JoySetEmulationType(/*HWND,*/DWORD, int);

void JoySetPosition(int, int, int, int);

void JoyUpdatePosition();

BOOL JoyUsingMouse();

void JoySetTrim(short nValue, bool bAxisX);

short JoyGetTrim(bool bAxisX);

DWORD JoyGetSnapshot(SS_IO_Joystick *pSS);

DWORD JoySetSnapshot(SS_IO_Joystick *pSS);

BYTE JoyReadButton(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);

BYTE JoyReadPosition(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);

BYTE JoyResetPosition(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
