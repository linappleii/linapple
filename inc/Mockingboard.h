#pragma once

extern bool g_bMBTimerIrqActive;
extern unsigned int g_uTimer1IrqCount;  // DEBUG

void MB_Initialize();

void MB_Reinitialize();

void MB_Destroy();

void MB_Reset();

void MB_Mute();

void MB_Demute();

void MB_StartOfCpuExecute();

void MB_EndOfVideoFrame();

void MB_CheckIRQ();

void MB_UpdateCycles(ULONG uExecutedCycles);

void MB_Update();

eSOUNDCARDTYPE MB_GetSoundcardType();

void MB_SetSoundcardType(eSOUNDCARDTYPE NewSoundcardType);

double MB_GetFramePeriod();

bool MB_IsActive();

unsigned int MB_GetVolume();

void MB_SetVolume(unsigned int dwVolume, unsigned int dwVolumeMax);

unsigned int MB_GetSnapshot(SS_CARD_MOCKINGBOARD *pSS, unsigned int dwSlot);

unsigned int MB_SetSnapshot(SS_CARD_MOCKINGBOARD *pSS, unsigned int dwSlot);

extern short *pDSMockBuf;  // Mockingboard data buffer (in size of g_dwDSMockBufferSize samples?)
extern unsigned int nDSMockWCur;  // write cursor
extern unsigned int nDSMockRCur;  // read cursor

