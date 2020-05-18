#pragma once

typedef struct _regsrec {
  unsigned char a;   // accumulator
  unsigned char x;   // index X
  unsigned char y;   // index Y
  unsigned char ps;  // processor status
  unsigned short pc;  // program counter
  unsigned short sp;  // stack pointer
  unsigned char bJammed; // CPU has crashed (NMOS 6502 only)
} regsrec, *regsptr;

extern regsrec regs;
extern unsigned __int64
g_nCumulativeCycles;

void CpuDestroy();

void CpuCalcCycles(ULONG nExecutedCycles);

unsigned int CpuExecute(unsigned int);

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

unsigned int CpuGetSnapshot(SS_CPU6502 *pSS);

unsigned int CpuSetSnapshot(SS_CPU6502 *pSS);
