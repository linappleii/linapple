/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Speaker hardware emulation (Core)
 *
 * This module tracks cycle-exact speaker toggles.
 * Sample generation and audio routing are handled via Peripheral ABI.
 */

#include <cstring>
#include <array>
#include "apple2/Speaker.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SoundCore.h"
#include "core/Common_Globals.h"
#include "core/Peripheral.h"

uint32_t soundtype = SOUND_WAVE;

// Internal default instance for legacy API
static Speaker_t g_defaultSpeaker;

static constexpr int SPKR_QUIET_CYCLES_DIVISOR = 5;

// Forward declaration of legacy callback for cases where the Peripheral ABI host is not present (e.g. some standalone tests)
extern void DSUploadBuffer(int16_t* buffer, uint32_t num_samples);

auto Speaker_Destroy(Speaker_t* instance) -> void {
  (void)instance;
}

auto Speaker_Initialize(Speaker_t* instance) -> void {
  Speaker_t* spkr = instance ? instance : &g_defaultSpeaker;
  spkr->num_events = 0;
  spkr->state = false;
  spkr->last_cycle = g_nCumulativeCycles;
  spkr->quiet_cycle_count = 0;
  spkr->recently_active = false;
  spkr->toggle_flag = false;
  
  spkr->last_sample_state = false;
  spkr->next_sample_cycle = static_cast<double>(g_nCumulativeCycles);
  
  // Only clear host if this is a fresh non-default instance.
  // The default speaker's host is set by Spkr_ABI_Init and must persist.
  if (instance != nullptr && instance != &g_defaultSpeaker) {
    spkr->host = nullptr;
  }
}

auto Speaker_Reset(Speaker_t* instance) -> void {
  Speaker_Initialize(instance);
}

auto Speaker_Update(Speaker_t* instance, uint32_t totalcycles) -> void {
  (void)totalcycles;
  Speaker_t* spkr = instance ? instance : &g_defaultSpeaker;

  if (!spkr->toggle_flag) {
    if (!spkr->quiet_cycle_count) {
      spkr->quiet_cycle_count = g_nCumulativeCycles;
    } else if (g_nCumulativeCycles - spkr->quiet_cycle_count > static_cast<uint64_t>(g_fCurrentCLK6502) / SPKR_QUIET_CYCLES_DIVISOR) {
      // After 0.2 sec of Apple time, deactivate spkr voice
      spkr->recently_active = false;
    }
  } else {
    spkr->quiet_cycle_count = 0;
    spkr->toggle_flag = false;
  }
  spkr->last_cycle = g_nCumulativeCycles;
}

auto Speaker_IsActive(Speaker_t* instance) -> bool {
  Speaker_t* spkr = instance ? instance : &g_defaultSpeaker;
  return spkr->recently_active;
}

// Justification: Parameters must match the 'iofunction' signature required by the Core memory map dispatch tables.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto Speaker_Toggle(Speaker_t* instance, uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  (void)pc; (void)addr; (void)bWrite; (void)d;
  Speaker_t* spkr = instance ? instance : &g_defaultSpeaker;

  CpuCalcCycles(nCyclesLeft);
  spkr->toggle_flag = true;

  if (!g_bFullSpeed) {
    spkr->recently_active = true;
  }

  // Record toggle event
  if (soundtype == SOUND_WAVE && static_cast<size_t>(spkr->num_events) < MAX_SPKR_EVENTS) {
    const auto idx = static_cast<size_t>(spkr->num_events);
    spkr->events.at(idx).cycle = g_nCumulativeCycles;
    spkr->events.at(idx).state = spkr->state = !spkr->state;
    spkr->num_events++;
  }

  return MemReadFloatingBus(nCyclesLeft);
}

auto Speaker_GenerateSamples(Speaker_t* instance, uint32_t dwExecutedCycles) -> void {
  if (dwExecutedCycles == 0) return;
  Speaker_t* spkr = instance ? instance : &g_defaultSpeaker;

  const double clksPerSample = g_fCurrentCLK6502 / SPKR_SAMPLE_RATE;
  if (clksPerSample <= 0.0) return;

  // Local event capture
  std::array<SpkrEvent, MAX_SPKR_EVENTS> events{};
  int num_events = Speaker_GetEvents(spkr, events.data(), MAX_SPKR_EVENTS);
  int event_idx = 0;

  const uint64_t startCycle = g_nCumulativeCycles - dwExecutedCycles;
  const uint64_t endCycle = g_nCumulativeCycles;

  if (spkr->next_sample_cycle < static_cast<double>(startCycle)) {
    spkr->next_sample_cycle = static_cast<double>(startCycle);
  }

  size_t numSamples = 0;
  while (spkr->next_sample_cycle <= static_cast<double>(endCycle) &&
         numSamples < (SPKR_BUFFER_SIZE - 2)) {
    const double sampleStart = spkr->next_sample_cycle;
    const double sampleEnd = spkr->next_sample_cycle + clksPerSample;

    double sum = 0.0;
    double currentTime = sampleStart;

    while (event_idx < num_events &&
           static_cast<double>(events.at(static_cast<size_t>(event_idx)).cycle) <
               sampleEnd) {
      const auto event_idx_st = static_cast<size_t>(event_idx);
      const auto eventTime =
          static_cast<double>(events.at(event_idx_st).cycle);

      if (eventTime <= sampleStart) {
        spkr->last_sample_state = events.at(event_idx_st).state;
      } else {
        sum += (eventTime - currentTime) * (spkr->last_sample_state ? 1.0 : -1.0);
        spkr->last_sample_state = events.at(event_idx_st).state;
        currentTime = eventTime;
      }
      event_idx++;
    }

    sum += (sampleEnd - currentTime) * (spkr->last_sample_state ? 1.0 : -1.0);

    const double average = sum / clksPerSample;
    const auto val = static_cast<int16_t>(average * SPKR_SAMPLE_VOLUME);

    spkr->sample_buffer.at(numSamples++) = val; // Left
    spkr->sample_buffer.at(numSamples++) = val; // Right
    spkr->next_sample_cycle += clksPerSample;
  }

  if (numSamples > 0) {
    auto* host = static_cast<HostInterface_t*>(spkr->host);
    if (host && host->AudioPushSamples) {
      host->AudioPushSamples(spkr, spkr->sample_buffer.data(), numSamples);
    } else {
      // Fallback for tests and environments without a fully initialized Peripheral ABI host.
      DSUploadBuffer(spkr->sample_buffer.data(), static_cast<uint32_t>(numSamples));
    }
  }
}

auto Speaker_GetEvents(Speaker_t* instance, SpkrEvent *events, int max_events) -> int {
  if (events == nullptr) return 0;
  Speaker_t* spkr = instance ? instance : &g_defaultSpeaker;
  int count = (spkr->num_events < max_events) ? spkr->num_events : max_events;
  if (count > 0) {
    memcpy(events, spkr->events.data(), static_cast<size_t>(count) * sizeof(SpkrEvent));
    spkr->num_events = 0;
  }
  return count;
}

auto Speaker_GetLastCycle(Speaker_t* instance) -> uint64_t {
  Speaker_t* spkr = instance ? instance : &g_defaultSpeaker;
  return spkr->last_cycle;
}

auto Speaker_GetCurrentState(Speaker_t* instance) -> bool {
  Speaker_t* spkr = instance ? instance : &g_defaultSpeaker;
  return spkr->state;
}

// --- Legacy API Wrappers ---

// Justification: Signature is required for compatibility with the Core memory map iofunction pointers.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto SpkrToggle(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  Speaker_t* spkr = instance ? static_cast<Speaker_t*>(instance) : &g_defaultSpeaker;
  return Speaker_Toggle(spkr, pc, addr, bWrite, d, nCyclesLeft);
}

auto Spkr_IsActive() -> bool { return Speaker_IsActive(&g_defaultSpeaker); }

auto SpkrGetSnapshot(SS_IO_Speaker *pSS) -> uint32_t {
  pSS->g_nSpkrLastCycle = Speaker_GetLastCycle(&g_defaultSpeaker);
  return 0;
}

auto SpkrSetSnapshot(SS_IO_Speaker *pSS) -> uint32_t {
  g_defaultSpeaker.last_cycle = pSS->g_nSpkrLastCycle;
  return 0;
}

// --- Peripheral ABI ---

static constexpr uint16_t ADDR_SPEAKER = 0xC030;

static auto Spkr_ABI_Init(int slot, HostInterface_t* host) -> void* {
  (void)slot;
  // Use the default instance for now until multiple instances are fully supported
  g_defaultSpeaker.host = static_cast<void*>(host);
  Speaker_Initialize(&g_defaultSpeaker);
  
  // Speaker is at $C030
  // Note: we pass g_defaultSpeaker as the instance pointer to the IO handler
  host->RegisterDirectIO(&g_defaultSpeaker, ADDR_SPEAKER, SpkrToggle, SpkrToggle);
  
  return &g_defaultSpeaker;
}

static auto Spkr_ABI_Reset(void* instance) -> void {
  Speaker_Reset(static_cast<Speaker_t*>(instance));
}

static auto Spkr_ABI_Shutdown(void* instance) -> void {
  Speaker_Destroy(static_cast<Speaker_t*>(instance));
}

static auto Spkr_ABI_Think(void* instance, uint32_t cycles) -> void {
  Speaker_Update(static_cast<Speaker_t*>(instance), cycles);
  Speaker_GenerateSamples(static_cast<Speaker_t*>(instance), cycles);
}

// Justification: Signature is fixed by the stable LinApple Peripheral ABI.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto Spkr_ABI_SaveState(void* instance, void* buffer, size_t* size) -> PeripheralStatus {
  if (!buffer || !size || *size < sizeof(SS_IO_Speaker)) {
    if (size) *size = sizeof(SS_IO_Speaker);
    return PERIPHERAL_ERROR;
  }
  auto* spkr = static_cast<Speaker_t*>(instance);
  auto* pSS = static_cast<SS_IO_Speaker*>(buffer);
  pSS->g_nSpkrLastCycle = spkr->last_cycle;
  *size = sizeof(SS_IO_Speaker);
  return PERIPHERAL_OK;
}

// Justification: Signature is fixed by the stable LinApple Peripheral ABI.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto Spkr_ABI_LoadState(void* instance, const void* buffer, size_t size) -> PeripheralStatus {
  if (!buffer || size < sizeof(SS_IO_Speaker)) {
    return PERIPHERAL_ERROR;
  }
  auto* spkr = static_cast<Speaker_t*>(instance);
  const auto* pSS = static_cast<const SS_IO_Speaker*>(buffer);
  spkr->last_cycle = pSS->g_nSpkrLastCycle;
  return PERIPHERAL_OK;
}

Peripheral_t g_speaker_peripheral = {
    LINAPPLE_ABI_VERSION,
    "Speaker",
    LINAPPLE_ANY_SLOT_MASK,
    Spkr_ABI_Init,
    Spkr_ABI_Reset,
    Spkr_ABI_Shutdown,
    Spkr_ABI_Think,
    nullptr, // on_vblank
    Spkr_ABI_SaveState,
    Spkr_ABI_LoadState,
    nullptr, // command
    nullptr  // query
};

#ifdef BUILD_SHARED_PERIPHERAL
EXPORT_PERIPHERAL(g_speaker_peripheral)
#endif
