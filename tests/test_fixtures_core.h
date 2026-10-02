// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "core/LinAppleCore.h"
#include "test_fixtures.h"

namespace TestFixtures {

/** Retrieve snapshot fixture path. */
class ScopedLocalTimeProvider_t {
 public:
  explicit ScopedLocalTimeProvider_t(const HostLocalTime_t& frozen)
      : frozen_(frozen) {
    linapple_set_local_time_provider(answer, this);
  }

  ~ScopedLocalTimeProvider_t() {
    linapple_set_local_time_provider(nullptr, nullptr);
  }

  ScopedLocalTimeProvider_t(const ScopedLocalTimeProvider_t&) = delete;
  auto operator=(const ScopedLocalTimeProvider_t&)
      -> ScopedLocalTimeProvider_t& = delete;
  ScopedLocalTimeProvider_t(ScopedLocalTimeProvider_t&&) = delete;
  auto operator=(ScopedLocalTimeProvider_t&&)
      -> ScopedLocalTimeProvider_t& = delete;

  auto set(const HostLocalTime_t& frozen) -> void { frozen_ = frozen; }
  auto value() const -> const HostLocalTime_t& { return frozen_; }
  auto calls() const -> unsigned { return calls_; }

 private:
  static auto answer(void* ctx, HostLocalTime_t* out) -> bool {
    auto* self = static_cast<ScopedLocalTimeProvider_t*>(ctx);
    if (self == nullptr || out == nullptr) {
      return false;
    }
    ++self->calls_;
    *out = self->frozen_;
    return true;
  }

  HostLocalTime_t frozen_;
  unsigned calls_ = 0;
};

/**
 * @brief RAII capturing byte sink.
 *
 * Installs itself behind the host interface's sink members and puts the
 * previous sink back, context included, on destruction. It records every byte
 * with the slot it came from, counts the writes it refused while not ready,
 * the readiness polls, opens, closes and ticks, and starts ready so a case
 * that never touches readiness sees every byte. For a line that also
 * receives it holds one receive queue per slot, so a byte queued for one card
 * is never read through another's token, counts the reads each slot made,
 * keeps the last line format a card sent and how many were sent, and answers
 * the modem inputs with a mask that starts with every line asserted, which is
 * what a card with no cable reads. The sink is a process global, so the guard
 * is neither copyable nor movable.
 */
class ScopedByteSink_t {
 public:
  struct Byte_t {
    int slot;
    uint8_t byte;
  };

  static constexpr uint8_t all_lines_asserted = 0x07;
  static constexpr size_t slot_count = 8;

  ScopedByteSink_t() : previous_(linapple_set_byte_sink(&vtable_, this)) {}

  ~ScopedByteSink_t() {
    linapple_set_byte_sink(previous_.vtable, previous_.ctx);
  }

  ScopedByteSink_t(const ScopedByteSink_t&) = delete;
  auto operator=(const ScopedByteSink_t&) -> ScopedByteSink_t& = delete;
  ScopedByteSink_t(ScopedByteSink_t&&) = delete;
  auto operator=(ScopedByteSink_t&&) -> ScopedByteSink_t& = delete;

  auto bytes() const -> const std::vector<Byte_t>& { return bytes_; }
  auto dropped() const -> unsigned { return dropped_; }
  auto ready_polls() const -> unsigned { return ready_polls_; }
  auto opens() const -> unsigned { return opens_; }
  auto closes() const -> unsigned { return closes_; }
  auto ticks() const -> unsigned { return ticks_; }
  auto last_open_kind() const -> PeripheralSinkKind_t { return last_kind_; }
  auto ready() const -> bool { return ready_; }
  auto set_ready(bool ready) -> void { ready_ = ready; }

  auto push_rx(int slot, uint8_t byte) -> void {
    if (slot >= 0 && static_cast<size_t>(slot) < slot_count) {
      rx_[static_cast<size_t>(slot)].push_back(byte);
    }
  }
  auto reads(int slot) const -> unsigned {
    if (slot < 0 || static_cast<size_t>(slot) >= slot_count) {
      return 0;
    }
    return reads_[static_cast<size_t>(slot)];
  }
  auto set_lines(uint8_t mask) -> void { lines_ = mask; }
  auto lines() const -> uint8_t { return lines_; }
  auto last_line() const -> const PeripheralSerialLine_t& { return last_line_; }
  auto line_sets() const -> unsigned { return line_sets_; }

 private:
  static auto self(void* ctx) -> ScopedByteSink_t* {
    return static_cast<ScopedByteSink_t*>(ctx);
  }

  static auto open(void* ctx, int slot, PeripheralSinkKind_t kind) -> void {
    (void)slot;
    if (self(ctx) != nullptr) {
      ++self(ctx)->opens_;
      self(ctx)->last_kind_ = kind;
    }
  }

  static auto write(void* ctx, int slot, uint8_t byte) -> void {
    if (self(ctx) == nullptr) {
      return;
    }
    if (!self(ctx)->ready_) {
      ++self(ctx)->dropped_;
      return;
    }
    self(ctx)->bytes_.push_back({slot, byte});
  }

  static auto ready(void* ctx, int slot) -> bool {
    (void)slot;
    if (self(ctx) == nullptr) {
      return false;
    }
    ++self(ctx)->ready_polls_;
    return self(ctx)->ready_;
  }

  static auto close(void* ctx, int slot) -> void {
    (void)slot;
    if (self(ctx) != nullptr) {
      ++self(ctx)->closes_;
    }
  }

  static auto tick(void* ctx) -> void {
    if (self(ctx) != nullptr) {
      ++self(ctx)->ticks_;
    }
  }

  static auto read(void* ctx, int slot, uint8_t* byte) -> bool {
    if (self(ctx) == nullptr || byte == nullptr || slot < 0 ||
        static_cast<size_t>(slot) >= slot_count) {
      return false;
    }
    ++self(ctx)->reads_[static_cast<size_t>(slot)];
    std::vector<uint8_t>& queue = self(ctx)->rx_[static_cast<size_t>(slot)];
    if (queue.empty()) {
      return false;
    }
    *byte = queue.front();
    queue.erase(queue.begin());
    return true;
  }

  static auto set_line(void* ctx, int slot, const PeripheralSerialLine_t* line)
      -> void {
    (void)slot;
    if (self(ctx) == nullptr || line == nullptr) {
      return;
    }
    self(ctx)->last_line_ = *line;
    ++self(ctx)->line_sets_;
  }

  static auto get_lines(void* ctx, int slot, uint8_t* lines) -> bool {
    (void)slot;
    if (self(ctx) == nullptr || lines == nullptr) {
      return false;
    }
    *lines = self(ctx)->lines_;
    return true;
  }

  const ByteSink_t vtable_ = {open, write, ready,    close,
                              tick, read,  set_line, get_lines};
  ByteSinkBinding_t previous_;
  std::vector<Byte_t> bytes_;
  std::array<std::vector<uint8_t>, slot_count> rx_{};
  std::array<unsigned, slot_count> reads_{};
  unsigned dropped_ = 0;
  unsigned ready_polls_ = 0;
  unsigned opens_ = 0;
  unsigned closes_ = 0;
  unsigned ticks_ = 0;
  unsigned line_sets_ = 0;
  PeripheralSinkKind_t last_kind_ = peripheral_sink_printer;
  PeripheralSerialLine_t last_line_{};
  uint8_t lines_ = all_lines_asserted;
  bool ready_ = true;
};

/**
 * @brief RAII swap of the active CPU context.
 *
 * The context is a process global, so a case that leaves a half-run one
 * behind changes what the next case measures.
 */
struct ScopedCpuContext_t {
  CpuInstance_t* previous = nullptr;
  CpuInstance_t fresh{};

  ScopedCpuContext_t() : previous(cpu_get_active_context()) {
    cpu_set_active_context(&fresh);
  }

  ~ScopedCpuContext_t() {
    if (previous != nullptr) {
      cpu_set_active_context(previous);
    }
  }

  ScopedCpuContext_t(const ScopedCpuContext_t&) = delete;
  auto operator=(const ScopedCpuContext_t&) -> ScopedCpuContext_t& = delete;
  ScopedCpuContext_t(ScopedCpuContext_t&&) = delete;
  auto operator=(ScopedCpuContext_t&&) -> ScopedCpuContext_t& = delete;
};

/**
 * @brief RAII emulator core built from a declared machine.
 *
 * The CPU context is a member so that it is installed before linapple_init
 * runs and restored after linapple_shutdown, and every frontend callback the
 * core holds is cleared on the way out: they are process globals, and a
 * dangling one would fire during the next case's shutdown.
 */
class ScopedCore_t {
 public:
  explicit ScopedCore_t(const ScopedTestConfig_t& config) {
    config.load();
    linapple_init();
  }

  ~ScopedCore_t() {
    linapple_set_audio_channel_callback(nullptr);
    linapple_set_audio_source_register_callback(nullptr);
    linapple_set_audio_source_unregister_callback(nullptr);
    linapple_shutdown();
  }

  ScopedCore_t(const ScopedCore_t&) = delete;
  auto operator=(const ScopedCore_t&) -> ScopedCore_t& = delete;
  ScopedCore_t(ScopedCore_t&&) = delete;
  auto operator=(ScopedCore_t&&) -> ScopedCore_t& = delete;

  /**
   * @brief Load bytes where the 6502 will fetch them.
   *
   * The 6502 fetches every opcode and every vector out of the memory image,
   * so that is where the byte has to end up whatever is banked in -- which is
   * the only way a test can point the interrupt vector at its own handler
   * without a language card. Where a write page also exists it gets the byte
   * too, so a later bank switch restores what was written rather than
   * whatever the page held before.
   */
  static auto poke(uint16_t addr, const uint8_t* bytes, size_t count) -> void {
    for (size_t i = 0; i < count; ++i) {
      const auto target = static_cast<uint16_t>(addr + i);
      memdirty[target >> 8] = 0xFF;
      mem[target] = bytes[i];
      uint8_t* page = memwrite[target >> 8];
      if (page != nullptr) {
        page[target & 0xFF] = bytes[i];
      }
    }
  }

  template <size_t N>
  static auto poke(uint16_t addr, const std::array<uint8_t, N>& bytes) -> void {
    poke(addr, bytes.data(), N);
  }

 private:
  ScopedCpuContext_t cpu_;
};

/**
 * @brief Where a stepped run begins: the registers a caller hands a routine.
 *
 * The stack sits at the top of page 1 and interrupts are masked, as the
 * Monitor leaves them for a routine entered from a JSR.
 */
struct EntryRegisters_t {
  uint16_t pc;
  uint8_t a;
  uint8_t x;
  uint8_t y;
};

inline auto enter_at(const EntryRegisters_t& entry) -> void {
  constexpr uint16_t stack_top = 0x01FF;
  constexpr uint8_t status_interrupts_masked = 0x24;
  CpuRegisters_t* regs = cpu_get_registers();
  regs->pc = entry.pc;
  regs->sp = stack_top;
  regs->a = entry.a;
  regs->x = entry.x;
  regs->y = entry.y;
  regs->ps = status_interrupts_masked;
}

/**
 * @brief Steps the 6502 one instruction at a time until the program counter
 * lands on the sentinel or the cap is spent; returns the cycles spent.
 *
 * One cpu_execute per instruction keeps the bridge's per-call count at zero,
 * so an I/O handler sees the cumulative cycle count as it stood at the first
 * cycle of the instruction that reached it, whatever the addressing mode; the
 * difference between two such reads is then the hardware difference. The
 * caller checks the PC afterwards: the cap is what ends a runaway.
 */
inline auto step_until_pc(uint16_t sentinel, uint32_t cap) -> uint32_t {
  const CpuRegisters_t* regs = cpu_get_registers();
  uint32_t cycles = 0;
  while (regs->pc != sentinel && cycles < cap) {
    cycles += cpu_execute(0);
  }
  return cycles;
}

}  // namespace TestFixtures
