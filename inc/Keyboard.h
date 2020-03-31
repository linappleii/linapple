#pragma once

extern bool g_bShiftKey;
extern bool g_bCtrlKey;
extern bool g_bAltKey;

typedef enum {
  English_US=1,
  English_UK=2,
  French_FR=3,
  German_DE=4
} KeybLanguage;

extern KeybLanguage g_KeyboardLanguage;
extern bool         g_KeyboardRockerSwitch;

void ClipboardInitiatePaste();

int KeybDecodeKey(int key);

void KeybReset();

bool KeybGetAltStatus();

bool KeybGetCapsStatus();

bool KeybGetCtrlStatus();

bool KeybGetShiftStatus();

void KeybUpdateCtrlShiftStatus();

BYTE KeybGetKeycode();

DWORD KeybGetNumQueries();

void KeybQueueKeypress(int, BOOL);

void KeybToggleCapsLock();

DWORD KeybGetSnapshot(SS_IO_Keyboard *pSS);

DWORD KeybSetSnapshot(SS_IO_Keyboard *pSS);

BYTE KeybReadData(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);

BYTE KeybReadFlag(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
