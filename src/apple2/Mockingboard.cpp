// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables) Justification: This file
// implements the C11-compatible Peripheral ABI. It requires void* pointers for
// instance state, raw memory management, and static global state to bridge with
// the core C interface and maintain peripheral singletons.

#include "core/Common_Globals.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

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

static const int NUM_VOICES_PER_CHIP = 3;
static const int CHIPS_PER_CARD = 2;
static const int VOICES_PER_CARD = NUM_VOICES_PER_CHIP * CHIPS_PER_CARD;

typedef struct {
  SY6522 sy6522;
  SSI263A SpeechChip;
  AY8910 ay_chip;
  uint16_t nAYCurrentRegister;
  uint8_t nAY8910Number;
  int nTimerStatus;
} SY6522_AY8910;

struct MockingboardPeripheral_t {
  std::array<SY6522_AY8910, CHIPS_PER_CARD> chips;
  std::array<std::unique_ptr<short[]>, VOICES_PER_CARD> voice_buffers;
  short mix_buffer[SAMPLE_RATE * 2]; // Stereo mix
  uint32_t n6522TimerPeriod = 0;
  uint16_t nMBTimerDevice = 0;
  uint64_t uLastCumulativeCycles = 0;
  uint64_t nMB_InActiveCycleCount = 0;
  bool bMB_RegAccessedFlag = false;
  bool bMB_Active = true;
  bool bTimerIrqActive = false;
  eSOUNDCARDTYPE type = SC_MOCKINGBOARD;
  bool phasor_native = false;
  HostInterface_t* host = nullptr;
  int slot = 0;

  MockingboardPeripheral_t() {
    for (auto& buf : voice_buffers) {
      buf.reset(new short[SAMPLE_RATE]);
    }
    for (int i = 0; i < CHIPS_PER_CARD; ++i) {
      chips[i].nAY8910Number = static_cast<uint8_t>(i);
      AY8910_reset_instance(&chips[i].ay_chip);
    }
  }
};

static MockingboardPeripheral_t* active_mb_instances[8] = {nullptr};

// Legacy global vars (kept for now, will be removed)
bool g_bMBTimerIrqActive = false;
uint32_t g_uTimer1IrqCount = 0;

static void StartTimer(MockingboardPeripheral_t* mp, int chip_idx) {
  SY6522_AY8910* pMB = &mp->chips[chip_idx];
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
  g_bMBTimerIrqActive = true; // Legacy support
  mp->nMBTimerDevice = static_cast<uint16_t>(chip_idx);
}

static void StopTimer(MockingboardPeripheral_t* mp, int chip_idx) {
  mp->chips[chip_idx].nTimerStatus = 0;
  mp->bTimerIrqActive = false;
  g_bMBTimerIrqActive = false; // Legacy support
}

static void UpdateIFR(MockingboardPeripheral_t* mp, int chip_idx) {
  SY6522_AY8910* pMB = &mp->chips[chip_idx];
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
    if (bIRQ) {
      CpuIrqAssert(IS_6522);
    } else {
      CpuIrqDeassert(IS_6522);
    }
  }
}

static void AY8910_Write_Instance(MockingboardPeripheral_t* mp, uint8_t nDevice,
                                  uint8_t nValue, uint8_t nAYDevice) {
  (void)nAYDevice;
  SY6522_AY8910* pMB = &mp->chips[nDevice];
  
  if ((nValue & 4) == 0) {
    AY8910_reset_instance(&pMB->ay_chip);
  } else {
    int nBDIR = (nValue & 2) ? 1 : 0;
    int nBC1 = nValue & 1;
    int nAYFunc = (nBDIR << 2) | (1 << 1) | nBC1;

    if (nAYFunc == 6) { // AY_WRITE
      AY8910_write_instance(&pMB->ay_chip, pMB->nAYCurrentRegister, pMB->sy6522.ORA, static_cast<int>(g_fCurrentCLK6502), SAMPLE_RATE);
    } else if (nAYFunc == 7) { // AY_LATCH
      if (pMB->sy6522.ORA <= 0x0F) {
        pMB->nAYCurrentRegister = pMB->sy6522.ORA & 0x0F;
      }
    }
  }
}

static void SY6522_Write_Instance(MockingboardPeripheral_t* mp, uint8_t nDevice,
                                  uint8_t nReg, uint8_t nValue) {
  mp->bMB_RegAccessedFlag = true;
  mp->bMB_Active = true;
  SY6522_AY8910* pMB = &mp->chips[nDevice];

  switch (nReg) {
    case VIA_REG_ORB:
      nValue &= pMB->sy6522.DDRB;
      pMB->sy6522.ORB = nValue;
      if (mp->type == SC_PHASOR) {
        int nAY_CS = mp->phasor_native ? (~(nValue >> 3) & 3) : 1;
        if (nAY_CS & 1) AY8910_Write_Instance(mp, nDevice, nValue, 0);
        // Phasor has 4 AY chips, but for now we only emulate 2 per card instance
      } else {
        AY8910_Write_Instance(mp, nDevice, nValue, 0);
      }
      break;
    case VIA_REG_ORA:
      pMB->sy6522.ORA = nValue & pMB->sy6522.DDRA;
      break;
    case VIA_REG_DDRB: pMB->sy6522.DDRB = nValue; break;
    case VIA_REG_DDRA: pMB->sy6522.DDRA = nValue; break;
    case VIA_REG_T1L_C:
    case VIA_REG_T1L_L: pMB->sy6522.TIMER1_LATCH.l = nValue; break;
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
    case VIA_REG_T2L_C: pMB->sy6522.TIMER2_LATCH.l = nValue; break;
    case VIA_REG_T2H_C:
      pMB->sy6522.IFR &= ~IxR_TIMER2;
      UpdateIFR(mp, nDevice);
      pMB->sy6522.TIMER2_LATCH.h = nValue;
      pMB->sy6522.TIMER2_COUNTER.w = pMB->sy6522.TIMER2_LATCH.w;
      break;
    case VIA_REG_ACR: pMB->sy6522.ACR = nValue; break;
    case VIA_REG_PCR: pMB->sy6522.PCR = nValue; break;
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
        if (!(pMB->sy6522.IER & IxR_TIMER1) && pMB->nTimerStatus) {
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
  }
}

static auto SY6522_Read_Instance(MockingboardPeripheral_t* mp, uint8_t nDevice,
                                 uint8_t nReg) -> uint8_t {
  mp->bMB_RegAccessedFlag = true;
  mp->bMB_Active = true;
  SY6522_AY8910* pMB = &mp->chips[nDevice];

  switch (nReg) {
    case VIA_REG_ORB: return pMB->sy6522.ORB;
    case VIA_REG_ORA: return pMB->sy6522.ORA;
    case VIA_REG_DDRB: return pMB->sy6522.DDRB;
    case VIA_REG_DDRA: return pMB->sy6522.DDRA;
    case VIA_REG_T1L_C:
      pMB->sy6522.IFR &= ~IxR_TIMER1;
      UpdateIFR(mp, nDevice);
      return pMB->sy6522.TIMER1_COUNTER.l;
    case VIA_REG_T1H_C: return pMB->sy6522.TIMER1_COUNTER.h;
    case VIA_REG_T1L_L: return pMB->sy6522.TIMER1_LATCH.l;
    case VIA_REG_T1H_L: return pMB->sy6522.TIMER1_LATCH.h;
    case VIA_REG_T2L_C:
      pMB->sy6522.IFR &= ~IxR_TIMER2;
      UpdateIFR(mp, nDevice);
      return pMB->sy6522.TIMER2_COUNTER.l;
    case VIA_REG_T2H_C: return pMB->sy6522.TIMER2_COUNTER.h;
    case VIA_REG_ACR: return pMB->sy6522.ACR;
    case VIA_REG_PCR: return pMB->sy6522.PCR;
    case VIA_REG_IFR: return pMB->sy6522.IFR;
    case VIA_REG_IER: return pMB->sy6522.IER | 0x80;
    case VIA_REG_ORA_NO_HANDSHAKE: return pMB->sy6522.ORA;
  }
  return 0;
}

static void MB_Update_Instance(MockingboardPeripheral_t* mp) {
  if (mp->type == SC_NONE) return;

  double n6522TimerPeriod = (mp->bTimerIrqActive || (mp->chips[0].sy6522.IFR & IxR_TIMER1))
             ? static_cast<double>(mp->n6522TimerPeriod)
             : (CLOCK_6502 / 60.0);
  
  double nIrqFreq = g_fCurrentCLK6502 / n6522TimerPeriod;
  int nNumSamples = static_cast<int>(static_cast<double>(SAMPLE_RATE) / nIrqFreq);

  if (nNumSamples > 0) {
    if (nNumSamples > SAMPLE_RATE) nNumSamples = SAMPLE_RATE;

    for (int i = 0; i < CHIPS_PER_CARD; i++) {
      int16_t* voices[3];
      voices[0] = &mp->voice_buffers[i * 3 + 0].get()[0];
      voices[1] = &mp->voice_buffers[i * 3 + 1].get()[0];
      voices[2] = &mp->voice_buffers[i * 3 + 2].get()[0];
      AY8910_update_instance(&mp->chips[i].ay_chip, voices, nNumSamples, static_cast<int>(g_fCurrentCLK6502), SAMPLE_RATE);
    }

    double fAttenuation = (mp->type == SC_PHASOR) ? 2.0 / 3.0 : 1.0;

    // Mix to stereo
    for (int i = 0; i < nNumSamples; i++) {
      int nDataL = 0;
      int nDataR = 0;

      for (int j = 0; j < 3; j++) {
        nDataL += static_cast<int>(static_cast<double>(mp->voice_buffers[0 * 3 + j].get()[i]) * fAttenuation);
        nDataR += static_cast<int>(static_cast<double>(mp->voice_buffers[1 * 3 + j].get()[i]) * fAttenuation);
      }

      if (nDataL < -32768) nDataL = -32768; else if (nDataL > 32767) nDataL = 32767;
      if (nDataR < -32768) nDataR = -32768; else if (nDataR > 32767) nDataR = 32767;

      mp->mix_buffer[i * 2] = static_cast<short>(nDataL);
      mp->mix_buffer[i * 2 + 1] = static_cast<short>(nDataR);
    }

    if (mp->host && mp->host->AudioPushSamples) {
      mp->host->AudioPushSamples(mp, mp->mix_buffer, static_cast<uint32_t>(nNumSamples * 2));
    } else {
      DSUploadMockBuffer(mp->mix_buffer, nNumSamples * 2);
    }
  }
}

static void MB_UpdateCycles_Instance(MockingboardPeripheral_t* mp, uint32_t uExecutedCycles) {
  if (mp->type == SC_NONE) return;

  uint64_t uCycles = g_nCumulativeCycles - mp->uLastCumulativeCycles;
  mp->uLastCumulativeCycles = g_nCumulativeCycles;

  while (uCycles > 0) {
    uint16_t nClocks = (uCycles > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(uCycles);
    uCycles -= nClocks;

    for (int i = 0; i < CHIPS_PER_CARD; i++) {
      SY6522_AY8910* pMB = &mp->chips[i];
      uint16_t OldTimer1 = pMB->sy6522.TIMER1_COUNTER.w;
      pMB->sy6522.TIMER1_COUNTER.w -= nClocks;
      pMB->sy6522.TIMER2_COUNTER.w -= nClocks;

      bool bTimer1Underflow = (!(OldTimer1 & 0x8000) && (pMB->sy6522.TIMER1_COUNTER.w & 0x8000));
      if (bTimer1Underflow && (mp->nMBTimerDevice == i) && mp->bTimerIrqActive) {
        g_uTimer1IrqCount++;
        pMB->sy6522.IFR |= IxR_TIMER1;
        UpdateIFR(mp, i);

        if ((pMB->sy6522.ACR & RUNMODE) == RM_ONESHOT) {
          StopTimer(mp, i);
        } else {
          pMB->sy6522.TIMER1_COUNTER.w = pMB->sy6522.TIMER1_LATCH.w;
          StartTimer(mp, i);
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
    mp->bMB_Active = true;
  }
}

static auto MB_IO_Read(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
                       uint8_t val, uint32_t cycles_left) -> uint8_t {
  (void)pc; (void)write; (void)val;
  if (!instance) return MemReadFloatingBus(cycles_left);
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  MB_UpdateCycles_Instance(mp, cycles_left);

  uint8_t nOffset = addr & 0xff;
  if (nOffset <= (SY6522A_Offset + 0x0F)) {
    return SY6522_Read_Instance(mp, SY6522_DEVICE_A, nOffset & 0x0F);
  } else if ((nOffset >= SY6522B_Offset) && (nOffset <= (SY6522B_Offset + 0x0F))) {
    return SY6522_Read_Instance(mp, SY6522_DEVICE_B, nOffset & 0x0F);
  }
  return MemReadFloatingBus(cycles_left);
}

static auto MB_IO_Write(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
                        uint8_t val, uint32_t cycles_left) -> uint8_t {
  (void)pc; (void)write;
  if (!instance) return 0;
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  MB_UpdateCycles_Instance(mp, cycles_left);

  uint8_t nOffset = addr & 0xff;
  if (nOffset <= (SY6522A_Offset + 0x0F)) {
    SY6522_Write_Instance(mp, SY6522_DEVICE_A, nOffset & 0x0F, val);
  } else if ((nOffset >= SY6522B_Offset) && (nOffset <= (SY6522B_Offset + 0x0F))) {
    SY6522_Write_Instance(mp, SY6522_DEVICE_B, nOffset & 0x0F, val);
  }
  return 0;
}

static auto PhasorIO(void* instance, uint16_t pc, uint16_t addr,
                     uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  (void)pc; (void)write;
  if (!instance) return MemReadFloatingBus(cycles_left);
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);

  if (mp->phasor_native == 0) {
    mp->phasor_native = (addr & 1) != 0;
  }

  uint8_t CS = 0;
  if (!mp->phasor_native) {
    CS = ((addr & 0x80) >> 6) | ((addr & 0x10) >> 4);  // 0, 1, 2 or 3
  } else {
    CS = ((addr & 0x80) >> 7) + 1;  // 1 or 2
  }

  if (CS == 1) {
    SY6522_Write_Instance(mp, SY6522_DEVICE_A, addr & 0x0F, val);
  } else if (CS == 2) {
    SY6522_Write_Instance(mp, SY6522_DEVICE_B, addr & 0x0F, val);
  }
  return 0;
}

static auto MB_ABI_Init(int slot, HostInterface_t* host) -> void* {
  if (active_mb_instances[slot]) return active_mb_instances[slot];

  auto* mp = new MockingboardPeripheral_t{};
  mp->host = host;
  mp->slot = slot;
  active_mb_instances[slot] = mp;

  // Use SC_PHASOR if configured
  char type_str[16];
  if (host->GetConfig("Mockingboard", "Type", type_str, sizeof(type_str))) {
    if (strcmp(type_str, "Phasor") == 0) mp->type = SC_PHASOR;
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
  mp->bMB_Active = true;
  mp->nMB_InActiveCycleCount = 0;
  mp->phasor_native = false;

  for (int i = 0; i < CHIPS_PER_CARD; i++) {
    memset(&mp->chips[i].sy6522, 0, sizeof(SY6522));
    AY8910_reset_instance(&mp->chips[i].ay_chip);
    mp->chips[i].nTimerStatus = 0;
    mp->chips[i].nAYCurrentRegister = 0;
  }
}

static void MB_ABI_Shutdown(void* instance) {
  if (!instance) return;
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  active_mb_instances[mp->slot] = nullptr;
  delete mp;
}

static void MB_ABI_Think(void* instance, uint32_t cycles) {
  if (!instance) return;
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  MB_UpdateCycles_Instance(mp, cycles);
  
  if (!mp->bTimerIrqActive && !(mp->chips[0].sy6522.IFR & IxR_TIMER1)) {
    static uint64_t last_60hz = 0;
    if (g_nCumulativeCycles - last_60hz > static_cast<uint64_t>(g_fCurrentCLK6502) / 60) {
      last_60hz = g_nCumulativeCycles;
      MB_Update_Instance(mp);
    }
  }
}

Peripheral_t g_mockingboard_peripheral = {
    LINAPPLE_ABI_VERSION,
    "Mockingboard",
    0xFE,  // Slots 1-7
    MB_ABI_Init,
    MB_ABI_Reset,
    MB_ABI_Shutdown,
    MB_ABI_Think,
    nullptr, nullptr, nullptr, nullptr, nullptr
};

extern "C" void Register_Mockingboard() {
  Peripheral_Register_Builtin(&g_mockingboard_peripheral);
}

#ifdef BUILD_SHARED_PERIPHERAL
EXPORT_PERIPHERAL(g_mockingboard_peripheral)
#endif

// --- Legacy Stubs for Build Compatibility ---
void MB_Initialize() {}
void MB_Reinitialize() {}
void MB_Destroy() {}
void MB_Reset() {}
void MB_Update() {}
void MB_UpdateCycles(uint32_t) {}
void MB_EndOfVideoFrame() {}
void MB_StartOfCpuExecute() {}
bool MB_IsActive() { return true; }
eSOUNDCARDTYPE MB_GetSoundcardType() { return SC_MOCKINGBOARD; }
void MB_SetSoundcardType(eSOUNDCARDTYPE) {}
double MB_GetFramePeriod() { return 1.0; }
uint32_t MB_GetVolume() { return 0; }
void MB_SetVolume(uint32_t, uint32_t) {}
uint32_t MB_GetSnapshot(SS_CARD_MOCKINGBOARD*, uint32_t) { return 0; }
uint32_t MB_SetSnapshot(SS_CARD_MOCKINGBOARD*, uint32_t) { return 0; }

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables)
