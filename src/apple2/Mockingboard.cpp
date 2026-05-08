#include "core/Common_Globals.h"
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

/* Description: Mockingboard/Phasor emulation
 *
 * Author: Copyright (c) 2002-2006, Tom Charlesworth
 */

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "AY8910.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Mockingboard.h"
#include "apple2/SoundCore.h"
#include "apple2/Structs.h"
#include "apple2/Video.h"
#include "core/Common.h"
#include "core/Log.h"
#include "core/Peripheral.h"

enum { SY6522_DEVICE_A = 0, SY6522_DEVICE_B = 1 };

enum { SY6522A_Offset = 0x00, SY6522B_Offset = 0x80 };

enum { IxR_TIMER1 = 0x40, IxR_TIMER2 = 0x20 };

enum { VIA_IFR_BIT_MASK = 0x7F, VIA_IFR_IRQ_FLAG = 0x80 };

enum { RUNMODE = 0x40, RM_ONESHOT = 0x00 };

enum { TIMER_LOW_BYTE_MAX = 0xFF };

enum {
  VIA_REG_ORB = 0x0,
  VIA_REG_ORA = 0x1,
  VIA_REG_DDRB = 0x2,
  VIA_REG_DDRA = 0x3,
  VIA_REG_T1L_C = 0x4,
  VIA_REG_T1H_C = 0x5,
  VIA_REG_T1L_L = 0x6,
  VIA_REG_T1H_L = 0x7,
  VIA_REG_T2L_C = 0x8,
  VIA_REG_T2H_C = 0x9,
  VIA_REG_SR = 0xA,
  VIA_REG_ACR = 0xB,
  VIA_REG_PCR = 0xC,
  VIA_REG_IFR = 0xD,
  VIA_REG_IER = 0xE,
  VIA_REG_ORA_NO_HANDSHAKE = 0xF
};

static const int NUM_VOICES = 12;
static const int NUM_AY8910 = 4;
static const int NUM_SY6522 = 4;
static const int NUM_DEVS_PER_MB = 2;
static const int SLOT4 = 4;

#if defined MOCKINGBOARD

typedef struct {
  SY6522 sy6522;
  SSI263A SpeechChip;
  uint16_t nAYCurrentRegister;
  uint8_t nAY8910Number;
  int nTimerStatus;
} SY6522_AY8910;

// Justification: Legacy hardware emulation relies on global state for
// cycle-accurate timing and multi-chip coordination.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static SY6522_AY8910 g_MB[NUM_AY8910];

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static uint32_t g_n6522TimerPeriod = 0;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static uint16_t g_nMBTimerDevice =
    0;  // SY6522 device# which is generating timer IRQ
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static uint64_t g_uLastCumulativeCycles = 0;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<short[], void (*)(void*)> ppAYVoiceBuffer[NUM_VOICES] = {
    {nullptr, free}, {nullptr, free}, {nullptr, free}, {nullptr, free},
    {nullptr, free}, {nullptr, free}, {nullptr, free}, {nullptr, free},
    {nullptr, free}, {nullptr, free}, {nullptr, free}, {nullptr, free}};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static uint64_t g_nMB_InActiveCycleCount = 0;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_bMB_RegAccessedFlag = false;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_bMB_Active = true;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_bMBAvailable = false;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static eSOUNDCARDTYPE g_SoundcardType =
    SC_MOCKINGBOARD;  // Mockingboard enable (dialog var)
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_bPhasorEnable = false;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static uint8_t g_nPhasorMode = 0;  // 0=Mockingboard emulation, 1=Phasor native

static const uint16_t g_nMB_NumChannels = 2;

static const uint32_t g_dwDSBufferSize =
    static_cast<size_t>(32 * 1024) * sizeof(short) * g_nMB_NumChannels;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static short g_nMixBuffer[g_dwDSBufferSize / sizeof(short)];

// When 6522 IRQ is *not* active use 60Hz update freq for MB voices
static const double g_f6522TimerPeriod_NoIRQ =
    CLOCK_6502 / 60.0;  // Constant whatever the CLK is set to

// External global vars:
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
bool g_bMBTimerIrqActive = false;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
uint32_t g_uTimer1IrqCount = 0;  // DEBUG

// Forward refs:

static void StartTimer(SY6522_AY8910* pMB) {
  if ((pMB->nAY8910Number & 1) != SY6522_DEVICE_A) {
    return;
  }
  if ((pMB->sy6522.IER & IxR_TIMER1) == 0x00) {
    return;
  }

  uint16_t nPeriod = pMB->sy6522.TIMER1_LATCH.w;

  if (nPeriod <= TIMER_LOW_BYTE_MAX) {  // Timer1L value has been written (but
                                        // TIMER1H hasn't)
    return;
  }

  pMB->nTimerStatus = 1;

  // 6522 CLK runs at same speed as 6502 CLK
  g_n6522TimerPeriod = nPeriod;

  g_bMBTimerIrqActive = true;
  g_nMBTimerDevice = pMB->nAY8910Number;
}

static void StopTimer(SY6522_AY8910* pMB) {
  pMB->nTimerStatus = 0;
  g_bMBTimerIrqActive = false;
  g_nMBTimerDevice = 0;
}

static void ResetSY6522(SY6522_AY8910* pMB) {
  memset(&pMB->sy6522, 0, sizeof(SY6522));
  if (pMB->nTimerStatus) {
    StopTimer(pMB);
  }
  pMB->nAYCurrentRegister = 0;
}

static void AY8910_Write(uint8_t nDevice, uint8_t nReg, uint8_t nValue,
                         uint8_t nAYDevice) {
  (void)nReg;
  SY6522_AY8910* pMB = &g_MB[nDevice];

  if ((nValue & 4) == 0) {
    // RESET: Reset AY8910 only
    AY8910_reset(nDevice + 2 * nAYDevice);
  } else {
    // Determine the AY8910 inputs
    int nBDIR = (nValue & 2) ? 1 : 0;
    const int nBC2 = 1;  // Hardwired to +5V
    int nBC1 = nValue & 1;

    int nAYFunc = (nBDIR << 2) | (nBC2 << 1) | nBC1;
    enum {
      AY_NOP0,
      AY_NOP1,
      AY_INACTIVE,
      AY_READ,
      AY_NOP4,
      AY_NOP5,
      AY_WRITE,
      AY_LATCH
    };

    switch (nAYFunc) {
      case AY_INACTIVE:  // 4: INACTIVE
        break;

      case AY_READ:  // 5: READ FROM PSG (need to set DDRA to input)
        break;

      case AY_WRITE:  // 6: WRITE TO PSG
        _AYWriteReg(nDevice + 2 * nAYDevice, pMB->nAYCurrentRegister,
                    pMB->sy6522.ORA);
        break;

      case AY_LATCH:  // 7: LATCH ADDRESS
        if (pMB->sy6522.ORA <= 0x0F) {
          pMB->nAYCurrentRegister = pMB->sy6522.ORA & 0x0F;
        }
        // else Pro-Mockingboard (clone from HK)
        break;
    }
  }
}

// Justification: Peripheral Host Interface requires storage of core callback
// services and active slot information for the migrated Mockingboard instance.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static HostInterface_t* g_pMBHost = nullptr;
static int g_nMB_Slot = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

static void UpdateIFR(SY6522_AY8910* pMB) {
  pMB->sy6522.IFR &= VIA_IFR_BIT_MASK;

  if (pMB->sy6522.IFR & pMB->sy6522.IER & VIA_IFR_BIT_MASK) {
    pMB->sy6522.IFR |= VIA_IFR_IRQ_FLAG;
  }

  // Now update the IRQ signal from all 6522s
  // . OR-sum of all active TIMER1, TIMER2 & SPEECH sources (from all 6522s)
  uint32_t bIRQ = 0;
  for (auto& i : g_MB) {
    bIRQ |= i.sy6522.IFR & VIA_IFR_IRQ_FLAG;
  }

  if (bIRQ) {
    CpuIrqAssert(IS_6522);
  } else {
    CpuIrqDeassert(IS_6522);
  }
}

static void SY6522_Write(uint8_t nDevice, uint8_t nReg, uint8_t nValue) {
  g_bMB_RegAccessedFlag = true;
  g_bMB_Active = true;

  SY6522_AY8910* pMB = &g_MB[nDevice];

  switch (nReg) {
    case VIA_REG_ORB:  // ORB
    {
      nValue &= pMB->sy6522.DDRB;
      pMB->sy6522.ORB = nValue;

      if (g_bPhasorEnable) {
        int nAY_CS = (g_nPhasorMode & 1) ? (~(nValue >> 3) & 3) : 1;

        if (nAY_CS & 1) {
          AY8910_Write(nDevice, nReg, nValue, 0);
        }

        if (nAY_CS & 2) {
          AY8910_Write(nDevice, nReg, nValue, 1);
        }
      } else {
        AY8910_Write(nDevice, nReg, nValue, 0);
      }

      break;
    }
    case VIA_REG_ORA:  // ORA
      pMB->sy6522.ORA = nValue & pMB->sy6522.DDRA;
      break;
    case VIA_REG_DDRB:  // DDRB
      pMB->sy6522.DDRB = nValue;
      break;
    case VIA_REG_DDRA:  // DDRA
      pMB->sy6522.DDRA = nValue;
      break;
    case VIA_REG_T1L_C:  // TIMER1L_COUNTER
    case VIA_REG_T1L_L:  // TIMER1L_LATCH
      pMB->sy6522.TIMER1_LATCH.l = nValue;
      break;
    case VIA_REG_T1H_C:  // TIMER1H_COUNTER
      /* Initiates timer1 & clears time-out of timer1 */

      // Clear Timer Interrupt Flag.
      pMB->sy6522.IFR &= ~IxR_TIMER1;
      UpdateIFR(pMB);

      pMB->sy6522.TIMER1_LATCH.h = nValue;
      pMB->sy6522.TIMER1_COUNTER.w = pMB->sy6522.TIMER1_LATCH.w;

      StartTimer(pMB);
      break;
    case VIA_REG_T1H_L:  // TIMER1H_LATCH
      // Clear Timer1 Interrupt Flag.
      pMB->sy6522.TIMER1_LATCH.h = nValue;
      pMB->sy6522.IFR &= ~IxR_TIMER1;
      UpdateIFR(pMB);
      break;
    case VIA_REG_T2L_C:  // TIMER2L
      pMB->sy6522.TIMER2_LATCH.l = nValue;
      break;
    case VIA_REG_T2H_C:  // TIMER2H
      // Clear Timer2 Interrupt Flag.
      pMB->sy6522.IFR &= ~IxR_TIMER2;
      UpdateIFR(pMB);

      pMB->sy6522.TIMER2_LATCH.h = nValue;
      pMB->sy6522.TIMER2_COUNTER.w = pMB->sy6522.TIMER2_LATCH.w;
      break;
    case VIA_REG_SR:  // SERIAL_SHIFT
      break;
    case VIA_REG_ACR:  // ACR
      pMB->sy6522.ACR = nValue;
      break;
    case VIA_REG_PCR:  // PCR -  Used for Speech chip only
      pMB->sy6522.PCR = nValue;
      break;
    case VIA_REG_IFR:  // IFR
      // - Clear those bits which are set in the lower 7 bits.
      // - Can't clear bit 7 directly.
      nValue |= VIA_IFR_IRQ_FLAG;  // Set high bit
      nValue ^= VIA_IFR_BIT_MASK;  // Make mask
      pMB->sy6522.IFR &= nValue;
      UpdateIFR(pMB);
      break;
    case VIA_REG_IER:  // IER
      if (!(nValue & VIA_IFR_IRQ_FLAG)) {
        // Clear those bits which are set in the lower 7 bits.
        nValue ^= VIA_IFR_BIT_MASK;
        pMB->sy6522.IER &= nValue;
        UpdateIFR(pMB);

        // Check if timer has been disabled.
        if (pMB->sy6522.IER & IxR_TIMER1) {
          break;
        }

        if (pMB->nTimerStatus == 0) {
          break;
        }

        pMB->nTimerStatus = 0;

        // Stop timer
        StopTimer(pMB);
      } else {
        // Set those bits which are set in the lower 7 bits.
        nValue &= VIA_IFR_BIT_MASK;
        pMB->sy6522.IER |= nValue;
        UpdateIFR(pMB);
        StartTimer(pMB);
      }
      break;
    case VIA_REG_ORA_NO_HANDSHAKE:
      pMB->sy6522.ORA = nValue & pMB->sy6522.DDRA;
      break;
  }
}

static auto SY6522_Read(uint8_t nDevice, uint8_t nReg) -> uint8_t {
  g_bMB_RegAccessedFlag = true;
  g_bMB_Active = true;

  SY6522_AY8910* pMB = &g_MB[nDevice];

  switch (nReg) {
    case VIA_REG_ORB:  // ORB
      return pMB->sy6522.ORB;
    case VIA_REG_ORA:  // ORA
      return pMB->sy6522.ORA;
    case VIA_REG_DDRB:  // DDRB
      return pMB->sy6522.DDRB;
    case VIA_REG_DDRA:  // DDRA
      return pMB->sy6522.DDRA;
    case VIA_REG_T1L_C:  // TIMER1L_COUNTER
      // Clear Timer1 Interrupt Flag.
      pMB->sy6522.IFR &= ~IxR_TIMER1;
      UpdateIFR(pMB);
      return pMB->sy6522.TIMER1_COUNTER.l;
    case VIA_REG_T1H_C:  // TIMER1H_COUNTER
      return pMB->sy6522.TIMER1_COUNTER.h;
    case VIA_REG_T1L_L:  // TIMER1L_LATCH
      return pMB->sy6522.TIMER1_LATCH.l;
    case VIA_REG_T1H_L:  // TIMER1H_LATCH
      return pMB->sy6522.TIMER1_LATCH.h;
    case VIA_REG_T2L_C:  // TIMER2L_COUNTER
      // Clear Timer2 Interrupt Flag.
      pMB->sy6522.IFR &= ~IxR_TIMER2;
      UpdateIFR(pMB);
      return pMB->sy6522.TIMER2_COUNTER.l;
    case VIA_REG_T2H_C:  // TIMER2H_COUNTER
      return pMB->sy6522.TIMER2_COUNTER.h;
    case VIA_REG_SR:  // SERIAL_SHIFT
      return 0;
    case VIA_REG_ACR:  // ACR
      return pMB->sy6522.ACR;
    case VIA_REG_PCR:  // PCR
      return pMB->sy6522.PCR;
    case VIA_REG_IFR:  // IFR
      return pMB->sy6522.IFR;
    case VIA_REG_IER:  // IER
      return pMB->sy6522.IER | 0x80;
    case VIA_REG_ORA_NO_HANDSHAKE:
      return pMB->sy6522.ORA;
  }

  return 0;
}

void MB_Update() {
  if (!g_bMB_RegAccessedFlag) {
    if (!g_nMB_InActiveCycleCount) {
      g_nMB_InActiveCycleCount = g_nCumulativeCycles;
    } else if (g_nCumulativeCycles - g_nMB_InActiveCycleCount >
               static_cast<uint64_t>(g_fCurrentCLK6502) / 10) {
      // After 0.1 sec of Apple time, assume MB is not active
      g_bMB_Active = false;
    }
  } else {
    g_nMB_InActiveCycleCount = 0;
    g_bMB_RegAccessedFlag = false;
    g_bMB_Active = true;
  }

#if defined MOCKINGBOARD
  static int nNumSamplesError = 0;

  double n6522TimerPeriod = MB_GetFramePeriod();
  double nIrqFreq = g_fCurrentCLK6502 / n6522TimerPeriod -
                    0.5;  // GPH: Round DOWN instead of up
  int nNumSamplesPerPeriod = static_cast<int>(
      static_cast<double>(SAMPLE_RATE) / nIrqFreq);  // Eg. For 60Hz this is 735
  int nNumSamples =
      nNumSamplesPerPeriod + nNumSamplesError;  // Apply correction
  if (nNumSamples <= 0) {
    nNumSamples = 0;
  }
  if (nNumSamples > 2 * nNumSamplesPerPeriod) {
    nNumSamples = 2 * nNumSamplesPerPeriod;
  }

  if (nNumSamples > 0) {
    for (int i = 0; i < NUM_AY8910; i++) {
      int16_t* voices[3];
      voices[0] = reinterpret_cast<int16_t*>(ppAYVoiceBuffer[i * 3 + 0].get());
      voices[1] = reinterpret_cast<int16_t*>(ppAYVoiceBuffer[i * 3 + 1].get());
      voices[2] = reinterpret_cast<int16_t*>(ppAYVoiceBuffer[i * 3 + 2].get());
      AY8910Update(i, voices, nNumSamples);
    }

    double fAttenuation = g_bPhasorEnable ? 2.0 / 3.0 : 1.0;

    // MB output is stereo: L=AY0+AY2, R=AY1+AY3
    for (int i = 0; i < nNumSamples; i++) {
      int nDataL = 0;
      int nDataR = 0;

      for (int j = 0; j < 3; j++) {
        // Slot4
        nDataL += static_cast<int>(
            static_cast<double>(ppAYVoiceBuffer[0 * 3 + j].get()[i]) *
            fAttenuation);
        nDataR += static_cast<int>(
            static_cast<double>(ppAYVoiceBuffer[1 * 3 + j].get()[i]) *
            fAttenuation);

        // Slot5
        nDataL += static_cast<int>(
            static_cast<double>(ppAYVoiceBuffer[2 * 3 + j].get()[i]) *
            fAttenuation);
        nDataR += static_cast<int>(
            static_cast<double>(ppAYVoiceBuffer[3 * 3 + j].get()[i]) *
            fAttenuation);
      }

      // Cap the superpositioned output
      if (nDataL < -32768) {
        nDataL = -32768;
      } else if (nDataL > 32767) {
        nDataL = 32767;
      }

      if (nDataR < -32768) {
        nDataR = -32768;
      } else if (nDataR > 32767) {
        nDataR = 32767;
      }

      g_nMixBuffer[i * 2] = static_cast<short>(nDataL);
      g_nMixBuffer[i * 2 + 1] = static_cast<short>(nDataR);
    }

    DSUploadMockBuffer(g_nMixBuffer, nNumSamples * 2);

#ifndef HEADLESS
    if (g_pMBHost && g_pMBHost->RiffPutSamples) {
      g_pMBHost->RiffPutSamples(&g_nMixBuffer[0],
                                static_cast<uint32_t>(nNumSamples * 2));
    }
#endif
  }
#endif  // if defined MOCKINGBOARD
}

static auto PhasorIO(void* instance, uint16_t PC, uint16_t nAddr,
                     uint8_t bWrite, uint8_t nValue, uint32_t nCyclesLeft)
    -> uint8_t;
static auto MB_Read(void* instance, uint16_t PC, uint16_t nAddr, uint8_t bWrite,
                    uint8_t nValue, uint32_t nCyclesLeft) -> uint8_t;
static auto MB_Write(void* instance, uint16_t PC, uint16_t nAddr,
                     uint8_t bWrite, uint8_t nValue, uint32_t nCyclesLeft)
    -> uint8_t;

void MB_Initialize() {
  if (g_bDisableDirectSound) {
    g_SoundcardType = SC_NONE;
  } else {
    for (int i = 0; i < NUM_VOICES; i++) {
      ppAYVoiceBuffer[i].reset(static_cast<short*>(malloc(
          SAMPLE_RATE *
          sizeof(
              short))));  // Buffer can hold a max of 1 seconds worth of samples
    }

    for (int i = 0; i < NUM_AY8910; i++) {
      g_MB[i].nAY8910Number = i;
    }

    g_bMBAvailable = true;
    MB_Reset();
  }

  g_bMB_Active = (g_SoundcardType != SC_NONE);
}

// NB. Called when /g_fCurrentCLK6502/ changes
void MB_Reinitialize() {
  AY8910_InitClock(static_cast<int>(g_fCurrentCLK6502));
}

void MB_Destroy() {
  for (auto& i : ppAYVoiceBuffer) {
    i.reset();
  }
}

void MB_Reset() {
  g_n6522TimerPeriod = 0;
  g_nMBTimerDevice = 0;
  g_uLastCumulativeCycles = g_nCumulativeCycles;

  g_bMB_RegAccessedFlag = false;
  g_bMB_Active = true;

  g_nMB_InActiveCycleCount = 0;

  for (int i = 0; i < NUM_AY8910; i++) {
    ResetSY6522(&g_MB[i]);
    AY8910_reset(i);
  }

  g_nPhasorMode = 0;
  MB_Reinitialize();  // Reset CLK for AY8910s
}

static auto MB_Read(void* instance, uint16_t PC, uint16_t nAddr, uint8_t bWrite,
                    uint8_t nValue, uint32_t nCyclesLeft) -> uint8_t {
  (void)instance;
  (void)PC;
  (void)bWrite;
  (void)nValue;
  MB_UpdateCycles(nCyclesLeft);

  if (!IS_APPLE2() && !MemCheckSLOTCXROM()) {
    return mem[nAddr];
  }

  uint8_t nMB = ((nAddr >> 8) & ADDR_NIBBLE_MASK) - SLOT4;
  uint8_t nOffset = nAddr & 0xff;

  if (nOffset <= (SY6522A_Offset + 0x0F)) {
    return SY6522_Read(nMB * NUM_DEVS_PER_MB + SY6522_DEVICE_A,
                       nOffset & ADDR_NIBBLE_MASK);
  } else if ((nOffset >= SY6522B_Offset) &&
             (nOffset <= (SY6522B_Offset + 0x0F))) {
    return SY6522_Read(nMB * NUM_DEVS_PER_MB + SY6522_DEVICE_B,
                       nOffset & ADDR_NIBBLE_MASK);
  } else {
    return MemReadFloatingBus(nCyclesLeft);
  }
}

static auto MB_Write(void* instance, uint16_t PC, uint16_t nAddr,
                     uint8_t bWrite, uint8_t nValue, uint32_t nCyclesLeft)
    -> uint8_t {
  (void)instance;
  (void)PC;
  (void)bWrite;
  MB_UpdateCycles(nCyclesLeft);

  if (!IS_APPLE2() && !MemCheckSLOTCXROM()) {
    return 0;
  }

  uint8_t nMB = ((nAddr >> 8) & ADDR_NIBBLE_MASK) - SLOT4;
  uint8_t nOffset = nAddr & 0xff;

  if (nOffset <= (SY6522A_Offset + 0x0F)) {
    SY6522_Write(nMB * NUM_DEVS_PER_MB + SY6522_DEVICE_A,
                 nOffset & ADDR_NIBBLE_MASK, nValue);
  } else if ((nOffset >= SY6522B_Offset) &&
             (nOffset <= (SY6522B_Offset + 0x0F))) {
    SY6522_Write(nMB * NUM_DEVS_PER_MB + SY6522_DEVICE_B,
                 nOffset & ADDR_NIBBLE_MASK, nValue);
  }
  return 0;
}

static auto PhasorIO(void* instance, uint16_t PC, uint16_t nAddr,
                     uint8_t bWrite, uint8_t nValue, uint32_t nCyclesLeft)
    -> uint8_t {
  (void)instance;
  (void)PC;
  (void)bWrite;
  (void)nValue;
  if (!g_bPhasorEnable) {
    return MemReadFloatingBus(nCyclesLeft);
  }
  if (g_nPhasorMode < 2) {
    g_nPhasorMode = nAddr & 1;
  }

  uint8_t nMB = ((nAddr >> 4) & 0x07) - 4;
  uint8_t CS = 0;

  if (g_nPhasorMode == 0) {
    CS = ((nAddr & 0x80) >> 6) | ((nAddr & 0x10) >> 4);  // 0, 1, 2 or 3
  } else {
    CS = ((nAddr & 0x80) >> 7) + 1;  // 1 or 2
  }

  if (CS == 1) {
    SY6522_Write(nMB * NUM_DEVS_PER_MB + SY6522_DEVICE_A,
                 nAddr & ADDR_NIBBLE_MASK, nValue);
  } else if (CS == 2) {
    SY6522_Write(nMB * NUM_DEVS_PER_MB + SY6522_DEVICE_B,
                 nAddr & ADDR_NIBBLE_MASK, nValue);
  }
  return 0;
}

void MB_CheckIRQ() {}

void MB_Mute() {}

void MB_Demute() {}

void MB_StartOfCpuExecute() { g_uLastCumulativeCycles = g_nCumulativeCycles; }

void MB_EndOfVideoFrame() {
  if (g_SoundcardType == SC_NONE) {
    return;
  }
  if (!g_bFullSpeed && !g_bMBTimerIrqActive &&
      !(g_MB[0].sy6522.IFR & IxR_TIMER1)) {
    MB_Update();
  }
}

void MB_UpdateCycles(uint32_t uExecutedCycles) {
  if (g_SoundcardType == SC_NONE) {
    return;
  }

  CpuCalcCycles(uExecutedCycles);
  uint64_t uCycles = g_nCumulativeCycles - g_uLastCumulativeCycles;
  g_uLastCumulativeCycles = g_nCumulativeCycles;

  while (uCycles > 0) {
    uint16_t nClocks =
        (uCycles > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(uCycles);
    uCycles -= nClocks;

    for (int i = 0; i < NUM_SY6522; i++) {
      SY6522_AY8910* pMB = &g_MB[i];

      uint16_t OldTimer1 = pMB->sy6522.TIMER1_COUNTER.w;

      pMB->sy6522.TIMER1_COUNTER.w -= nClocks;
      pMB->sy6522.TIMER2_COUNTER.w -= nClocks;

      // Check for counter underflow
      bool bTimer1Underflow =
          (!(OldTimer1 & 0x8000) && (pMB->sy6522.TIMER1_COUNTER.w & 0x8000));

      if (bTimer1Underflow && (g_nMBTimerDevice == i) && g_bMBTimerIrqActive) {
        g_uTimer1IrqCount++;  // DEBUG

        pMB->sy6522.IFR |= IxR_TIMER1;
        UpdateIFR(pMB);

        if ((pMB->sy6522.ACR & RUNMODE) == RM_ONESHOT) {
          // One-shot mode
          StopTimer(pMB);  // Phasor's playback code uses one-shot mode
        } else {
          // Free-running mode
          // - Ultima4/5 change ACCESS_TIMER1 after a couple of IRQs into tune
          pMB->sy6522.TIMER1_COUNTER.w = pMB->sy6522.TIMER1_LATCH.w;
          StartTimer(pMB);
        }

        if (!g_bFullSpeed) {
          MB_Update();
        }
      }
    }
  }

  if (!g_bMB_RegAccessedFlag) {
    if (!g_nMB_InActiveCycleCount) {
      g_nMB_InActiveCycleCount = g_nCumulativeCycles;
    } else if (g_nCumulativeCycles - g_nMB_InActiveCycleCount >
               static_cast<uint64_t>(g_fCurrentCLK6502) / 10) {
      // After 0.1 sec of Apple time with no MB register access, assume MB is
      // inactive
      g_bMB_Active = false;
    }
  } else {
    g_nMB_InActiveCycleCount = 0;
    g_bMB_RegAccessedFlag = false;
    g_bMB_Active = true;
  }
}

auto MB_GetSoundcardType() -> eSOUNDCARDTYPE { return g_SoundcardType; }

void MB_SetSoundcardType(eSOUNDCARDTYPE NewSoundcardType) {
  if ((NewSoundcardType == SC_UNINIT) ||
      (g_SoundcardType == NewSoundcardType)) {
    return;
  }

  g_SoundcardType = NewSoundcardType;

  if (g_SoundcardType == SC_NONE) {
    MB_Mute();
  }

  g_bPhasorEnable = (g_SoundcardType == SC_PHASOR);
}

auto MB_GetFramePeriod() -> double {
  return (g_bMBTimerIrqActive || (g_MB[0].sy6522.IFR & IxR_TIMER1))
             ? static_cast<double>(g_n6522TimerPeriod)
             : g_f6522TimerPeriod_NoIRQ;
}

auto MB_IsActive() -> bool {
  // Ignore /g_bMBTimerIrqActive/ as timer's irq handler will access 6522 regs
  // affecting /g_bMB_Active/
  return g_bMB_Active;
}

auto MB_GetVolume() -> uint32_t { return 0; }

void MB_SetVolume(uint32_t dwVolume, uint32_t dwVolumeMax) {
  (void)dwVolume;
  (void)dwVolumeMax;
}

auto MB_GetSnapshot(SS_CARD_MOCKINGBOARD* pSS, uint32_t dwSlot) -> uint32_t {
  pSS->Hdr.UnitHdr.dwLength = sizeof(SS_CARD_EMPTY);
  pSS->Hdr.UnitHdr.dwVersion = MAKE_VERSION(1, 0, 0, 0);

  pSS->Hdr.dwSlot = dwSlot;
  pSS->Hdr.dwType = CT_Mockingboard;

  uint32_t nMbCardNum = dwSlot - SLOT4;
  uint32_t nDeviceNum = nMbCardNum * 2;
  SY6522_AY8910* pMB = &g_MB[nDeviceNum];

  for (auto& i : pSS->Unit) {
    memcpy(&i.RegsSY6522, &pMB->sy6522, sizeof(SY6522));
    memcpy(&i.RegsAY8910, AY8910_GetRegsPtr(nDeviceNum), 16);
    memcpy(&i.RegsSSI263, &pMB->SpeechChip, sizeof(SSI263A));
    i.nAYCurrentRegister = pMB->nAYCurrentRegister;

    nDeviceNum++;
    pMB++;
  }

  return 0;
}

auto MB_SetSnapshot(SS_CARD_MOCKINGBOARD* pSS, uint32_t) -> uint32_t {
  if (pSS->Hdr.UnitHdr.dwVersion != MAKE_VERSION(1, 0, 0, 0)) {
    return -1;
  }

  uint32_t nMbCardNum = pSS->Hdr.dwSlot - SLOT4;
  uint32_t nDeviceNum = nMbCardNum * 2;
  SY6522_AY8910* pMB = &g_MB[nDeviceNum];

  for (auto& i : pSS->Unit) {
    memcpy(&pMB->sy6522, &i.RegsSY6522, sizeof(SY6522));
    memcpy(AY8910_GetRegsPtr(nDeviceNum), &i.RegsAY8910, 16);
    memcpy(&pMB->SpeechChip, &i.RegsSSI263, sizeof(SSI263A));
    pMB->nAYCurrentRegister = i.nAYCurrentRegister;

    StartTimer(pMB);  // Attempt to start timer

    nDeviceNum++;
    pMB++;
  }

  return 0;
}

static auto MB_ABI_Init(int slot, HostInterface_t* host) -> void* {
  g_pMBHost = host;
  g_nMB_Slot = slot;

  static bool s_mb_initialized = false;
  if (!s_mb_initialized) {
    MB_Initialize();
    s_mb_initialized = true;
  }

  // MB_Initialize already calls RegisterIoHandler, but we also register via
  // the host interface to ensure the manager is aware.
  host->RegisterIO(slot, PhasorIO, PhasorIO, MB_Read, MB_Write);

  return reinterpret_cast<void*>(1);  // Dummy instance
}

static void MB_ABI_Reset(void* instance) {
  (void)instance;
  MB_Reset();
}

static void MB_ABI_Shutdown(void* instance) {
  (void)instance;
  MB_Destroy();
}

static void MB_ABI_Think(void* instance, uint32_t cycles) {
  (void)instance;
  MB_UpdateCycles(cycles);
}

static auto MB_ABI_SaveState(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus {
  (void)instance;
  if (!buffer || !size || *size < sizeof(SS_CARD_MOCKINGBOARD)) {
    if (size) *size = sizeof(SS_CARD_MOCKINGBOARD);
    return PERIPHERAL_ERROR;
  }
  MB_GetSnapshot(static_cast<SS_CARD_MOCKINGBOARD*>(buffer), g_nMB_Slot);
  *size = sizeof(SS_CARD_MOCKINGBOARD);
  return PERIPHERAL_OK;
}

static auto MB_ABI_LoadState(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus {
  (void)instance;
  if (!buffer || size < sizeof(SS_CARD_MOCKINGBOARD)) {
    return PERIPHERAL_ERROR;
  }
  MB_SetSnapshot(const_cast<SS_CARD_MOCKINGBOARD*>(
                     static_cast<const SS_CARD_MOCKINGBOARD*>(buffer)),
                 g_nMB_Slot);
  return PERIPHERAL_OK;
}

Peripheral_t g_mockingboard_peripheral = {
    LINAPPLE_ABI_VERSION,
    "Mockingboard",
    0xFE,  // Slots 1-7
    MB_ABI_Init,
    MB_ABI_Reset,
    MB_ABI_Shutdown,
    MB_ABI_Think,
    nullptr,  // on_vblank
    MB_ABI_SaveState,
    MB_ABI_LoadState,
    nullptr,  // command
    nullptr   // query
};

extern "C" void Register_Mockingboard() {
  Peripheral_Register_Builtin(&g_mockingboard_peripheral);
}

#ifdef BUILD_SHARED_PERIPHERAL
EXPORT_PERIPHERAL(g_mockingboard_peripheral)
#endif
#endif
