// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory,
// cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
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

/* Description: Mockingboard sound card emulation
 *
 * This module emulates the SY6522 VIA and AY-3-8910 PSG chips.
 */

#include "apple2/peripherals/mockingboard/Mockingboard.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Structs.h"
#include "apple2/chips/AY8910.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/Peripheral.h"

// NOLINTBEGIN(cppcoreguidelines-use-enum-class)
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

enum {
  AY_PB_BC1 = 0x01,
  AY_PB_BDIR = 0x02,
  AY_PB_RESET_N = 0x04,
  AY_FUNC_WRITE = 0x06,
  AY_FUNC_LATCH = 0x07,
  AY_REG_MASK = 0x0F
};
// NOLINTEND(cppcoreguidelines-use-enum-class)

static constexpr int NUM_VOICES_PER_CHIP = 3;
static constexpr int CHIPS_PER_CARD = 2;
static constexpr int VOICES_PER_CARD = NUM_VOICES_PER_CHIP * CHIPS_PER_CARD;

static constexpr int16_t AUDIO_CLAMP_MIN = -32768;
static constexpr int16_t AUDIO_CLAMP_MAX = 32767;

static constexpr double PHASOR_ATTENUATION = 2.0 / 3.0;
static constexpr double DEFAULT_ATTENUATION = 1.0;
static constexpr uint64_t INACTIVE_THRESHOLD_DIVISOR = 10;
static constexpr uint64_t HZ_60_DIVISOR = 60;

namespace {
constexpr int MB_DEFAULT_SLOT = 4;
constexpr int MB_TYPE_STR_MAX = 16;
constexpr uint8_t MB_IO_ADDR_HI_MASK = 0xFF;
constexpr uint8_t VIA_REG_MASK = 0x0F;
constexpr size_t MB_MAX_SLOTS = 8;
}  // namespace

struct SY6522_AY8910 {
  SY6522 sy6522 = {};
  SSI263A SpeechChip = {};
  AY8910 ay_chip = {};
  uint16_t nAYCurrentRegister = 0;
  uint8_t nAY8910Number = 0;
  int nTimerStatus = 0;
};

struct MockingboardPeripheral_t {
  std::array<SY6522_AY8910, CHIPS_PER_CARD> chips = {};
  std::array<std::unique_ptr<int16_t[]>, VOICES_PER_CARD> voice_buffers = {};
  int16_t mix_buffer[SAMPLE_RATE * 2] = {};  // Stereo mix
  uint32_t n6522TimerPeriod = 0;
  uint16_t nMBTimerDevice = 0;
  uint64_t uLastCumulativeCycles = 0;
  uint64_t nMB_InActiveCycleCount = 0;
  uint64_t last_60hz = 0;
  bool bMB_RegAccessedFlag = false;
  bool bMB_Active = false;
  bool bTimerIrqActive = false;
  uint32_t uTimer1IrqCount = 0;
  eSOUNDCARDTYPE type = SC_MOCKINGBOARD;
  bool phasor_native = false;
  HostInterface_t* host = nullptr;
  int slot = 0;

  MockingboardPeripheral_t() {
    for (auto& buf : voice_buffers) {
      buf.reset(new int16_t[SAMPLE_RATE]);
    }
    for (int i = 0; i < CHIPS_PER_CARD; ++i) {
      const auto idx = static_cast<size_t>(i);
      chips.at(idx).nAY8910Number = static_cast<uint8_t>(i);
      AY8910_reset_instance(&chips.at(idx).ay_chip);
    }
  }
};

static std::array<MockingboardPeripheral_t*, MB_MAX_SLOTS> active_mb_instances =
    {nullptr};

static void StartTimer(MockingboardPeripheral_t* mp, int chip_idx) {
  SY6522_AY8910* pMB = &mp->chips.at(static_cast<size_t>(chip_idx));
  if (chip_idx != SY6522_DEVICE_A) {
    return;
  }
  if ((pMB->sy6522.IER & IxR_TIMER1) == 0x00) {
    return;
  }

  uint16_t nPeriod = pMB->sy6522.TIMER1_LATCH.w;
  if (nPeriod <= TIMER_LOW_BYTE_MAX) {
    return;
  }

  pMB->nTimerStatus = 1;
  mp->n6522TimerPeriod = nPeriod;
  mp->bTimerIrqActive = true;
  mp->bTimerIrqActive = true;  // Legacy support
  mp->nMBTimerDevice = static_cast<uint16_t>(chip_idx);
}

static void StopTimer(MockingboardPeripheral_t* mp, int chip_idx) {
  mp->chips.at(static_cast<size_t>(chip_idx)).nTimerStatus = 0;
  mp->bTimerIrqActive = false;
  mp->bTimerIrqActive = false;  // Legacy support
}

static void UpdateIFR(MockingboardPeripheral_t* mp, int chip_idx) {
  SY6522_AY8910* pMB = &mp->chips.at(static_cast<size_t>(chip_idx));
  pMB->sy6522.IFR &= VIA_IFR_BIT_MASK;

  if (pMB->sy6522.IFR & pMB->sy6522.IER & VIA_IFR_BIT_MASK) {
    pMB->sy6522.IFR |= VIA_IFR_IRQ_FLAG;
  }

  uint32_t bIRQ = 0;
  for (auto& i : mp->chips) {
    bIRQ |= i.sy6522.IFR & VIA_IFR_IRQ_FLAG;
  }

  if (mp->host && mp->host->AssertIrq) {
    mp->host->AssertIrq(mp->slot, bIRQ != 0);
  } else {
    if (bIRQ != 0) {
      CpuIrqAssert(IS_6522);
    } else {
      CpuIrqDeassert(IS_6522);
    }
  }
}

static void AY8910_Write_Instance(MockingboardPeripheral_t* mp, uint8_t nDevice,
                                  uint8_t nValue, uint8_t nAYDevice) {
  (void)nAYDevice;
  SY6522_AY8910* pMB = &mp->chips.at(static_cast<size_t>(nDevice));

  if ((nValue & AY_PB_RESET_N) == 0) {
    AY8910_reset_instance(&pMB->ay_chip);
  } else {
    int nBDIR = (nValue & AY_PB_BDIR) ? 1 : 0;
    int nBC1 = (nValue & AY_PB_BC1) ? 1 : 0;
    int nAYFunc = (nBDIR << 2) | (1 << 1) | nBC1;

    if (nAYFunc == AY_FUNC_WRITE) {
      AY8910_write_instance(&pMB->ay_chip, pMB->nAYCurrentRegister,
                            pMB->sy6522.ORA,
                            static_cast<int>(g_fCurrentCLK6502), SAMPLE_RATE);
    } else if (nAYFunc == AY_FUNC_LATCH) {
      if (pMB->sy6522.ORA <= AY_REG_MASK) {
        pMB->nAYCurrentRegister =
            static_cast<uint16_t>(pMB->sy6522.ORA & AY_REG_MASK);
      }
    }
  }
}

static void SY6522_Write_Instance(MockingboardPeripheral_t* mp, uint8_t nDevice,
                                  uint8_t nReg, uint8_t nValue) {
  mp->bMB_RegAccessedFlag = true;
  if (!g_bFullSpeed) {
    mp->bMB_Active = true;
  }
  SY6522_AY8910* pMB = &mp->chips.at(static_cast<size_t>(nDevice));

  // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
  switch (nReg) {
    case VIA_REG_ORB:
      nValue &= pMB->sy6522.DDRB;
      pMB->sy6522.ORB = nValue;
      if (mp->type == SC_PHASOR) {
        int nAY_CS = mp->phasor_native ? (~(nValue >> 3) & 3) : 1;
        if ((nAY_CS & 1) != 0) AY8910_Write_Instance(mp, nDevice, nValue, 0);
      } else {
        AY8910_Write_Instance(mp, nDevice, nValue, 0);
      }
      break;
    case VIA_REG_ORA:
      pMB->sy6522.ORA = nValue & pMB->sy6522.DDRA;
      break;
    case VIA_REG_DDRB:
      pMB->sy6522.DDRB = nValue;
      break;
    case VIA_REG_DDRA:
      pMB->sy6522.DDRA = nValue;
      break;
    case VIA_REG_T1L_C:
    case VIA_REG_T1L_L:
      pMB->sy6522.TIMER1_LATCH.l = nValue;
      break;
    case VIA_REG_T1H_C:
      pMB->sy6522.IFR &= ~IxR_TIMER1;
      UpdateIFR(mp, nDevice);
      pMB->sy6522.TIMER1_LATCH.h = nValue;
      pMB->sy6522.TIMER1_COUNTER.w = pMB->sy6522.TIMER1_LATCH.w;
      StartTimer(mp, nDevice);
      break;
    case VIA_REG_T1H_L:
      pMB->sy6522.TIMER1_LATCH.h = nValue;
      pMB->sy6522.IFR &= ~IxR_TIMER1;
      UpdateIFR(mp, nDevice);
      break;
    case VIA_REG_T2L_C:
      pMB->sy6522.TIMER2_LATCH.l = nValue;
      break;
    case VIA_REG_T2H_C:
      pMB->sy6522.IFR &= ~IxR_TIMER2;
      UpdateIFR(mp, nDevice);
      pMB->sy6522.TIMER2_LATCH.h = nValue;
      pMB->sy6522.TIMER2_COUNTER.w = pMB->sy6522.TIMER2_LATCH.w;
      break;
    case VIA_REG_ACR:
      pMB->sy6522.ACR = nValue;
      break;
    case VIA_REG_PCR:
      pMB->sy6522.PCR = nValue;
      break;
    case VIA_REG_IFR:
      nValue |= VIA_IFR_IRQ_FLAG;
      nValue ^= VIA_IFR_BIT_MASK;
      pMB->sy6522.IFR &= nValue;
      UpdateIFR(mp, nDevice);
      break;
    case VIA_REG_IER:
      if (!(nValue & VIA_IFR_IRQ_FLAG)) {
        nValue ^= VIA_IFR_BIT_MASK;
        pMB->sy6522.IER &= nValue;
        UpdateIFR(mp, nDevice);
        if (!(pMB->sy6522.IER & IxR_TIMER1) && pMB->nTimerStatus != 0) {
          StopTimer(mp, nDevice);
        }
      } else {
        nValue &= VIA_IFR_BIT_MASK;
        pMB->sy6522.IER |= nValue;
        UpdateIFR(mp, nDevice);
        StartTimer(mp, nDevice);
      }
      break;
    case VIA_REG_ORA_NO_HANDSHAKE:
      pMB->sy6522.ORA = nValue & pMB->sy6522.DDRA;
      break;
    default:
      break;
  }
  // NOLINTEND(cppcoreguidelines-pro-type-union-access)
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
// Justification: Functions are part of the Peripheral ABI or internal
// helpers that mimic it, where parameter order is fixed or follows convention.

static auto SY6522_Read_Instance(MockingboardPeripheral_t* mp, uint8_t nDevice,
                                 uint8_t nReg) -> uint8_t {
  mp->bMB_RegAccessedFlag = true;
  if (!g_bFullSpeed) {
    mp->bMB_Active = true;
  }
  SY6522_AY8910* pMB = &mp->chips.at(static_cast<size_t>(nDevice));

  // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
  switch (nReg) {
    case VIA_REG_ORB:
      return pMB->sy6522.ORB;
    case VIA_REG_ORA:
      return pMB->sy6522.ORA;
    case VIA_REG_DDRB:
      return pMB->sy6522.DDRB;
    case VIA_REG_DDRA:
      return pMB->sy6522.DDRA;
    case VIA_REG_T1L_C:
      pMB->sy6522.IFR &= ~IxR_TIMER1;
      UpdateIFR(mp, nDevice);
      return pMB->sy6522.TIMER1_COUNTER.l;
    case VIA_REG_T1H_C:
      return pMB->sy6522.TIMER1_COUNTER.h;
    case VIA_REG_T1L_L:
      return pMB->sy6522.TIMER1_LATCH.l;
    case VIA_REG_T1H_L:
      return pMB->sy6522.TIMER1_LATCH.h;
    case VIA_REG_T2L_C:
      pMB->sy6522.IFR &= ~IxR_TIMER2;
      UpdateIFR(mp, nDevice);
      return pMB->sy6522.TIMER2_COUNTER.l;
    case VIA_REG_T2H_C:
      return pMB->sy6522.TIMER2_COUNTER.h;
    case VIA_REG_ACR:
      return pMB->sy6522.ACR;
    case VIA_REG_PCR:
      return pMB->sy6522.PCR;
    case VIA_REG_IFR:
      return pMB->sy6522.IFR;
    case VIA_REG_IER:
      return static_cast<uint8_t>(pMB->sy6522.IER | VIA_IFR_IRQ_FLAG);
    case VIA_REG_ORA_NO_HANDSHAKE:
      return pMB->sy6522.ORA;
    default:
      break;
  }
  // NOLINTEND(cppcoreguidelines-pro-type-union-access)
  return 0;
}

static void MB_Update_Instance(MockingboardPeripheral_t* mp) {
  if (mp->type == SC_NONE || !mp->bMB_Active) return;

  double n6522TimerPeriod =
      (mp->bTimerIrqActive || (mp->chips.at(0).sy6522.IFR & IxR_TIMER1))
          ? static_cast<double>(mp->n6522TimerPeriod)
          : (CLOCK_6502 / 60.0);

  double nIrqFreq = g_fCurrentCLK6502 / n6522TimerPeriod;
  int nNumSamples =
      static_cast<int>(static_cast<double>(SAMPLE_RATE) / nIrqFreq);

  if (nNumSamples > 0) {
    if (static_cast<uint32_t>(nNumSamples) > SAMPLE_RATE) {
      nNumSamples = static_cast<int>(SAMPLE_RATE);
    }

    for (size_t i = 0; i < CHIPS_PER_CARD; i++) {
      int16_t* voices[3];
      // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      voices[0] =
          &mp->voice_buffers.at(static_cast<size_t>(i) * 3 + 0).get()[0];
      voices[1] =
          &mp->voice_buffers.at(static_cast<size_t>(i) * 3 + 1).get()[0];
      voices[2] =
          &mp->voice_buffers.at(static_cast<size_t>(i) * 3 + 2).get()[0];
      // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      AY8910_update_instance(&mp->chips.at(i).ay_chip, voices, nNumSamples,
                             static_cast<int>(g_fCurrentCLK6502), SAMPLE_RATE);
    }

    const double fAttenuation =
        (mp->type == SC_PHASOR) ? PHASOR_ATTENUATION : DEFAULT_ATTENUATION;

    // Mix to stereo
    for (int i = 0; i < nNumSamples; i++) {
      int nDataL = 0;
      int nDataR = 0;

      for (int j = 0; j < 3; j++) {
        nDataL += static_cast<int>(
            static_cast<double>(mp->voice_buffers.at(0 * 3 + j).get()[i]) *
            fAttenuation);
        nDataR += static_cast<int>(
            static_cast<double>(mp->voice_buffers.at(1 * 3 + j).get()[i]) *
            fAttenuation);
      }

      if (nDataL < AUDIO_CLAMP_MIN) {
        nDataL = AUDIO_CLAMP_MIN;
      } else if (nDataL > AUDIO_CLAMP_MAX) {
        nDataL = AUDIO_CLAMP_MAX;
      }
      if (nDataR < AUDIO_CLAMP_MIN) {
        nDataR = AUDIO_CLAMP_MIN;
      } else if (nDataR > AUDIO_CLAMP_MAX) {
        nDataR = AUDIO_CLAMP_MAX;
      }

      // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      mp->mix_buffer[static_cast<ptrdiff_t>(i) * 2] =
          static_cast<int16_t>(nDataL);
      mp->mix_buffer[static_cast<ptrdiff_t>(i) * 2 + 1] =
          static_cast<int16_t>(nDataR);
      // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }

    if (mp->host != nullptr && mp->host->AudioPushSamples != nullptr) {
      mp->host->AudioPushSamples(mp, mp->mix_buffer,
                                 static_cast<uint32_t>(nNumSamples * 2));
    }
  }
}

static void MB_UpdateCycles_Instance(MockingboardPeripheral_t* mp,
                                     uint32_t uExecutedCycles) {
  (void)uExecutedCycles;
  if (mp->type == SC_NONE) return;

  uint64_t uCycles = g_nCumulativeCycles - mp->uLastCumulativeCycles;
  mp->uLastCumulativeCycles = g_nCumulativeCycles;

  while (uCycles > 0) {
    constexpr uint64_t MAX_CLOCKS_U16 = 0xFFFF;
    constexpr uint16_t MAX_CLOCKS_U16_VAL = 0xFFFF;
    uint16_t nClocks = (uCycles > MAX_CLOCKS_U16)
                           ? MAX_CLOCKS_U16_VAL
                           : static_cast<uint16_t>(uCycles);
    uCycles -= nClocks;

    for (size_t i = 0; i < CHIPS_PER_CARD; i++) {
      SY6522_AY8910* pMB = &mp->chips.at(i);
      // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
      uint16_t OldTimer1 = pMB->sy6522.TIMER1_COUNTER.w;
      pMB->sy6522.TIMER1_COUNTER.w =
          static_cast<uint16_t>(pMB->sy6522.TIMER1_COUNTER.w - nClocks);
      pMB->sy6522.TIMER2_COUNTER.w =
          static_cast<uint16_t>(pMB->sy6522.TIMER2_COUNTER.w - nClocks);

      constexpr uint16_t TIMER_MSB_BIT = 0x8000;
      bool bTimer1Underflow = (!(OldTimer1 & TIMER_MSB_BIT) &&
                               (pMB->sy6522.TIMER1_COUNTER.w & TIMER_MSB_BIT));
      // NOLINTEND(cppcoreguidelines-pro-type-union-access)
      if (bTimer1Underflow &&
          (mp->nMBTimerDevice == static_cast<uint16_t>(i)) &&
          mp->bTimerIrqActive) {
        mp->uTimer1IrqCount++;
        pMB->sy6522.IFR |= IxR_TIMER1;
        UpdateIFR(mp, static_cast<int>(i));

        if ((pMB->sy6522.ACR & RUNMODE) == RM_ONESHOT) {
          StopTimer(mp, static_cast<int>(i));
        } else {
          pMB->sy6522.TIMER1_COUNTER.w = pMB->sy6522.TIMER1_LATCH.w;
          StartTimer(mp, static_cast<int>(i));
        }
        MB_Update_Instance(mp);
      }
    }
  }

  if (!mp->bMB_RegAccessedFlag) {
    if (!mp->nMB_InActiveCycleCount) {
      mp->nMB_InActiveCycleCount = g_nCumulativeCycles;
    } else if (g_nCumulativeCycles - mp->nMB_InActiveCycleCount >
               static_cast<uint64_t>(g_fCurrentCLK6502) / 10) {
      mp->bMB_Active = false;
    }
  } else {
    mp->nMB_InActiveCycleCount = 0;
    mp->bMB_RegAccessedFlag = false;
    if (!g_bFullSpeed) {
      mp->bMB_Active = true;
    }
  }
}

static auto MB_IO_Read(void* instance, uint16_t pc, uint16_t addr,
                       uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  (void)pc;
  (void)write;
  (void)val;
  if (!instance) return MemReadFloatingBus(cycles_left);
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  MB_UpdateCycles_Instance(mp, cycles_left);
  uint8_t nOffset = addr & MB_IO_ADDR_HI_MASK;
  if (nOffset <= (SY6522A_Offset + VIA_REG_MASK)) {
    return SY6522_Read_Instance(mp, SY6522_DEVICE_A, nOffset & VIA_REG_MASK);
  }
  if ((nOffset >= SY6522B_Offset) &&
      (nOffset <= (SY6522B_Offset + VIA_REG_MASK))) {
    return SY6522_Read_Instance(mp, SY6522_DEVICE_B, nOffset & VIA_REG_MASK);
  }
  return MemReadFloatingBus(cycles_left);
}

static auto MB_IO_Write(void* instance, uint16_t pc, uint16_t addr,
                        uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  (void)pc;
  (void)write;
  if (!instance) return 0;
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  MB_UpdateCycles_Instance(mp, cycles_left);

  uint8_t nOffset = addr & MB_IO_ADDR_HI_MASK;
  if (nOffset <= (SY6522A_Offset + VIA_REG_MASK)) {
    SY6522_Write_Instance(mp, SY6522_DEVICE_A, nOffset & VIA_REG_MASK, val);
  } else if ((nOffset >= SY6522B_Offset) &&
             (nOffset <= (SY6522B_Offset + VIA_REG_MASK))) {
    SY6522_Write_Instance(mp, SY6522_DEVICE_B, nOffset & VIA_REG_MASK, val);
  }
  return 0;
}

static auto PhasorIO(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
                     uint8_t val, uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)write;
  if (!instance) return MemReadFloatingBus(cycles_left);
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);

  if (!mp->phasor_native) {
    mp->phasor_native = (addr & 1) != 0;
  }

  uint8_t CS = 0;
  if (mp->phasor_native) {
    constexpr int MSB_SHIFT_6 = 6;
    constexpr int BIT_4_SHIFT_4 = 4;
    constexpr uint8_t BIT_4_MASK = 0x10;
    CS = ((addr & VIA_IFR_IRQ_FLAG) >> MSB_SHIFT_6) |
         ((addr & BIT_4_MASK) >> BIT_4_SHIFT_4);  // 0, 1, 2 or 3
  } else {
    constexpr int MSB_SHIFT_7 = 7;
    CS = ((addr & VIA_IFR_IRQ_FLAG) >> MSB_SHIFT_7) + 1;  // 1 or 2
  }

  if (CS == 1) {
    SY6522_Write_Instance(mp, SY6522_DEVICE_A, addr & VIA_REG_MASK, val);
  } else if (CS == 2) {
    SY6522_Write_Instance(mp, SY6522_DEVICE_B, addr & VIA_REG_MASK, val);
  }
  return 0;
}

static auto MB_ABI_Init(int slot, HostInterface_t* host) -> void* {
  const auto s_idx = static_cast<size_t>(slot);
  if (active_mb_instances.at(s_idx)) {
    active_mb_instances.at(s_idx)->host = host;
    active_mb_instances.at(s_idx)->slot = slot;
    return active_mb_instances.at(s_idx);
  }

  auto* mp = new MockingboardPeripheral_t{};
  mp->host = host;
  mp->slot = slot;
  active_mb_instances.at(s_idx) = mp;

  // Use SC_PHASOR if configured
  char type_str[MB_TYPE_STR_MAX];
  if (host->GetConfig("Mockingboard", "Type", type_str, sizeof(type_str))) {
    if (strcmp(type_str, "Phasor") == 0) {
      mp->type = SC_PHASOR;
    }
  }

  if (mp->type == SC_PHASOR) {
    host->RegisterIO(slot, PhasorIO, PhasorIO, MB_IO_Read, MB_IO_Write);
  } else {
    host->RegisterIO(slot, nullptr, nullptr, MB_IO_Read, MB_IO_Write);
  }
  return mp;
}

static void MB_ABI_Reset(void* instance) {
  if (!instance) return;
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  mp->n6522TimerPeriod = 0;
  mp->nMBTimerDevice = 0;
  mp->uLastCumulativeCycles = g_nCumulativeCycles;
  mp->bMB_RegAccessedFlag = false;
  mp->bMB_Active = false;
  mp->nMB_InActiveCycleCount = 0;
  mp->last_60hz = g_nCumulativeCycles;
  mp->phasor_native = false;

  for (int i = 0; i < CHIPS_PER_CARD; i++) {
    memset(&mp->chips.at(static_cast<size_t>(i)).sy6522, 0, sizeof(SY6522));
    AY8910_reset_instance(&mp->chips.at(static_cast<size_t>(i)).ay_chip);
    mp->chips.at(static_cast<size_t>(i)).nTimerStatus = 0;
    mp->chips.at(static_cast<size_t>(i)).nAYCurrentRegister = 0;
  }
}

static void MB_ABI_Shutdown(void* instance) {
  if (!instance) return;
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  active_mb_instances.at(static_cast<size_t>(mp->slot)) = nullptr;
  delete mp;
}

static void MB_ABI_Think(void* instance, uint32_t cycles) {
  if (!instance) return;
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  MB_UpdateCycles_Instance(mp, cycles);

  if (!mp->bTimerIrqActive && !(mp->chips.at(0).sy6522.IFR & IxR_TIMER1)) {
    if (mp->last_60hz == 0) {
      mp->last_60hz = g_nCumulativeCycles;
    }
    if (g_nCumulativeCycles - mp->last_60hz >
        static_cast<uint64_t>(g_fCurrentCLK6502) / HZ_60_DIVISOR) {
      mp->last_60hz = g_nCumulativeCycles;
      MB_Update_Instance(mp);
    }
  }
}

static void MB_ABI_OnVBlank(void* instance, bool vblank) {
  (void)vblank;
  (void)instance;
  // End of frame logic
}
// NOLINTEND(bugprone-easily-swappable-parameters)

Peripheral_t g_mockingboard_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.mockingboard",
    .name = "Mockingboard",
    .description = "Dual AY-3-8910 sound card emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = MB_DEFAULT_SLOT,
    .init = MB_ABI_Init,
    .reset = MB_ABI_Reset,
    .shutdown = MB_ABI_Shutdown,
    .think = MB_ABI_Think,
    .on_vblank = MB_ABI_OnVBlank,
    .save_state = nullptr,
    .load_state = nullptr,
    .command = nullptr,
    .query = nullptr};

PERIPHERAL_REGISTER(g_mockingboard_peripheral)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory,
// cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
