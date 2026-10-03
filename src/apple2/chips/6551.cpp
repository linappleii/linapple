// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/chips/6551.h"

#include <array>
#include <cstdint>

namespace {

// The 1.8432 MHz crystal divided by 16 and by this (SY6551 Fig. 6, p. 3-175);
// code 0 is the 16x external clock, which the SSC does not supply (1981
// manual p. 54), so with it nothing moves.
constexpr std::array<uint32_t, 16> divisors = {
    {0, 2304, 1536, 1048, 856, 768, 384, 192, 96, 64, 48, 32, 24, 16, 12, 6}};
constexpr std::array<uint32_t, 16> bauds = {{0, 50, 75, 110, 135, 150, 300, 600,
                                             1200, 1800, 2400, 3600, 4800, 7200,
                                             9600, 19200}};
constexpr uint64_t crystal_millihertz = 1843200000;
constexpr uint32_t sixteenths_per_bit = 16;
constexpr uint32_t sixteenths_per_half_bit = 8;
// RDRF is set "at about the 9/16 point through the Stop Bit" (W65C51S p. 16);
// with 1.5 stop bits "halfway through the trailing half-Stop Bit" (p. 21),
// the same 9 sixteenths.
constexpr uint32_t rdrf_sixteenths_into_stop = 9;
constexpr uint8_t parity_none = 0;
constexpr uint8_t parity_odd = 1;
constexpr uint8_t word_length_8 = 8;
constexpr uint8_t word_length_5 = 5;

auto divisor_of(const Acia6551_t* a) noexcept -> uint32_t {
  if ((a->control & acia_control::receiver_clock_internal) == 0) {
    return 0;
  }
  return divisors.at(a->control & acia_control::baud_mask);
}

auto has_clock(const Acia6551_t* a) noexcept -> bool {
  return a->clock_mhz != 0 && divisor_of(a) != 0;
}

auto data_bits(const Acia6551_t* a) noexcept -> uint8_t {
  return static_cast<uint8_t>(word_length_8 -
                              ((a->control & acia_control::word_length_mask) >>
                               acia_control::word_length_shift));
}

auto parity_of(const Acia6551_t* a) noexcept -> uint8_t {
  if ((a->command & acia_command::parity_enable) == 0) {
    return parity_none;
  }
  return static_cast<uint8_t>(parity_odd +
                              ((a->command & acia_command::parity_mode_mask) >>
                               acia_command::parity_mode_shift));
}

// Bit 7 set means two stop bits, except one with 8 data bits and parity and
// 1.5 with 5 data bits and no parity (SY6551 Fig. 6; IIc Tech Ref p. 262).
auto stop_half_bits(const Acia6551_t* a) noexcept -> uint8_t {
  if ((a->control & acia_control::two_stop_bits) == 0) {
    return 2;
  }
  const bool parity = (a->command & acia_command::parity_enable) != 0;
  if (data_bits(a) == word_length_8 && parity) {
    return 2;
  }
  if (data_bits(a) == word_length_5 && !parity) {
    return 3;
  }
  return 4;
}

auto tic(const Acia6551_t* a) noexcept -> uint8_t {
  return static_cast<uint8_t>(a->command & acia_command::tic_mask);
}

auto dtr(const Acia6551_t* a) noexcept -> bool {
  return (a->command & acia_command::dtr) != 0;
}

auto latch(const Acia6551_t* a, uint8_t bit) noexcept -> bool {
  return (a->status_latches & bit) != 0;
}

auto set_latch(Acia6551_t* a, uint8_t bit, bool on) noexcept -> void {
  if (on) {
    a->status_latches |= bit;
  } else {
    a->status_latches &= static_cast<uint8_t>(~bit);
  }
}

// CTS deasserted disables the transmitter (SY6551 p. 3-174), as does TIC 00
// (Fig. 7); a break holds the line.
auto transmitter_sends(const Acia6551_t* a) noexcept -> bool {
  return has_clock(a) && tic(a) != acia_command::tic_off &&
         tic(a) != acia_command::tic_break && (a->lines & acia_line::cts) != 0;
}

auto frame_cycles(const Acia6551_t* a) noexcept -> uint64_t {
  return acia_cycles_for(a, acia_frame_sixteenths(a));
}

auto raise_irq(Acia6551_t* a, bool from_lines) noexcept -> void {
  if (!a->irq) {
    a->irq_from_lines = from_lines;
  } else if (!from_lines) {
    a->irq_from_lines = false;
  }
  a->irq = true;
}

auto emit(Acia6551_t* a, uint8_t byte) noexcept -> void {
  a->pending_out = byte;
  a->has_pending_out = true;
}

// "A Start Bit immediately occurs" (W65C51S p. 17); with TIC 01 the interrupt
// is raised "at the beginning of the Start Bit" (p. 16).
auto start_transmit(Acia6551_t* a, uint64_t at) noexcept -> void {
  emit(a, a->transmit_data);
  set_latch(a, acia_latch::tdr_full, false);
  set_latch(a, acia_latch::tx_busy, true);
  a->tx_anchor = at;
  a->tx_busy_until = at + frame_cycles(a);
  if (tic(a) == acia_command::tic_irq && dtr(a)) {
    raise_irq(a, false);
  }
}

auto try_start_transmit(Acia6551_t* a, uint64_t at) noexcept -> void {
  if (latch(a, acia_latch::tdr_full) && !latch(a, acia_latch::tx_busy) &&
      transmitter_sends(a)) {
    start_transmit(a, at);
  }
}

// With TIC 01 and nothing loaded "IRQB interrupts continue to occur at the
// same rate as previously, yet no data is transmitted" (W65C51S p. 17); a
// break begins at the next character boundary (p. 21).
auto tx_boundary_armed(const Acia6551_t* a) noexcept -> bool {
  if (!has_clock(a) || frame_cycles(a) == 0) {
    return false;
  }
  if (latch(a, acia_latch::tx_busy)) {
    return true;
  }
  if (tic(a) == acia_command::tic_irq && dtr(a) && transmitter_sends(a)) {
    return true;
  }
  return tic(a) == acia_command::tic_break && !a->break_level;
}

auto next_tx_boundary(const Acia6551_t* a) noexcept -> uint64_t {
  if (latch(a, acia_latch::tx_busy)) {
    return a->tx_busy_until;
  }
  const uint64_t frame = frame_cycles(a);
  const uint64_t anchor = a->tx_anchor <= a->synced ? a->tx_anchor : a->synced;
  const uint64_t elapsed = a->synced - anchor;
  return anchor + ((elapsed / frame) + 1) * frame;
}

auto on_tx_boundary(Acia6551_t* a, uint64_t at) noexcept -> void {
  set_latch(a, acia_latch::tx_busy, false);
  if (tic(a) == acia_command::tic_break) {
    a->break_level = true;
    return;
  }
  if (latch(a, acia_latch::tdr_full) && transmitter_sends(a)) {
    start_transmit(a, at);
    return;
  }
  a->tx_anchor = at;
  if (tic(a) == acia_command::tic_irq && dtr(a) && transmitter_sends(a)) {
    raise_irq(a, false);
  }
}

// A byte completing into a full RDR sets overrun and is lost; the RDR keeps
// the earlier byte (W65C51S p. 18). The error bits clear together on the
// next clean byte (SY6551 Fig. 8; W65C51S p. 20). DTR gates the interrupt,
// not the completion (W65C51S p. 23, note 2).
auto on_rx_rdrf(Acia6551_t* a) noexcept -> void {
  a->rx_completed = true;
  const uint8_t byte = a->shift_data;
  a->shift_data = 0;
  if (latch(a, acia_latch::rdrf)) {
    set_latch(a, acia_status::overrun, true);
    return;
  }
  a->receive_data = byte;
  set_latch(a, acia_latch::rdrf, true);
  if (a->shift_errors == 0) {
    set_latch(a, acia_latch::error_mask, false);
  } else {
    set_latch(a, a->shift_errors, true);
  }
  if (dtr(a) && (a->command & acia_command::rx_irq_disable) == 0) {
    raise_irq(a, false);
  }
  // Echo mode requires TIC 00 (SY6551 Fig. 7, "bits 2 and 3 must be 0").
  if ((a->command & acia_command::echo) != 0 &&
      tic(a) == acia_command::tic_off && has_clock(a) &&
      (a->lines & acia_line::cts) != 0) {
    emit(a, byte);
  }
}

auto on_rx_free(Acia6551_t* a, uint64_t at) noexcept -> void {
  set_latch(a, acia_latch::rx_busy, false);
  a->rx_freed_cycle = at;
}

// Time went backwards (a restored snapshot, a swapped CPU context): nothing
// elapsed, and whatever was in flight keeps the time it had left.
auto rebase(Acia6551_t* a, uint64_t now) noexcept -> void {
  const uint64_t back = a->synced - now;
  a->synced = now;
  a->previous_synced = now;
  a->tx_anchor = a->tx_anchor >= back ? a->tx_anchor - back : now;
  a->tx_busy_until = a->tx_busy_until >= back ? a->tx_busy_until - back : now;
  a->rx_rdrf_at = a->rx_rdrf_at >= back ? a->rx_rdrf_at - back : now;
  a->rx_free_at = a->rx_free_at >= back ? a->rx_free_at - back : now;
  a->rx_freed_cycle = a->rx_freed_cycle >= back ? a->rx_freed_cycle - back : 0;
  a->rdr_emptied_cycle =
      a->rdr_emptied_cycle >= back ? a->rdr_emptied_cycle - back : 0;
}

auto advance(Acia6551_t* a, uint64_t now) noexcept -> void {
  if (now < a->synced) {
    rebase(a, now);
    return;
  }
  if (now == a->synced) {
    return;
  }
  a->previous_synced = a->synced;
  for (;;) {
    const uint64_t next = acia_next_event(a);
    if (next == 0 || next > now) {
      break;
    }
    // Decided before the receive events run: they re-anchor the clock at
    // this cycle, which would hide a transmit boundary coinciding with them.
    const bool tx_due = tx_boundary_armed(a) && next_tx_boundary(a) == next;
    a->synced = next;
    if (latch(a, acia_latch::rx_busy) && !a->rx_completed &&
        a->rx_rdrf_at == next) {
      on_rx_rdrf(a);
    }
    if (latch(a, acia_latch::rx_busy) && a->rx_free_at == next) {
      on_rx_free(a, next);
    }
    if (tx_due) {
      on_tx_boundary(a, next);
    }
  }
  a->synced = now;
}

}  // namespace

auto acia_cycles_for(const Acia6551_t* a, uint32_t sixteenths) noexcept
    -> uint64_t {
  if (a == nullptr || !has_clock(a)) {
    return 0;
  }
  // A bit is 16 x divisor crystal periods; the largest product (50 baud, an
  // 11-bit frame) is below 2^50.
  const uint64_t numerator = a->clock_mhz * divisor_of(a) * sixteenths;
  return (numerator * 2 + crystal_millihertz) / (2 * crystal_millihertz);
}

auto acia_frame_sixteenths(const Acia6551_t* a) noexcept -> uint32_t {
  if (a == nullptr) {
    return 0;
  }
  const uint32_t parity_bits = parity_of(a) == parity_none ? 0 : 1;
  return (1 + data_bits(a) + parity_bits) * sixteenths_per_bit +
         stop_half_bits(a) * sixteenths_per_half_bit;
}

auto acia_rdrf_sixteenths(const Acia6551_t* a) noexcept -> uint32_t {
  if (a == nullptr) {
    return 0;
  }
  const uint32_t parity_bits = parity_of(a) == parity_none ? 0 : 1;
  return (1 + data_bits(a) + parity_bits) * sixteenths_per_bit +
         rdrf_sixteenths_into_stop;
}

auto acia_set_clock_mhz(Acia6551_t* a, uint64_t clock_mhz) noexcept -> void {
  if (a != nullptr) {
    a->clock_mhz = clock_mhz;
  }
}

auto acia_reset(Acia6551_t* a, uint64_t now) noexcept -> void {
  if (a == nullptr) {
    return;
  }
  a->control = 0;
  a->command = 0;
  a->status_latches = 0;
  a->irq = false;
  a->irq_from_lines = false;
  a->break_level = false;
  a->rx_completed = false;
  a->synced = now;
  a->previous_synced = now;
  a->tx_anchor = now;
  a->rx_freed_cycle = now;
  a->rdr_emptied_cycle = now;
  a->has_pending_out = false;
}

auto acia_programmed_reset(Acia6551_t* a, uint64_t now) noexcept -> void {
  if (a == nullptr) {
    return;
  }
  advance(a, now);
  const bool was_break = tic(a) == acia_command::tic_break;
  a->command &=
      static_cast<uint8_t>(~(acia_command::dtr | acia_command::rx_irq_disable |
                             acia_command::tic_mask | acia_command::echo));
  set_latch(a, acia_status::overrun, false);
  if (a->irq && a->irq_from_lines) {
    a->irq = false;
    a->irq_from_lines = false;
  }
  if (was_break) {
    a->break_level = false;
  }
}

auto acia_restart(Acia6551_t* a, uint64_t now) noexcept -> void {
  if (a == nullptr) {
    return;
  }
  a->status_latches &= static_cast<uint8_t>(~acia_latch::reserved);
  if (!has_clock(a)) {
    a->status_latches &=
        static_cast<uint8_t>(~(acia_latch::tx_busy | acia_latch::rx_busy));
  }
  a->irq_from_lines = false;
  a->break_level = tic(a) == acia_command::tic_break;
  a->rx_completed = false;
  a->shift_errors = 0;
  a->synced = now;
  a->previous_synced = now;
  a->tx_anchor = now;
  a->rx_freed_cycle = now;
  a->rdr_emptied_cycle = now;
  a->has_pending_out = false;
  if (latch(a, acia_latch::tx_busy)) {
    a->tx_busy_until = now + frame_cycles(a);
  }
  if (latch(a, acia_latch::rx_busy)) {
    a->rx_rdrf_at = now + acia_cycles_for(a, acia_rdrf_sixteenths(a));
    a->rx_free_at = now + frame_cycles(a);
  }
}

auto acia_step(Acia6551_t* a, uint64_t now, uint8_t* byte_out) noexcept
    -> bool {
  if (a == nullptr) {
    return false;
  }
  if (!a->has_pending_out) {
    advance(a, now);
  }
  if (!a->has_pending_out) {
    return false;
  }
  a->has_pending_out = false;
  if (byte_out != nullptr) {
    *byte_out = a->pending_out;
  }
  return true;
}

auto acia_irq(const Acia6551_t* a) noexcept -> bool {
  return a != nullptr && a->irq;
}

auto acia_read(Acia6551_t* a, uint8_t reg, uint64_t now) noexcept -> uint8_t {
  if (a == nullptr) {
    return 0;
  }
  advance(a, now);
  switch (reg & acia_reg::mask) {
    case acia_reg::data:
      // Clears RDRF alone (SY6551 Fig. 8); the register "will contain the
      // last valid data word received" (W65C51S p. 18, inferred for a read
      // with RDRF clear).
      if (latch(a, acia_latch::rdrf)) {
        set_latch(a, acia_latch::rdrf, false);
        a->rdr_emptied_cycle = now;
      }
      return a->receive_data;
    case acia_reg::status: {
      // DSR and DCD read 1 when the line is not asserted; the read clears
      // the IRQ latch (SY6551 Fig. 8; W65C51S p. 8).
      uint8_t status =
          a->status_latches & (acia_latch::error_mask | acia_latch::rdrf);
      if (!latch(a, acia_latch::tdr_full)) {
        status |= acia_status::tdre;
      }
      if ((a->lines & acia_line::dcd) == 0) {
        status |= acia_status::dcd;
      }
      if ((a->lines & acia_line::dsr) == 0) {
        status |= acia_status::dsr;
      }
      if (a->irq) {
        status |= acia_status::irq;
      }
      a->irq = false;
      a->irq_from_lines = false;
      return status;
    }
    case acia_reg::command:
      return a->command;
    case acia_reg::control:
    default:
      return a->control;
  }
}

auto acia_write(Acia6551_t* a, uint8_t reg, uint8_t value, uint64_t now,
                uint8_t* byte_out) noexcept -> bool {
  if (a == nullptr) {
    return false;
  }
  advance(a, now);
  switch (reg & acia_reg::mask) {
    case acia_reg::data:
      // A write to a full TDR replaces it: no sheet offers any protection.
      a->transmit_data = value;
      set_latch(a, acia_latch::tdr_full, true);
      try_start_transmit(a, now);
      break;
    case acia_reg::status:
      acia_programmed_reset(a, now);
      break;
    case acia_reg::command: {
      const bool was_break = tic(a) == acia_command::tic_break;
      a->command = value;
      // Leaving break mode "generates an immediate Stop Bit" (W65C51S
      // p. 21). Disabling an interrupt source leaves a set latch, by analogy
      // with the programmed reset (p. 23); the sheets do not say so outright.
      if (was_break && tic(a) != acia_command::tic_break) {
        a->break_level = false;
      }
      try_start_transmit(a, now);
      break;
    }
    case acia_reg::control:
    default: {
      const bool had_clock = has_clock(a);
      a->control = value;
      // Whether the character clock runs from reset or from the rate being
      // selected is not stated (inferred: from the moment it exists).
      if (!had_clock && has_clock(a)) {
        a->tx_anchor = now;
      }
      try_start_transmit(a, now);
      break;
    }
  }
  return acia_step(a, now, byte_out);
}

auto acia_set_lines(Acia6551_t* a, uint8_t lines, uint64_t now) noexcept
    -> void {
  if (a == nullptr) {
    return;
  }
  advance(a, now);
  const uint8_t previous = a->lines;
  a->lines = lines & acia_line::all_asserted;
  const uint8_t modem = acia_line::dsr | acia_line::dcd;
  if (((previous ^ a->lines) & modem) != 0 && dtr(a)) {
    raise_irq(a, true);
  }
  try_start_transmit(a, now);
}

auto acia_rx_ready(const Acia6551_t* a) noexcept -> bool {
  if (a == nullptr) {
    return false;
  }
  // DCD "must be low for the Receiver to operate" (SY6551 p. 3-174); DTR
  // deasserted disables it (Fig. 7).
  return has_clock(a) && dtr(a) && (a->lines & acia_line::dcd) != 0 &&
         !latch(a, acia_latch::rx_busy) && !latch(a, acia_latch::rdrf);
}

auto acia_rx_start(Acia6551_t* a, uint8_t byte, uint8_t errors,
                   uint64_t now) noexcept -> void {
  if (a == nullptr || !has_clock(a)) {
    return;
  }
  advance(a, now);
  if (latch(a, acia_latch::rx_busy)) {
    return;
  }
  // A byte pulled at the first opportunity starts where the receiver became
  // ready, if that was inside the interval just advanced through; one that
  // found the receiver idle starts when it is noticed.
  const uint64_t ready_since = a->rx_freed_cycle > a->rdr_emptied_cycle
                                   ? a->rx_freed_cycle
                                   : a->rdr_emptied_cycle;
  const uint64_t start = ready_since > a->previous_synced && ready_since <= now
                             ? ready_since
                             : now;
  a->shift_data = byte;
  a->shift_errors = errors & acia_latch::error_mask &
                    static_cast<uint8_t>(~acia_status::overrun);
  a->rx_completed = false;
  set_latch(a, acia_latch::rx_busy, true);
  a->rx_rdrf_at = start + acia_cycles_for(a, acia_rdrf_sixteenths(a));
  a->rx_free_at = start + frame_cycles(a);
  advance(a, now);
}

auto acia_line_view(const Acia6551_t* a, AciaLine_t* out) noexcept -> void {
  if (a == nullptr || out == nullptr) {
    return;
  }
  out->baud = has_clock(a) ? bauds.at(a->control & acia_control::baud_mask) : 0;
  out->data_bits = data_bits(a);
  out->parity = parity_of(a);
  out->stop_half_bits = stop_half_bits(a);
  out->dtr = dtr(a) ? 1 : 0;
  // RTS follows the transmitter being on (SY6551 Fig. 7, TIC 01, 10 and 11).
  out->rts = tic(a) != acia_command::tic_off ? 1 : 0;
  out->brk = a->break_level ? 1 : 0;
}

auto acia_next_event(const Acia6551_t* a) noexcept -> uint64_t {
  if (a == nullptr) {
    return 0;
  }
  uint64_t next = 0;
  if (tx_boundary_armed(a)) {
    next = next_tx_boundary(a);
  }
  if (latch(a, acia_latch::rx_busy)) {
    const uint64_t rx = a->rx_completed ? a->rx_free_at : a->rx_rdrf_at;
    if (next == 0 || rx < next) {
      next = rx;
    }
  }
  return next;
}
