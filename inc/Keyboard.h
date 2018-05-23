#pragma once

extern bool   g_bShiftKey;
extern bool   g_bCtrlKey;
extern bool   g_bAltKey;
//extern bool   g_bCapsLock;

void    ClipboardInitiatePaste();

void    KeybReset();
bool    KeybGetAltStatus();
bool    KeybGetCapsStatus();
bool    KeybGetCtrlStatus();
bool    KeybGetShiftStatus();
void    KeybUpdateCtrlShiftStatus();
BYTE    KeybGetKeycode ();
DWORD   KeybGetNumQueries ();
void    KeybQueueKeypress (int,BOOL);
void    KeybToggleCapsLock ();
DWORD   KeybGetSnapshot(SS_IO_Keyboard* pSS);
DWORD   KeybSetSnapshot(SS_IO_Keyboard* pSS);

BYTE KeybReadData (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
BYTE KeybReadFlag (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
