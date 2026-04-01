#pragma once

#include <cstdint>

typedef struct _regsrec {
  uint8_t a;   // accumulator
  uint8_t x;   // index X
  uint8_t y;   // index Y
  uint8_t ps;  // processor status
  uint16_t pc;  // program counter
  uint16_t sp;  // stack pointer
  uint8_t bJammed; // CPU has crashed (NMOS 6502 only)
} regsrec, *regsptr;

extern regsrec regs;
extern uint64_t g_nCumulativeCycles;

void CpuDestroy();

void CpuCalcCycles(uint32_t nExecutedCycles);

uint32_t CpuExecute(uint32_t);

uint32_t CpuGetCyclesThisFrame(uint32_t nExecutedCycles);

void CpuInitialize();

void CpuSetupBenchmark();

void CpuIrqReset();

void CpuIrqAssert(eIRQSRC Device);

void CpuIrqDeassert(eIRQSRC Device);

void CpuNmiReset();

void CpuNmiAssert(eIRQSRC Device);

void CpuNmiDeassert(eIRQSRC Device);

void CpuReset();

uint32_t CpuGetSnapshot(SS_CPU6502 *pSS);
uint32_t CpuSetSnapshot(SS_CPU6502 *pSS);
