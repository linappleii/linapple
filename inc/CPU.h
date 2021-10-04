#pragma once

typedef struct _regsrec {
  UINT8 a;   // accumulator
  UINT8 x;   // index X
  UINT8 y;   // index Y
  UINT8 ps;  // processor status
  UINT16 pc;  // program counter
  UINT16 sp;  // stack pointer
  UINT8 bJammed; // CPU has crashed (NMOS 6502 only)
} regsrec, *regsptr;

extern regsrec regs;
extern UINT64 g_nCumulativeCycles;

void CpuDestroy();

void CpuCalcCycles(ULONG nExecutedCycles);

DWORD CpuExecute(DWORD);

ULONG CpuGetCyclesThisFrame(ULONG nExecutedCycles);

void CpuInitialize();

void CpuSetupBenchmark();

void CpuIrqReset();

void CpuIrqAssert(eIRQSRC Device);

void CpuIrqDeassert(eIRQSRC Device);

void CpuNmiReset();

void CpuNmiAssert(eIRQSRC Device);

void CpuNmiDeassert(eIRQSRC Device);

void CpuReset();

DWORD CpuGetSnapshot(SS_CPU6502 *pSS);
DWORD CpuSetSnapshot(SS_CPU6502 *pSS);
