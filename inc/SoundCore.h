#pragma once

// Max volume for disable sound distortion
#define SD_VOLUME  SDL_MIX_MAXVOLUME / 2
enum {
  FADE_OUT, FADE_IN
};

bool DSInit();
void DSUninit();

void SoundCore_SetFade(int how);

void DSUploadBuffer(short *buffer, unsigned len);

void DSUploadMockBuffer(short *buffer, unsigned len);

extern bool g_bDSAvailable;
