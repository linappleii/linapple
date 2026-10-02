// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <type_traits>

// Synertek SY6551 asynchronous communication interface adapter, the part the
// Super Serial Card shipped with (1981 SSC manual p. 45, p. 97). Register
// select, the flags and the reset rows are the Synertek sheet's (SY6551,
// Synertek 1981-1982 Data Catalog pp. 3-169 to 3-176); the Synertek and
// Rockwell sheets carry no timing figures, so what happens when inside a
// character is cited to WDC's W65C51S sheet (June 2007), whose part keeps
// the original behaviour. The W65C51N, whose TDRE is stuck at 1, is not
// this chip.

namespace acia_reg {
constexpr uint8_t data = 0;
constexpr uint8_t status = 1;
constexpr uint8_t command = 2;
constexpr uint8_t control = 3;
constexpr uint8_t mask = 0x03;
}  // namespace acia_reg

// Status register bits (SY6551 Fig. 8, p. 3-176).
namespace acia_status {
constexpr uint8_t parity_error = 0x01;
constexpr uint8_t framing_error = 0x02;
constexpr uint8_t overrun = 0x04;
constexpr uint8_t rdrf = 0x08;
constexpr uint8_t tdre = 0x10;
constexpr uint8_t dcd = 0x20;
constexpr uint8_t dsr = 0x40;
constexpr uint8_t irq = 0x80;
}  // namespace acia_status

// Command register bits (SY6551 Fig. 7, p. 3-175).
namespace acia_command {
constexpr uint8_t dtr = 0x01;
constexpr uint8_t rx_irq_disable = 0x02;
constexpr uint8_t tic_mask = 0x0C;
constexpr uint8_t tic_off = 0x00;
constexpr uint8_t tic_irq = 0x04;
constexpr uint8_t tic_on = 0x08;
constexpr uint8_t tic_break = 0x0C;
constexpr uint8_t echo = 0x10;
constexpr uint8_t parity_enable = 0x20;
constexpr uint8_t parity_mode_mask = 0xC0;
constexpr uint8_t parity_mode_shift = 6;
}  // namespace acia_command

// Control register bits (SY6551 Fig. 6, p. 3-175).
namespace acia_control {
constexpr uint8_t baud_mask = 0x0F;
constexpr uint8_t receiver_clock_internal = 0x10;
constexpr uint8_t word_length_mask = 0x60;
constexpr uint8_t word_length_shift = 5;
constexpr uint8_t two_stop_bits = 0x80;
}  // namespace acia_control

// The latch byte: the four low status bits as the sheet numbers them, then
// the three facts the status register derives from. TDRE reads as the
// inverse of tdr_full; a shifter's cycle below is meaningful only while its
// busy bit is set. Bit 7 is never set.
namespace acia_latch {
constexpr uint8_t error_mask = 0x07;
constexpr uint8_t rdrf = 0x08;
constexpr uint8_t tdr_full = 0x10;
constexpr uint8_t tx_busy = 0x20;
constexpr uint8_t rx_busy = 0x40;
constexpr uint8_t reserved = 0x80;
}  // namespace acia_latch

// The modem inputs as a mask, 1 = asserted (the RS-232 line at its active
// level). On the SSC all three read asserted with no cable (15 kOhm pull-ups,
// 1981 manual p. 48).
namespace acia_line {
constexpr uint8_t cts = 0x01;
constexpr uint8_t dsr = 0x02;
constexpr uint8_t dcd = 0x04;
constexpr uint8_t all_asserted = 0x07;
}  // namespace acia_line

// The format the chip is programmed for and the outputs it drives. parity is
// 0 none, 1 odd, 2 even, 3 mark, 4 space; stop_half_bits is 2, 3 or 4; baud
// is 0 when the control register selects a clock the board does not supply.
// dtr, rts and brk are 1 = asserted.
struct AciaLine_t {
  uint32_t baud = 0;
  uint8_t data_bits = 8;
  uint8_t parity = 0;
  uint8_t stop_half_bits = 2;
  uint8_t dtr = 0;
  uint8_t rts = 0;
  uint8_t brk = 0;
};

// Time is the host's cumulative 6502 cycle count. Every point in a character
// is a count of sixteenths of a bit from the start bit's leading edge, and
// the chip converts it to cycles with one rounded 64-bit division over the
// host clock in millihertz, so a 10-bit character at 9600 baud is 1,063
// NTSC cycles and the same arithmetic serves a PAL machine. The chip is
// advanced lazily: every event is computed at its own cycle, never at the
// access that notices it, and a now below synced is treated as no time
// having passed.
struct Acia6551_t {
  uint8_t control = 0;
  uint8_t command = 0;
  uint8_t status_latches = 0;
  uint8_t receive_data = 0;
  uint8_t transmit_data = 0;
  uint8_t shift_data = 0;
  uint8_t shift_errors = 0;
  uint8_t lines = acia_line::all_asserted;
  bool irq = false;
  // A programmed reset releases an IRQ caused by a DSR or DCD transition and
  // leaves one caused by anything else (W65C51S p. 23, note 4).
  bool irq_from_lines = false;
  bool break_level = false;
  // The RDRF point of the byte in flight has passed; the shifter is still
  // busy until the frame ends.
  bool rx_completed = false;
  uint64_t clock_mhz = 0;
  uint64_t synced = 0;
  uint64_t previous_synced = 0;
  uint64_t tx_busy_until = 0;
  // The start of the transmitter's character clock: a write to an idle
  // transmitter starts a start bit at once and so re-anchors it, and with
  // TIC 01 and nothing to send the interrupt recurs at every boundary of it
  // (W65C51S p. 17).
  uint64_t tx_anchor = 0;
  uint64_t rx_rdrf_at = 0;
  uint64_t rx_free_at = 0;
  // When the receiver last fell idle and when the RDR was last emptied: a
  // byte pulled from the line at the first opportunity after both starts
  // there, not at the access that pulls it.
  uint64_t rx_freed_cycle = 0;
  uint64_t rdr_emptied_cycle = 0;
  // A byte that left the chip during an advance the caller did not step
  // through, held for the next acia_step.
  uint8_t pending_out = 0;
  bool has_pending_out = false;
};

static_assert(std::is_standard_layout<Acia6551_t>::value,
              "Acia6551_t must satisfy standard layout guarantees");

auto acia_set_clock_mhz(Acia6551_t* a, uint64_t clock_mhz) noexcept -> void;
// Hardware reset: control and command cleared, TDRE set, the error bits and
// RDRF cleared, the shifters idle, the clock re-anchored at now; the RDR
// keeps its byte (W65C51S p. 13 itemises control, command and status only;
// inferred for the RDR). The inputs are the board's and are kept.
auto acia_reset(Acia6551_t* a, uint64_t now) noexcept -> void;
// A write to the status register: command bits 4-0 and the overrun bit
// cleared, nothing else (R6551 p. 3; W65C51S pp. 14, 23).
auto acia_programmed_reset(Acia6551_t* a, uint64_t now) noexcept -> void;
// After the registers, latches and data bytes have been put back from a
// saved state, which carries no cycles: the clock is anchored at now and a
// character in flight restarts from its first bit, so TDRE or RDRF arrives
// up to one character time later than it would have and no byte is lost or
// doubled. A shifter marked busy with no clock selected is made idle.
auto acia_restart(Acia6551_t* a, uint64_t now) noexcept -> void;
// Advances the chip to now. Returns true and the byte when one left the
// transmitter during the advance, or was left over from an earlier one; the
// caller steps until it returns false.
auto acia_step(Acia6551_t* a, uint64_t now, uint8_t* byte_out) noexcept -> bool;
auto acia_read(Acia6551_t* a, uint8_t reg, uint64_t now) noexcept -> uint8_t;
// Returns true and the byte when the write itself sends one: a byte written
// to an idle transmitter starts at once.
auto acia_write(Acia6551_t* a, uint8_t reg, uint8_t value, uint64_t now,
                uint8_t* byte_out) noexcept -> bool;
auto acia_irq(const Acia6551_t* a) noexcept -> bool;
// The modem inputs as an acia_line mask. A change on DSR or DCD with DTR
// asserted sets the IRQ latch (SY6551 p. 3-173).
auto acia_set_lines(Acia6551_t* a, uint8_t lines, uint64_t now) noexcept
    -> void;
// True while the receiver can take a byte from the line: a clock, DTR and
// DCD asserted, the shifter idle and the RDR empty.
auto acia_rx_ready(const Acia6551_t* a) noexcept -> bool;
// Starts receiving a byte. errors carries acia_status::framing_error and
// parity_error as the frame will report them.
auto acia_rx_start(Acia6551_t* a, uint8_t byte, uint8_t errors,
                   uint64_t now) noexcept -> void;
auto acia_line_view(const Acia6551_t* a, AciaLine_t* out) noexcept -> void;
// The earliest cycle at which something happens that no register access
// brings about: the transmit character boundary, the receiver's RDRF point
// or its free point. 0 when nothing is pending.
auto acia_next_event(const Acia6551_t* a) noexcept -> uint64_t;
// Cycles from a start bit's leading edge to a point sixteenths of a bit
// into the character at the programmed rate; 0 with no clock.
auto acia_cycles_for(const Acia6551_t* a, uint32_t sixteenths) noexcept
    -> uint64_t;
// The length of a character in sixteenths of a bit, and the point at which
// RDRF is set, 9 sixteenths into the first stop bit (W65C51S p. 16).
auto acia_frame_sixteenths(const Acia6551_t* a) noexcept -> uint32_t;
auto acia_rdrf_sixteenths(const Acia6551_t* a) noexcept -> uint32_t;
