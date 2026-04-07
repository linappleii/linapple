#include <cstdint>
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

void KeybReset();
void KeybSetModifiers(bool bShift, bool bCtrl, bool bAlt);
void KeybPushAppleKey(uint8_t apple_code);
void KeybQueueKeypress(uint8_t apple_code);
void KeybToggleCapsLock();
void KeybSetAnyKeyDownStatus(bool bDown);
bool KeybGetAnyKeyDownStatus();

bool KeybGetAltStatus();
bool KeybGetCapsStatus();
bool KeybGetCtrlStatus();
bool KeybGetShiftStatus();
void KeybUpdateCtrlShiftStatus();
unsigned char KeybGetKeycode();

unsigned char KeybReadData(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);
unsigned char KeybReadFlag(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);

void ClipboardInitiatePaste();
unsigned int KeybGetNumQueries();
unsigned int KeybGetSnapshot(SS_IO_Keyboard *pSS);
unsigned int KeybSetSnapshot(SS_IO_Keyboard *pSS);
