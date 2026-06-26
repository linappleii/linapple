#pragma once

#include <cstdint>

#include "core/Common.h"

using SS_CPU6502 = struct tagSS_CPU6502;

const uint16_t NMI_VECTOR_ADDR = 0xFFFA;
const uint16_t RESET_VECTOR_ADDR = 0xFFFC;
const uint16_t IRQ_VECTOR_ADDR = 0xFFFE;

const uint16_t TRAP_NMOS_DEFAULT = 0x336D;
const uint16_t TRAP_CMOS_DEFAULT = 0x3469;

const uint32_t UINT32_MAX_VAL = 0xFFFFFFFF;

struct CpuRegisters_t {
  uint8_t a = 0;
  uint8_t x = 0;
  uint8_t y = 0;
  uint8_t ps = 0;
  uint16_t pc = 0;
  uint16_t sp = 0;
  uint8_t is_jammed = 0;  // CPU has crashed (NMOS 6502 only)
};

using regsrec = CpuRegisters_t;
using regsptr = CpuRegisters_t*;

struct CpuInstance_t {
  CpuRegisters_t cpu_regs{};
  uint64_t cumulative_cycles = 0;
  uint32_t cycles_submitted = 0;
  uint32_t cycles_executed = 0;
  volatile uint32_t bm_irq = 0;
  volatile uint32_t bm_nmi = 0;
  volatile bool nmi_flank = false;
};

auto CpuGetRegisters() -> CpuRegisters_t*;
auto CpuGetCumulativeCycles() -> uint64_t;
extern uint64_t g_nCumulativeCycles;

auto CpuGetActiveContext() -> CpuInstance_t*;
auto CpuSetActiveContext(CpuInstance_t* context) -> void;

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

auto CpuGetSnapshot(SS_CPU6502* pSS) -> uint32_t;
auto CpuSetSnapshot(SS_CPU6502* pSS) -> uint32_t;
