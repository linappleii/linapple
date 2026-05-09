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
  uint16_t nAYCurrentRegister;
  uint8_t nAY8910Number;
  int nTimerStatus;
} SY6522_AY8910;

struct MockingboardPeripheral_t {
  std::array<SY6522_AY8910, CHIPS_PER_CARD> chips;
  std::array<std::unique_ptr<short[]>, VOICES_PER_CARD> voice_buffers;
  uint32_t n6522TimerPeriod = 0;
  uint16_t nMBTimerDevice = 0;
  uint64_t uLastCumulativeCycles = 0;
  uint64_t nMB_InActiveCycleCount = 0;
  bool bMB_RegAccessedFlag = false;
  bool bMB_Active = true;
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
    }
  }
};

static MockingboardPeripheral_t* active_mb_instances[8] = {nullptr};

// Legacy global vars (kept for now, will be removed)
bool g_bMBTimerIrqActive = false;
uint32_t g_uTimer1IrqCount = 0;

static void StartTimer(MockingboardPeripheral_t* mp, int chip_idx) {
  SY6522_AY8910* pMB = &mp->chips[chip_idx];
  if ((pMB->nAY8910Number & 1) != SY6522_DEVICE_A) {
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
  g_bMBTimerIrqActive = true;
  mp->nMBTimerDevice = pMB->nAY8910Number;
}

static void StopTimer(MockingboardPeripheral_t* mp, int chip_idx) {
  mp->chips[chip_idx].nTimerStatus = 0;
  g_bMBTimerIrqActive = false;
  mp->nMBTimerDevice = 0;
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

  if (bIRQ) {
    CpuIrqAssert(IS_6522);
  } else {
    CpuIrqDeassert(IS_6522);
  }
}

static void AY8910_Write_Instance(MockingboardPeripheral_t* mp, uint8_t nDevice,
                                  uint8_t nValue, uint8_t nAYDevice) {
  SY6522_AY8910* pMB = &mp->chips[nDevice];
  int ay_chip_global_idx = (mp->slot == 4 ? 0 : 2) + nDevice; // Legacy remapping

  if ((nValue & 4) == 0) {
    AY8910_reset(ay_chip_global_idx);
  } else {
    int nBDIR = (nValue & 2) ? 1 : 0;
    int nBC1 = nValue & 1;
    int nAYFunc = (nBDIR << 2) | (1 << 1) | nBC1;

    if (nAYFunc == 6) { // AY_WRITE
      _AYWriteReg(ay_chip_global_idx, pMB->nAYCurrentRegister, pMB->sy6522.ORA);
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
      AY8910_Write_Instance(mp, nDevice, nValue, 0);
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

static auto MB_IO_Read(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
                       uint8_t val, uint32_t cycles_left) -> uint8_t {
  (void)pc; (void)write; (void)val;
  if (!instance) return MemReadFloatingBus(cycles_left);
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);

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

  uint8_t nOffset = addr & 0xff;
  if (nOffset <= (SY6522A_Offset + 0x0F)) {
    SY6522_Write_Instance(mp, SY6522_DEVICE_A, nOffset & 0x0F, val);
  } else if ((nOffset >= SY6522B_Offset) && (nOffset <= (SY6522B_Offset + 0x0F))) {
    SY6522_Write_Instance(mp, SY6522_DEVICE_B, nOffset & 0x0F, val);
  }
  return 0;
}

static auto MB_ABI_Init(int slot, HostInterface_t* host) -> void* {
  if (active_mb_instances[slot]) return active_mb_instances[slot];

  auto* mp = new MockingboardPeripheral_t{};
  mp->host = host;
  mp->slot = slot;
  active_mb_instances[slot] = mp;

  host->RegisterIO(slot, nullptr, nullptr, MB_IO_Read, MB_IO_Write);
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

  for (int i = 0; i < CHIPS_PER_CARD; i++) {
    memset(&mp->chips[i].sy6522, 0, sizeof(SY6522));
    mp->chips[i].nTimerStatus = 0;
    mp->chips[i].nAYCurrentRegister = 0;
    AY8910_reset((mp->slot == 4 ? 0 : 2) + i);
  }
}

static void MB_ABI_Shutdown(void* instance) {
  if (!instance) return;
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  active_mb_instances[mp->slot] = nullptr;
  delete mp;
}

static void MB_ABI_Think(void* instance, uint32_t cycles) {
  (void)instance; (void)cycles;
  // Cycle-accurate timer updates will be implemented in #313
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
