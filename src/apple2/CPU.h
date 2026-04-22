#pragma once

#include <cstdint>
#include "core/Common.h"

using SS_CPU6502 = struct tagSS_CPU6502;

const uint16_t NMI_VECTOR_ADDR   = 0xFFFA;
const uint16_t RESET_VECTOR_ADDR = 0xFFFC;
const uint16_t IRQ_VECTOR_ADDR   = 0xFFFE;

const uint16_t TRAP_NMOS_DEFAULT = 0x336D;
const uint16_t TRAP_CMOS_DEFAULT = 0x3469;

const uint32_t UINT32_MAX_VAL    = 0xFFFFFFFF;

typedef struct _regsrec {
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t ps;
  uint16_t pc;
  uint16_t sp;
  uint8_t bJammed; // CPU has crashed (NMOS 6502 only)
} regsrec, *regsptr;

extern regsrec regs;
extern uint64_t g_nCumulativeCycles;

void CpuDestroy();

void CpuCalcCycles(uint32_t nExecutedCycles);

auto CpuExecute(uint32_t) -> uint32_t;

auto CpuGetCyclesThisFrame(uint32_t nExecutedCycles) -> uint32_t;

void CpuInitialize();

void CpuSetupBenchmark();

void CpuIrqReset();

void CpuIrqAssert(eIRQSRC Device);

void CpuIrqDeassert(eIRQSRC Device);

void CpuNmiReset();

void CpuNmiAssert(eIRQSRC Device);

void CpuNmiDeassert(eIRQSRC Device);

void CpuReset();

auto CpuGetSnapshot(SS_CPU6502 *pSS) -> uint32_t;
auto CpuSetSnapshot(SS_CPU6502 *pSS) -> uint32_t;
