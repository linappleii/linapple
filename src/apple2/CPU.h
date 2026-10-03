// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstdint>

#include "apple2/peripherals/Peripheral_Types.h"

struct SsCpu6502_t;

// 6502 Architecture Vectors
constexpr uint16_t NMI_VECTOR_ADDR = 0xFFFA;
constexpr uint16_t RESET_VECTOR_ADDR = 0xFFFC;
constexpr uint16_t IRQ_VECTOR_ADDR = 0xFFFE;

// Default execution trap vectors for test ROMs
constexpr uint16_t TRAP_NMOS_DEFAULT = 0x336D;
constexpr uint16_t TRAP_CMOS_DEFAULT = 0x2434;

struct CpuRegisters_t {
  uint8_t a = 0;
  uint8_t x = 0;
  uint8_t y = 0;
  uint8_t ps = 0;
  uint16_t pc = 0;
  uint16_t sp = 0;  // 16-bit to store pre-computed page 1 address (0x0100 | S)
  bool is_jammed = false;  // CPU has crashed on illegal instruction (NMOS 6502)
};

// The slice bound and flag are not here: no context switch happens inside
// cpu_execute_slice.
struct CpuInstance_t {
  CpuRegisters_t cpu_regs{};
  uint64_t cumulative_cycles = 0;
  uint32_t cycles_submitted = 0;
  uint32_t cycles_executed = 0;
  uint32_t bm_irq = 0;
  uint32_t bm_nmi = 0;
  bool nmi_flank = false;
};

auto cpu_get_registers() noexcept -> CpuRegisters_t*;
auto cpu_get_cumulative_cycles() noexcept -> uint64_t;
auto cpu_add_cumulative_cycles(uint32_t cycles) noexcept -> void;
extern uint64_t g_cumulative_cycles;

auto cpu_get_active_context() noexcept -> CpuInstance_t*;
auto cpu_set_active_context(CpuInstance_t* context) noexcept -> void;

auto cpu_destroy() noexcept -> void;
auto cpu_calc_cycles(uint32_t executed_cycles) noexcept -> void;
// One call is one batch: every I/O handler sees executed_cycles as its offset
// into it.
auto cpu_execute(uint32_t total_cycles) -> uint32_t;
// A frame run as slices is still one batch: cpu_begin_frame starts the
// frame-relative count and each slice continues it to the first instruction
// boundary at or after the lesser of frame_cycles and until_cycle (absolute;
// UINT64_MAX for no bound; at or behind now runs one instruction), returning
// the frame-relative total so far.
auto cpu_begin_frame(uint32_t frame_cycles) noexcept -> void;
auto cpu_execute_slice(uint32_t frame_cycles, uint64_t until_cycle) -> uint32_t;
// Lowers the running slice's bound, never raises it. A no-op outside
// cpu_execute_slice: a frame-relative bound computed there would be nonsense,
// and no direct cpu_execute may return early.
auto cpu_limit_cycles(uint64_t at_cumulative) noexcept -> void;
auto cpu_get_cycles_this_frame(uint32_t executed_cycles) noexcept -> uint32_t;
auto cpu_initialize() noexcept -> void;
auto cpu_step() -> void;
auto cpu_setup_benchmark() -> void;
auto cpu_irq_reset() noexcept -> void;
auto cpu_irq_assert(IrqSrc_t device) noexcept -> void;
auto cpu_irq_deassert(IrqSrc_t device) noexcept -> void;
auto cpu_nmi_reset() noexcept -> void;
auto cpu_nmi_assert(IrqSrc_t device) noexcept -> void;
auto cpu_nmi_deassert(IrqSrc_t device) noexcept -> void;
auto cpu_reset() noexcept -> void;
auto cpu_get_snapshot(SsCpu6502_t* snapshot) noexcept -> uint32_t;
auto cpu_set_snapshot(const SsCpu6502_t* snapshot) noexcept -> uint32_t;
