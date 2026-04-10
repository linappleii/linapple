#include <cstdint>
#pragma once

typedef struct tagSS_IO_Speaker SS_IO_Speaker;

// For audio use none or SDL_SOUND subsystem
#define SOUND_NONE 0
#define SOUND_WAVE 1

extern unsigned int soundtype;

typedef struct {
  uint64_t cycle;
  bool state;
} SpkrEvent;

void SpkrDestroy();
void SpkrInitialize();
void SpkrReinitialize();
void SpkrReset();
void SpkrUpdate(unsigned int totalcycles);

bool Spkr_IsActive();

unsigned int SpkrGetSnapshot(SS_IO_Speaker *pSS);
unsigned int SpkrSetSnapshot(SS_IO_Speaker *pSS);

unsigned char SpkrToggle(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);

// Core Speaker API for Frontend
int SpkrGetEvents(SpkrEvent *events, int max_events);
uint64_t SpkrGetLastCycle();
bool SpkrGetCurrentState();
