#pragma once

#include <cstdint>

enum {
  FADE_OUT, FADE_IN
};

void SoundCore_Initialize();
void SoundCore_Destroy();

void DSUploadBuffer(const int16_t* buffer, uint32_t num_samples);
void DSUploadMockBuffer(const int16_t* buffer, uint32_t num_samples);

// Mixes all sound sources and returns samples
void SoundCore_GetSamples(int16_t *out, size_t num_samples);

// These were used for fading, might need to be moved or implemented differently
void SoundCore_SetFade(int how);

extern bool g_bDSAvailable;
