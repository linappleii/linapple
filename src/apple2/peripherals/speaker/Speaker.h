#pragma once

#include <cstdint>

#include "apple2/peripherals/speaker/Speaker_Structs.h"

struct SS_IO_Speaker;

enum { SOUND_NONE = 0, SOUND_WAVE = 1 };

static constexpr int16_t SPKR_SAMPLE_VOLUME = 0x4000;

// Pointer-based API
auto Speaker_Destroy(Speaker_t* instance) -> void;
auto Speaker_Initialize(Speaker_t* instance) -> void;
auto Speaker_Reset(Speaker_t* instance) -> void;
auto Speaker_Update(Speaker_t* instance, uint32_t totalcycles) -> void;
auto Speaker_IsActive(Speaker_t* instance) -> bool;
auto Speaker_Toggle(Speaker_t* instance, uint16_t pc, uint16_t addr,
                    uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft) -> uint8_t;
auto Speaker_GenerateSamples(Speaker_t* instance, uint32_t dwExecutedCycles)
    -> void;

// Core Speaker API for Frontend (Pointer-based)
auto Speaker_GetEvents(Speaker_t* instance, SpkrEvent* events,
                       uint32_t max_events) -> uint32_t;
auto Speaker_GetLastCycle(Speaker_t* instance) -> uint64_t;
auto Speaker_GetCurrentState(Speaker_t* instance) -> bool;

// Speaker Query IDs
enum { SPEAKER_QUERY_IS_ACTIVE = 0x100 };
