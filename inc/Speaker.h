#include <cstdint>
#pragma once

// For audio use only none or SDL_SOUND subsystem
#define  SOUND_NONE    0
#define  SOUND_WAVE    1

extern unsigned int soundtype;
extern double g_fClksPerSpkrSample;

// needed for DSPlaySnd callback function
extern short *pDSSpkrBuf;
extern unsigned int g_dwDSSpkrBufferSize;
extern unsigned int nDSSpkrWCur;
extern unsigned int nDSSpkrRCur;

void SpkrDestroy();
void SpkrInitialize();
void SpkrReinitialize();
void SpkrReset();
void SpkrUpdate(unsigned int);
unsigned int SpkrGetVolume();
void SpkrSetVolume(unsigned int dwVolume, unsigned int dwVolumeMax);
void Spkr_Mute();
void Spkr_Demute();
bool Spkr_IsActive();
bool Spkr_DSInit();
void Spkr_DSUninit();
unsigned int SpkrGetSnapshot(SS_IO_Speaker *pSS);
unsigned int SpkrSetSnapshot(SS_IO_Speaker *pSS);
unsigned char SpkrToggle(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);
