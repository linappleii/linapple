// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstdint>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

struct SsCpu6502_t;
using SsCpu6502_t = SsCpu6502_t;

constexpr uint16_t nmi_vector_addr = 0xFFFA;
constexpr uint16_t reset_vector_addr = 0xFFFC;
constexpr uint16_t irq_vector_addr = 0xFFFE;

constexpr uint16_t trap_nmos_default = 0x336D;
constexpr uint16_t trap_cmos_default = 0x3469;

constexpr uint32_t uint32_max_val = 0xFFFFFFFF;

// Legacy constant aliases for backwards compatibility
constexpr uint16_t NMI_VECTOR_ADDR = nmi_vector_addr;
constexpr uint16_t RESET_VECTOR_ADDR = reset_vector_addr;
constexpr uint16_t IRQ_VECTOR_ADDR = irq_vector_addr;
constexpr uint16_t TRAP_NMOS_DEFAULT = trap_nmos_default;
constexpr uint16_t TRAP_CMOS_DEFAULT = trap_cmos_default;
constexpr uint32_t UINT32_MAX_VAL = uint32_max_val;

struct CpuRegisters_t {
  uint8_t a = 0;
  uint8_t x = 0;
  uint8_t y = 0;
  uint8_t ps = 0;
  uint16_t pc = 0;
  uint16_t sp = 0;
  uint8_t is_jammed = 0;  // CPU has crashed (NMOS 6502 only)
};

using RegsRec_t = CpuRegisters_t;
using RegsPtr_t = CpuRegisters_t*;

struct CpuInstance_t {
  CpuRegisters_t cpu_regs{};
  uint64_t cumulative_cycles = 0;
  uint32_t cycles_submitted = 0;
  uint32_t cycles_executed = 0;
  volatile uint32_t bm_irq = 0;
  volatile uint32_t bm_nmi = 0;
  volatile bool nmi_flank = false;
};

// Modern snake_case API
auto cpu_get_registers() -> CpuRegisters_t*;
auto cpu_get_cumulative_cycles() -> uint64_t;
extern uint64_t g_cumulative_cycles;

auto cpu_get_active_context() -> CpuInstance_t*;
auto cpu_set_active_context(CpuInstance_t* context) -> void;

auto cpu_destroy() -> void;
auto cpu_calc_cycles(uint32_t executed_cycles) -> void;
auto cpu_execute(uint32_t total_cycles) -> uint32_t;
auto cpu_get_cycles_this_frame(uint32_t executed_cycles) -> uint32_t;
auto cpu_initialize() -> void;
auto cpu_step() -> void;
auto cpu_setup_benchmark() -> void;
auto cpu_irq_reset() -> void;
auto cpu_irq_assert(IrqSrc_t device) -> void;
auto cpu_irq_deassert(IrqSrc_t device) -> void;
auto cpu_nmi_reset() -> void;
auto cpu_nmi_assert(IrqSrc_t device) -> void;
auto cpu_nmi_deassert(IrqSrc_t device) -> void;
auto cpu_reset() -> void;
auto cpu_get_snapshot(SsCpu6502_t* snapshot) -> uint32_t;
auto cpu_set_snapshot(SsCpu6502_t* snapshot) -> uint32_t;

