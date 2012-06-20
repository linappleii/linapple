#pragma once

enum JOYNUM {JN_JOYSTICK0=0, JN_JOYSTICK1};

extern DWORD      joytype[2];

void    JoyInitialize ();
void	JoyShutDown();

BOOL    JoyProcessKey (int,BOOL,BOOL,BOOL);
void    JoyReset ();
void    JoySetButton (eBUTTON,eBUTTONSTATE);
BOOL    JoySetEmulationType (/*HWND,*/DWORD,int);
void    JoySetPosition (int,int,int,int);
void    JoyUpdatePosition ();
BOOL    JoyUsingMouse ();
void    JoySetTrim(short nValue, bool bAxisX);
short   JoyGetTrim(bool bAxisX);
DWORD   JoyGetSnapshot(SS_IO_Joystick* pSS);
DWORD   JoySetSnapshot(SS_IO_Joystick* pSS);

BYTE JoyReadButton (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
BYTE JoyReadPosition (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
BYTE JoyResetPosition (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
