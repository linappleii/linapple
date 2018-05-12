#pragma once

// Define max 1 of these:
//#define RIFF_SPKR
//#define RIFF_MB

// max volume for disable sound distortion
#define SD_VOLUME	SDL_MIX_MAXVOLUME / 2
enum {FADE_OUT, FADE_IN };

bool DSInit();		// init SDL_Auidio
void DSUninit();	// uninit SDL_Auidio
//void DSSndPlay(void * mydata, Uint8 *stream, int len); // callback func for playing sound

void SoundCore_SetFade(int how);	//

double DSUploadBuffer(short* buffer, unsigned len);
void   DSUploadMockBuffer(short* buffer, unsigned len);	// Upload Mockingboard data
//LONG NewVolume(DWORD dwVolume, DWORD dwVolumeMax);

extern bool g_bDSAvailable;
