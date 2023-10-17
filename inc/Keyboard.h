#pragma once

extern bool g_bShiftKey;
extern bool g_bCtrlKey;
extern bool g_bAltKey;

typedef enum {
  English_US=1,
  English_UK=2,
  French_FR=3,
  German_DE=4,
  Spanish_ES=5
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

unsigned char KeybGetKeycode();

unsigned int KeybGetNumQueries();

void KeybQueueKeypress(int, bool);

void KeybToggleCapsLock();

unsigned int KeybGetSnapshot(SS_IO_Keyboard *pSS);

unsigned int KeybSetSnapshot(SS_IO_Keyboard *pSS);

unsigned char KeybReadData(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, ULONG nCyclesLeft);

unsigned char KeybReadFlag(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, ULONG nCyclesLeft);
