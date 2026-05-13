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

// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables) Justification: This file
// implements the C11-compatible Peripheral ABI. It requires void* pointers for
// instance state and raw memory management to bridge with the core C interface.

#include "apple2/peripherals/Speaker.h"

#include <array>
#include <cstring>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SoundCore.h"
#include "core/Common_Globals.h"
#include "core/Peripheral.h"

static constexpr int SPKR_QUIET_CYCLES_DIVISOR = 5;

// Forward declaration of legacy callback for cases where the Peripheral ABI
// host is not present (e.g. some standalone tests)
extern void DSUploadBuffer(int16_t* buffer, uint32_t num_samples);

auto Speaker_Destroy(Speaker_t* instance) -> void { (void)instance; }

auto Speaker_Initialize(Speaker_t* instance) -> void {
  if (!instance) return;
  instance->num_events = 0;
  instance->state = false;
  instance->last_cycle = g_nCumulativeCycles;
  instance->quiet_cycle_count = 0;
  instance->recently_active = false;
  instance->toggle_flag = false;
  instance->sound_type = 1;  // SOUND_WAVE

  instance->last_sample_state = false;
  instance->next_sample_cycle = static_cast<double>(g_nCumulativeCycles);
  instance->filter_state = 0.0f;
  instance->last_input = 0.0f;
  instance->host = nullptr;
}

auto Speaker_Reset(Speaker_t* instance) -> void {
  Speaker_Initialize(instance);
}

auto Speaker_Update(Speaker_t* instance, uint32_t totalcycles) -> void {
  (void)totalcycles;
  if (!instance) return;

  if (!instance->toggle_flag) {
    if (!instance->quiet_cycle_count) {
      instance->quiet_cycle_count = g_nCumulativeCycles;
    } else if (g_nCumulativeCycles - instance->quiet_cycle_count >
               static_cast<uint64_t>(g_fCurrentCLK6502) /
                   SPKR_QUIET_CYCLES_DIVISOR) {
      if (instance->recently_active) {
        // Logger::Info("Speaker: Deactivated\n");
      }
      instance->recently_active = false;
    }
  } else {
    if (!instance->recently_active) {
      // Logger::Info("Speaker: Activated\n");
    }
    instance->quiet_cycle_count = 0;
    instance->toggle_flag = false;
  }
  instance->last_cycle = g_nCumulativeCycles;
}

auto Speaker_IsActive(Speaker_t* instance) -> bool {
  return instance ? instance->recently_active : false;
}

// Justification: Parameters must match the 'iofunction' signature required by
// the Core memory map dispatch tables.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto Speaker_Toggle(Speaker_t* instance, uint16_t pc, uint16_t addr,
                    uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t {
  (void)pc;
  (void)addr;
  (void)bWrite;
  (void)d;
  if (!instance) return MemReadFloatingBus(nCyclesLeft);

  CpuCalcCycles(nCyclesLeft);
  instance->toggle_flag = true;

  if (!g_bFullSpeed) {
    instance->recently_active = true;
  }

  // Record toggle event
  if (instance->sound_type == 1 /* SOUND_WAVE */ &&
      static_cast<size_t>(instance->num_events) < MAX_SPKR_EVENTS) {
    const auto idx = static_cast<size_t>(instance->num_events);
    instance->events.at(idx).cycle = g_nCumulativeCycles;
    instance->events.at(idx).state = instance->state = !instance->state;
    instance->num_events++;
  }

  return MemReadFloatingBus(nCyclesLeft);
}

auto Speaker_GenerateSamples(Speaker_t* instance, uint32_t dwExecutedCycles)
    -> void {
  if (dwExecutedCycles == 0 || !instance) return;

  const double clksPerSample = g_fCurrentCLK6502 / SPKR_SAMPLE_RATE;
  if (clksPerSample <= 0.0) return;

  const uint64_t startCycle = g_nCumulativeCycles - dwExecutedCycles;
  const uint64_t endCycle = g_nCumulativeCycles;

  if (instance->next_sample_cycle < static_cast<double>(startCycle)) {
    instance->next_sample_cycle = static_cast<double>(startCycle);
  }

  size_t numSamples = 0;

  if (!instance->recently_active) {
    // Fill buffer with zeros and advance next_sample_cycle
    while (instance->next_sample_cycle <= static_cast<double>(endCycle) &&
           numSamples < (SPKR_BUFFER_SIZE - 2)) {
      instance->sample_buffer.at(numSamples++) = 0;  // Left
      instance->sample_buffer.at(numSamples++) = 0;  // Right
      instance->next_sample_cycle += clksPerSample;
    }

    // Clear any pending events to keep state consistent
    instance->num_events = 0;
  } else {
    // Local event capture
    std::array<SpkrEvent, MAX_SPKR_EVENTS> events{};
    uint32_t num_events =
        Speaker_GetEvents(instance, events.data(), MAX_SPKR_EVENTS);
    uint32_t event_idx = 0;

    while (instance->next_sample_cycle <= static_cast<double>(endCycle) &&
           numSamples < (SPKR_BUFFER_SIZE - 2)) {
      const double sampleStart = instance->next_sample_cycle;
      const double sampleEnd = instance->next_sample_cycle + clksPerSample;

      double sum = 0.0;
      double currentTime = sampleStart;

      while (event_idx < num_events &&
             static_cast<double>(events.at(event_idx).cycle) < sampleEnd) {
        const auto eventTime = static_cast<double>(events.at(event_idx).cycle);

        if (eventTime <= sampleStart) {
          instance->last_sample_state = events.at(event_idx).state;
        } else {
          sum += (eventTime - currentTime) *
                 (instance->last_sample_state ? 1.0 : -1.0);
          instance->last_sample_state = events.at(event_idx).state;
          currentTime = eventTime;
        }
        event_idx++;
      }

      sum += (sampleEnd - currentTime) *
             (instance->last_sample_state ? 1.0 : -1.0);

      float average = static_cast<float>(sum / clksPerSample);

      // Simple High-Pass Filter (DC Blocker) to prevent popping from DC offset
      // out = (in - last_in) + 0.999 * last_out
      instance->filter_state =
          (average - instance->last_input) + (0.999f * instance->filter_state);
      instance->last_input = average;

      const int16_t val =
          static_cast<int16_t>(instance->filter_state * SPKR_SAMPLE_VOLUME);

      instance->sample_buffer.at(numSamples++) = val;  // Left
      instance->sample_buffer.at(numSamples++) = val;  // Right
      instance->next_sample_cycle += clksPerSample;
    }
  }

  // If we just became inactive, ensure we flush a bit of silence through the
  // filter to let it decay to zero smoothly.
  if (!instance->recently_active && instance->filter_state != 0.0f) {
    while (numSamples < (SPKR_BUFFER_SIZE - 2) &&
           std::abs(instance->filter_state) > 0.001f) {
      instance->filter_state *= 0.99f;
      const int16_t val =
          static_cast<int16_t>(instance->filter_state * SPKR_SAMPLE_VOLUME);
      instance->sample_buffer.at(numSamples++) = val;
      instance->sample_buffer.at(numSamples++) = val;
    }
    if (std::abs(instance->filter_state) <= 0.001f) {
      instance->filter_state = 0.0f;
    }
  }

  if (numSamples > 0) {
    auto* host = static_cast<HostInterface_t*>(instance->host);
    if (host && host->AudioPushSamples) {
      host->AudioPushSamples(instance, instance->sample_buffer.data(),
                             numSamples);
    } else {
      // Fallback for tests and environments without a fully initialized
      // Peripheral ABI host.
      DSUploadBuffer(instance->sample_buffer.data(),
                     static_cast<uint32_t>(numSamples));
    }
  }
}

auto Speaker_GetEvents(Speaker_t* instance, SpkrEvent* events,
                       uint32_t max_events) -> uint32_t {
  if (events == nullptr || !instance) return 0;
  uint32_t count =
      (instance->num_events < max_events) ? instance->num_events : max_events;
  if (count > 0) {
    memcpy(events, instance->events.data(),
           static_cast<size_t>(count) * sizeof(SpkrEvent));
    instance->num_events = 0;
  }
  return count;
}

auto Speaker_GetLastCycle(Speaker_t* instance) -> uint64_t {
  return instance ? instance->last_cycle : 0;
}

auto Speaker_GetCurrentState(Speaker_t* instance) -> bool {
  return instance ? instance->state : false;
}

// --- ABI Implementation ---

static constexpr uint16_t ADDR_SPEAKER = 0xC030;

// Justification: Signature is required for compatibility with the Core memory
// map iofunction pointers.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto SpkrToggleBridge(void* instance, uint16_t pc, uint16_t addr,
                             uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  return Speaker_Toggle(static_cast<Speaker_t*>(instance), pc, addr, write, val,
                        cycles_left);
}

static auto Spkr_ABI_Init(int slot, HostInterface_t* host) -> void* {
  auto* instance = new Speaker_t{};
  instance->host = static_cast<void*>(host);
  instance->slot = slot;
  Speaker_Initialize(instance);

  // Speaker is at $C030
  if (host && host->RegisterDirectIO) {
    host->RegisterDirectIO(instance, ADDR_SPEAKER, SpkrToggleBridge,
                           SpkrToggleBridge);
  }

  return instance;
}

static auto Spkr_ABI_Reset(void* instance) -> void {
  Speaker_Reset(static_cast<Speaker_t*>(instance));
}

static auto Spkr_ABI_Shutdown(void* instance) -> void {
  auto* spkr = static_cast<Speaker_t*>(instance);
  Speaker_Destroy(spkr);
  delete spkr;
}

static auto Spkr_ABI_Think(void* instance, uint32_t cycles) -> void {
  auto* spkr = static_cast<Speaker_t*>(instance);
  Speaker_Update(spkr, cycles);
  Speaker_GenerateSamples(spkr, cycles);
}

// Justification: Signature is fixed by the stable LinApple Peripheral ABI.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto Spkr_ABI_SaveState(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus {
  if (!size) return PERIPHERAL_ERROR;
  if (!buffer) {
    *size = sizeof(SS_IO_Speaker);
    return PERIPHERAL_OK;
  }
  if (!instance || *size < sizeof(SS_IO_Speaker)) return PERIPHERAL_ERROR;

  auto* spkr = static_cast<Speaker_t*>(instance);
  auto* pSS = static_cast<SS_IO_Speaker*>(buffer);
  pSS->g_nSpkrLastCycle = spkr->last_cycle;
  pSS->quiet_cycle_count = spkr->quiet_cycle_count;
  pSS->recently_active = spkr->recently_active ? 1 : 0;
  pSS->state = spkr->state ? 1 : 0;
  pSS->next_sample_cycle = spkr->next_sample_cycle;
  pSS->last_sample_state = spkr->last_sample_state ? 1 : 0;
  pSS->filter_state = spkr->filter_state;

  *size = sizeof(SS_IO_Speaker);
  return PERIPHERAL_OK;
}

// Justification: Signature is fixed by the stable LinApple Peripheral ABI.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto Spkr_ABI_LoadState(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus {
  if (!instance || !buffer || size < sizeof(SS_IO_Speaker)) {
    return PERIPHERAL_ERROR;
  }
  auto* spkr = static_cast<Speaker_t*>(instance);
  const auto* pSS = static_cast<const SS_IO_Speaker*>(buffer);
  spkr->last_cycle = pSS->g_nSpkrLastCycle;
  spkr->quiet_cycle_count = pSS->quiet_cycle_count;
  spkr->recently_active = (pSS->recently_active != 0);
  spkr->state = (pSS->state != 0);
  spkr->next_sample_cycle = pSS->next_sample_cycle;
  spkr->last_sample_state = (pSS->last_sample_state != 0);
  spkr->filter_state = pSS->filter_state;

  return PERIPHERAL_OK;
}

static auto Spkr_ABI_Query(void* instance, uint32_t query_id, void* out,
                           size_t* out_size) -> PeripheralStatus {
  if (!instance || !out_size) return PERIPHERAL_ERROR;
  auto* spkr = static_cast<Speaker_t*>(instance);

  switch (query_id) {
    case SPEAKER_QUERY_IS_ACTIVE: {
      if (!out) {
        *out_size = sizeof(bool);
        return PERIPHERAL_OK;
      }
      if (*out_size < sizeof(bool)) return PERIPHERAL_ERROR;
      *static_cast<bool*>(out) = Speaker_IsActive(spkr);
      *out_size = sizeof(bool);
      return PERIPHERAL_OK;
    }
    default:
      return PERIPHERAL_INCOMPATIBLE;
  }
}

Peripheral_t g_speaker_peripheral = {
    LINAPPLE_ABI_VERSION,
    "linapple.speaker",
    "Speaker",
    "Built-in Apple II speaker and cassette port emulation",
    "LinApple Contributors",
    VERSIONSTRING,
    0x01,  // Slot 0 (Internal)
    0,     // Default Slot 0
    Spkr_ABI_Init,
    Spkr_ABI_Reset,
    Spkr_ABI_Shutdown,
    Spkr_ABI_Think,
    nullptr,  // on_vblank
    Spkr_ABI_SaveState,
    Spkr_ABI_LoadState,
    nullptr,  // command
    Spkr_ABI_Query};

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables)
PERIPHERAL_REGISTER(g_speaker_peripheral)
