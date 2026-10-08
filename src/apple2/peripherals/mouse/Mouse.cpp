// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/mouse/Mouse.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/chips/6821.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "apple2/peripherals/mouse/MouseRom.h"

namespace {

constexpr int min_slot = 1;
constexpr int max_slot = 7;
constexpr uint32_t io_register_mask = 0x03;
constexpr int byte_shift = 8;
constexpr uint8_t byte_mask = 0xFF;

// Power-on clamps (AppleMouse II User's Manual p. 48). Signed because the
// manual's positions run "-32768 to +32767 (or 0 to +65535)" (p. 45); how the
// 6805 compares is unknown.
constexpr int16_t default_clamp_min = 0;
constexpr int16_t default_clamp_max = 1023;

// The 6805's timer runs from the Apple's Q3 through the PAL's "2 MHZ CLOCK"
// (schematic 050-0101-A zones A3, C2), so its tick is a fixed count of 6502
// cycles. The count is in the 6805's ROM and unknown; one NTSC or PAL frame
// (IIe Technical Reference p. 169) is the only count under which Tech Note
// Mouse #2's "synchronized with the actual VBL rate" holds. A real card may
// drift: its 8-bit timer is reloaded by software (MC6805P data sheet 5.1).
constexpr uint64_t tick_period_60hz = 17030;
constexpr uint64_t tick_period_50hz = 20280;

// The first byte the firmware writes to the 6805; the high nibble names the
// command (manual pp. 47-49; the undocumented ones are what the entries at
// $Cn1A and $Cn1D-$Cn1F send).
namespace command {
constexpr uint8_t set_mouse = 0x00;
constexpr uint8_t read_mouse = 0x10;
constexpr uint8_t serve_mouse = 0x20;
constexpr uint8_t clear_mouse = 0x30;
constexpr uint8_t pos_mouse = 0x40;
constexpr uint8_t init_mouse = 0x50;
constexpr uint8_t clamp_mouse = 0x60;
constexpr uint8_t home_mouse = 0x70;
constexpr uint8_t basic_mode = 0x80;
constexpr uint8_t time_data = 0x90;
constexpr uint8_t with_data_byte = 0xA0;
constexpr uint8_t peek_poke = 0xF0;
constexpr uint8_t peek = 0xF0;
constexpr uint8_t poke = 0xF1;
constexpr uint8_t group_mask = 0xF0;
constexpr uint8_t axis_bit = 0x01;
// TIMEDATA's bit 0 selects 50 Hz "effective at the next INITMOUSE" (Tech Note
// Mouse #2); bits 3-2 add one to three data bytes (bank 7 $C441-$C4AE).
constexpr uint8_t time_data_rate_bit = 0x01;
constexpr uint8_t time_data_length_mask = 0x0C;
constexpr uint8_t time_data_two_bytes = 0x08;
constexpr uint8_t time_data_three_bytes = 0x04;
constexpr uint8_t time_data_four_bytes = 0x0C;
}  // namespace command

constexpr uint32_t read_mouse_reply_bytes = 5;
constexpr uint32_t five_byte_command = 5;
constexpr uint32_t peek_command_bytes = 3;
constexpr uint32_t poke_command_bytes = 4;
// Arbitrary and unsourced: the firmware stores INITMOUSE's reply at $6F8+n
// and never reads it.
constexpr uint8_t init_mouse_reply = 0xFF;

// Bits 7 and 6 are rebuilt from the button fields; 4 and 0 are reserved.
constexpr uint8_t frame_status_mask = 0x2E;
constexpr uint32_t frame_word_max = 0xFFFF;
constexpr uint32_t frame_buffer_size = 8;

// Mode byte bits (manual p. 44).
namespace mode {
constexpr uint8_t tracking = 0x01;
constexpr uint8_t movement_interrupts = 0x02;
constexpr uint8_t button_interrupts = 0x04;
constexpr uint8_t refresh_interrupts = 0x08;
constexpr uint8_t mask = 0x0F;
}  // namespace mode

// Status byte bits (manual p. 45). The interrupt sources share the mode's bit
// positions, which lets the mode mask them. Bits 4 and 0 are reserved: the
// card has one button (schematic: SW on J1-4 to the 6805's PB7).
namespace status {
constexpr uint8_t movement_interrupt = 0x02;
constexpr uint8_t button_interrupt = 0x04;
constexpr uint8_t refresh_interrupt = 0x08;
constexpr uint8_t moved = 0x20;
constexpr uint8_t button_at_last_read = 0x40;
constexpr uint8_t button_down = 0x80;
constexpr uint8_t interrupt_sources = 0x0E;
}  // namespace status

// PIA port B: PB1-PB3 drive the ROM's A8-A10, PB4 and PB5 are the read and
// write strobes to the 6805, PB6 and PB7 its "byte ready" and "busy" replies
// (schematic 050-0101-A zones B3, C2).
namespace port_b {
constexpr uint8_t bank_mask = 0x0E;
constexpr uint8_t bank_shift = 1;
constexpr uint8_t read_strobe = 0x10;
constexpr uint8_t write_strobe = 0x20;
constexpr uint8_t byte_ready = 0x40;
constexpr uint8_t busy = 0x80;
constexpr uint8_t card_driven = 0x3E;
constexpr uint8_t pia_inputs = 0xC1;
}  // namespace port_b

// GetClamp peeks $4E down to $47 for MaxYL, MaxXL, MaxYH, MaxXH, MinYL, MinXL,
// MinYH, MinXH (Tech Note Mouse #7); the MC6805P2's 64 bytes of RAM are
// $10-$4F (data sheet 3.1). That the operand is that RAM address is inferred.
namespace peek_address {
constexpr uint16_t max_y_low = 0x4E;
constexpr uint16_t max_x_low = 0x4D;
constexpr uint16_t max_y_high = 0x4C;
constexpr uint16_t max_x_high = 0x4B;
constexpr uint16_t min_y_low = 0x4A;
constexpr uint16_t min_x_low = 0x49;
constexpr uint16_t min_y_high = 0x48;
constexpr uint16_t min_x_high = 0x47;
}  // namespace peek_address

static_assert(sizeof(MouseSaveState_t) == 92,
              "the mouse card's state frame is part of the plugin ABI");
static_assert(offsetof(MouseSaveState_t, version) == 0,
              "the frame header is version then size");
static_assert(offsetof(MouseSaveState_t, struct_size) == 4,
              "the frame header is version then size");
static_assert(offsetof(MouseSaveState_t, position_x) == 8,
              "the position sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, position_y) == 12,
              "the position sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, min_x) == 16,
              "the clamps sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, max_x) == 20,
              "the clamps sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, min_y) == 24,
              "the clamps sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, max_y) == 28,
              "the clamps sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, tick_phase) == 32,
              "the phase takes the word older frames held the host width in");
static_assert(offsetof(MouseSaveState_t, reserved0) == 36,
              "the host height's word is reserved");
static_assert(offsetof(MouseSaveState_t, read_x) == 40,
              "the last reading sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, read_y) == 44,
              "the last reading sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, parser_pos) == 48,
              "the parser sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, parser_out_len) == 52,
              "the parser sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, pia_ora) == 56,
              "the PIA's six registers sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, pia_port_a_in) == 62,
              "the PIA's inputs sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, pia_port_b_in) == 63,
              "the PIA's inputs sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, rate_50hz) == 64,
              "the rate takes a byte older frames held as zero");
static_assert(offsetof(MouseSaveState_t, pending) == 65,
              "the pending sources take a byte older frames held as zero");
static_assert(offsetof(MouseSaveState_t, irq_asserted) == 66,
              "the IRQ level takes a byte older frames held as zero");
static_assert(offsetof(MouseSaveState_t, reserved1) == 67,
              "one of the PIA's unconnected pins stays reserved");
static_assert(offsetof(MouseSaveState_t, parser_in_len) == 68,
              "the reply length takes a byte older frames held as zero");
static_assert(offsetof(MouseSaveState_t, parser_reply_pos) == 69,
              "the reply cursor takes a byte older frames held as zero");
static_assert(offsetof(MouseSaveState_t, reserved2) == 70,
              "the PIA's two IRQ outputs stay reserved");
static_assert(offsetof(MouseSaveState_t, pia_port_a_shadow) == 72,
              "the shadows sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, pia_port_b_shadow) == 73,
              "the shadows sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, mode) == 74,
              "the mode sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, reserved3) == 75,
              "the retired VBL edge byte is reserved");
static_assert(offsetof(MouseSaveState_t, status) == 76,
              "the status byte sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, button_at_last_read) == 77,
              "the button memory sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, reserved4) == 78,
              "the second button's memory is reserved");
static_assert(offsetof(MouseSaveState_t, button) == 79,
              "the button sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, reserved5) == 80,
              "the second button's level is reserved");
static_assert(offsetof(MouseSaveState_t, buffer) == 81,
              "the command buffer sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, padding) == 89,
              "three bytes of padding close the frame");

struct MouseCard_t {
  HostInterface_t* host = nullptr;
  int slot = 0;

  Pia6821 pia{};
  uint8_t port_a_shadow = 0;
  uint8_t port_b_shadow = 0;

  std::array<uint8_t, 8> buffer{};
  uint32_t pos = 0;
  uint32_t out_len = 1;
  uint32_t in_len = 0;
  uint32_t reply_pos = 0;

  uint8_t mode = 0;
  int16_t position_x = 0;
  int16_t position_y = 0;
  int16_t min_x = default_clamp_min;
  int16_t max_x = default_clamp_max;
  int16_t min_y = default_clamp_min;
  int16_t max_y = default_clamp_max;
  int16_t read_x = 0;
  int16_t read_y = 0;
  bool button = false;
  bool button_at_last_read = false;

  uint8_t status = 0;
  uint8_t pending = 0;
  bool irq_asserted = false;
  bool rate_50hz = false;
  uint64_t tick_period = tick_period_60hz;
  uint64_t next_tick = 0;
};

auto low_byte(int16_t value) -> uint8_t {
  return static_cast<uint8_t>(static_cast<uint16_t>(value) & byte_mask);
}

auto high_byte(int16_t value) -> uint8_t {
  return static_cast<uint8_t>(static_cast<uint16_t>(value) >> byte_shift);
}

auto word_of(uint8_t low, uint8_t high) -> int16_t {
  return static_cast<int16_t>(
      static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << byte_shift)));
}

// A minimum above the maximum is stored as given and pins the position at the
// minimum: the 6805's choice is unknown, this one is deterministic.
auto clamp_axis(int32_t value, int16_t low, int16_t high) -> int16_t {
  return static_cast<int16_t>(
      std::max<int32_t>(low, std::min<int32_t>(high, value)));
}

auto register_bank(MouseCard_t* card) -> void {
  const uint32_t bank =
      (static_cast<uint32_t>(card->port_b_shadow) & port_b::bank_mask) >>
      port_b::bank_shift;
  card->host->RegisterCxROM(card->slot,
                            mouse_rom.data() + bank * mouse_rom_bank_size);
}

// IRQ' is the 6805's PB6 through a 10 k pull-up; the PIA's interrupt pins are
// not connected (schematic zones D2, C3), so the line is the card's own latch.
auto set_irq(MouseCard_t* card, bool level) -> void {
  if (card->irq_asserted == level) {
    return;
  }
  card->irq_asserted = level;
  card->host->AssertIrq(card->slot, level);
}

// Every source interrupts "at the end of the current monitor screen writing
// cycle" (manual p. 46), the tick. Bit 0 gates movement and button, not the
// refresh: "$08 (mouse off but VBL interrupt on) will generate VBL interrupts"
// (Tech Note Mouse #3).
auto fire(MouseCard_t* card) -> void {
  uint8_t reportable = 0;
  if ((card->mode & mode::tracking) != 0) {
    reportable = card->pending & card->mode &
                 (mode::movement_interrupts | mode::button_interrupts);
  }
  if ((card->mode & mode::refresh_interrupts) != 0) {
    reportable |= status::refresh_interrupt;
  }
  card->pending = 0;
  if (reportable == 0) {
    return;
  }
  card->status |= reportable;
  set_irq(card, true);
}

// Missed ticks collapse into one with the phase kept: the status bits are
// OR'd, so N firings and one are the same observable. A counter that went
// backwards (a restored snapshot) re-anchors.
auto advance(MouseCard_t* card, uint64_t now) -> void {
  if (now + card->tick_period < card->next_tick) {
    card->next_tick = now + card->tick_period;
  }
  if (card->next_tick <= now) {
    fire(card);
    card->next_tick +=
        ((now - card->next_tick) / card->tick_period + 1) * card->tick_period;
  }
  card->host->ScheduleEvent(card, card->next_tick);
}

// The 6805's latency from the INITMOUSE strobe to its timer restart is
// unknown and modelled as zero.
auto restart_tick(MouseCard_t* card) -> void {
  card->tick_period = card->rate_50hz ? tick_period_50hz : tick_period_60hz;
  card->next_tick = card->host->GetCycles() + card->tick_period;
  card->host->ScheduleEvent(card, card->next_tick);
}

// Bytes per command, the command byte included, as the firmware sends them:
// POSMOUSE and CLAMPMOUSE five (bank 7 $C418-$C43F), TIMEDATA by its bits 3-2,
// the $Cn1D entry two (bank 7 $C413-$C416), the peek three and the poke four
// (bank 1 $C48B-$C4DE).
auto command_out_len(uint8_t cmd) -> uint32_t {
  switch (cmd & command::group_mask) {
    case command::pos_mouse:
    case command::clamp_mouse:
      return five_byte_command;
    case command::time_data:
      switch (cmd & command::time_data_length_mask) {
        case command::time_data_two_bytes:
          return 2;
        case command::time_data_three_bytes:
          return 3;
        case command::time_data_four_bytes:
          return 4;
        default:
          return 1;
      }
    case command::with_data_byte:
      return 2;
    case command::peek_poke:
      if (cmd == command::peek) {
        return peek_command_bytes;
      }
      if (cmd == command::poke) {
        return poke_command_bytes;
      }
      return 1;
    default:
      return 1;
  }
}

// The bytes the 6805 answers with: READMOUSE five (bank 6 $C4CA-$C4DD),
// SERVEMOUSE, INITMOUSE and the peek one.
auto command_in_len(uint8_t cmd) -> uint32_t {
  switch (cmd & command::group_mask) {
    case command::read_mouse:
      return read_mouse_reply_bytes;
    case command::serve_mouse:
    case command::init_mouse:
      return 1;
    case command::peek_poke:
      return cmd == command::peek ? 1 : 0;
    default:
      return 0;
  }
}

auto peek_byte(const MouseCard_t* card, uint16_t address) -> uint8_t {
  switch (address) {
    case peek_address::max_y_low:
      return low_byte(card->max_y);
    case peek_address::max_x_low:
      return low_byte(card->max_x);
    case peek_address::max_y_high:
      return high_byte(card->max_y);
    case peek_address::max_x_high:
      return high_byte(card->max_x);
    case peek_address::min_y_low:
      return low_byte(card->min_y);
    case peek_address::min_x_low:
      return low_byte(card->min_x);
    case peek_address::min_y_high:
      return high_byte(card->min_y);
    case peek_address::min_x_high:
      return high_byte(card->min_x);
    default:
      return 0;
  }
}

// Bits 1-3 of the reply read 0 (manual p. 47) and the 6805 forgets the sources
// with them, so a SERVEMOUSE after a READMOUSE finds nothing (inferred; the
// nearest support is IIc Technical Reference Table 9-3, p. 180). The line
// stays up until SERVEMOUSE (Tech Note Mouse #4).
auto execute_read_mouse(MouseCard_t* card) -> void {
  uint8_t reply = card->status & status::moved;
  if (card->button) {
    reply |= status::button_down;
  }
  if (card->button_at_last_read) {
    reply |= status::button_at_last_read;
  }
  card->read_x = card->position_x;
  card->read_y = card->position_y;
  card->button_at_last_read = card->button;
  card->status &=
      static_cast<uint8_t>(~(status::moved | status::interrupt_sources));
  card->buffer.at(1) = low_byte(card->read_x);
  card->buffer.at(2) = high_byte(card->read_x);
  card->buffer.at(3) = low_byte(card->read_y);
  card->buffer.at(4) = high_byte(card->read_y);
  card->buffer.at(5) = reply;
}

// INITMOUSE "sets the internal default values" (manual p. 48): off, (0, 0),
// clamps 0..1023, the rate TIMEDATA last chose. On a IIe the firmware strobes
// $50 twice, the second after the vertical blanking edge (bank 2 $C426-$C445);
// each restarts the tick, which "synchronizes it with the vertical blanking".
auto execute_init_mouse(MouseCard_t* card) -> void {
  card->mode = 0;
  card->position_x = 0;
  card->position_y = 0;
  card->min_x = default_clamp_min;
  card->max_x = default_clamp_max;
  card->min_y = default_clamp_min;
  card->max_y = default_clamp_max;
  card->pending = 0;
  card->status &= static_cast<uint8_t>(~status::interrupt_sources);
  set_irq(card, false);
  restart_tick(card);
  card->buffer.at(1) = init_mouse_reply;
}

// The firmware pushes $5F8, $578, $4F8, $478 and the command and pops them into
// the write loop (bank 7 $C418-$C43F), so the wire order is command, low
// minimum, low maximum, high minimum, high maximum (manual p. 48). The clamp
// moves nothing (IIc Technical Reference Table 9-3; weakly sourced for the
// card).
auto execute_clamp_mouse(MouseCard_t* card) -> void {
  const int16_t low = word_of(card->buffer.at(1), card->buffer.at(3));
  const int16_t high = word_of(card->buffer.at(2), card->buffer.at(4));
  if ((card->buffer.at(0) & command::axis_bit) != 0) {
    card->min_y = low;
    card->max_y = high;
    return;
  }
  card->min_x = low;
  card->max_x = high;
}

// A loaded position is taken as given and only motion clamps (inferred from the
// IIc's CLEARMOUSE zero "not necessarily within clamping boundaries", Table
// 9-3). No load marks movement.
auto execute(MouseCard_t* card) -> void {
  const uint8_t cmd = card->buffer.at(0);
  switch (cmd & command::group_mask) {
    case command::set_mouse:
      card->mode = cmd & mode::mask;
      break;

    case command::read_mouse:
      execute_read_mouse(card);
      break;

    // The firmware takes its carry from bits 1-3 of the reply alone (bank 3
    // $C4BD-$C4D4), so a consumed event must read back $00 for the second call
    // to answer "not the mouse" (manual p. 47; Tech Note Mouse #4).
    case command::serve_mouse:
      card->buffer.at(1) = card->status & status::interrupt_sources;
      card->status &= static_cast<uint8_t>(~status::interrupt_sources);
      set_irq(card, false);
      break;

    // "The button and interrupt status byte remains unchanged" (manual
    // p. 47); only INITMOUSE resets the mode and clamps (Tech Note Mouse #3).
    case command::clear_mouse:
      card->position_x = 0;
      card->position_y = 0;
      break;

    case command::pos_mouse:
      card->position_x = word_of(card->buffer.at(1), card->buffer.at(2));
      card->position_y = word_of(card->buffer.at(3), card->buffer.at(4));
      break;

    case command::init_mouse:
      execute_init_mouse(card);
      break;

    case command::clamp_mouse:
      execute_clamp_mouse(card);
      break;

    case command::home_mouse:
      card->position_x = card->min_x;
      card->position_y = card->min_y;
      break;

    // CHR$(1) after PR#n sends $80 (bank 4 $C411-$C42E) and "places the mouse
    // in BASIC mode and sets the mouse position numbers to zero" (manual
    // p. 35); nothing else in bank 4 moves the position or sets a mode, so the
    // 6805 must (inferred). CHR$(0) sends $00, SETMOUSE off.
    case command::basic_mode:
      if (cmd == command::basic_mode) {
        card->mode = mode::tracking;
        card->position_x = 0;
        card->position_y = 0;
      }
      break;

    case command::time_data:
      card->rate_50hz = (cmd & command::time_data_rate_bit) != 0;
      break;

    case command::peek_poke:
      if (cmd == command::peek) {
        card->buffer.at(peek_command_bytes) = peek_byte(
            card,
            static_cast<uint16_t>(
                card->buffer.at(1) |
                (static_cast<uint16_t>(card->buffer.at(2)) << byte_shift)));
      }
      break;

    default:
      break;
  }
}

auto present_reply(MouseCard_t* card) -> void {
  if (card->reply_pos >= card->in_len) {
    return;
  }
  pia_6821_set_port_a(&card->pia,
                      card->buffer.at(card->out_len + card->reply_pos));
}

// The buffer is cleared at a new command so a frame is a function of the last
// command alone; the reply's first byte is on port A before the firmware reads.
auto take_byte(MouseCard_t* card, uint8_t byte) -> void {
  if (card->pos == 0) {
    card->buffer.fill(0);
    card->buffer.at(0) = byte;
    card->out_len = command_out_len(byte);
    card->in_len = command_in_len(byte);
    card->reply_pos = 0;
  } else if (card->pos < card->buffer.size()) {
    card->buffer.at(card->pos) = byte;
  }
  ++card->pos;
  if (card->pos < card->out_len) {
    return;
  }
  execute(card);
  card->pos = 0;
  present_reply(card);
}

auto pia_listener_a(void* obj, uint8_t data) -> void {
  if (obj == nullptr) {
    return;
  }
  auto* card = static_cast<MouseCard_t*>(obj);
  card->port_a_shadow = data;
}

// PB5 rising: the 6805 raises "busy" on PB7; PB5 falling: it takes the byte
// from port A and drops PB7 (the firmware's write loop, bank 3 $C40E-$C43F).
auto on_write_strobe(MouseCard_t* card, uint8_t data) -> void {
  if ((data & port_b::write_strobe) != 0) {
    card->port_b_shadow |= port_b::busy;
    return;
  }
  take_byte(card, card->port_a_shadow);
  card->port_b_shadow &= static_cast<uint8_t>(~port_b::busy);
}

// PB4 rising: the 6805 drops "byte ready" on PB6; PB4 falling: it presents the
// next reply byte, if any, and raises PB6 (the firmware's read loop, bank 6
// $C486-$C4C4). An unread reply (INITMOUSE's second) stays on port A.
auto on_read_strobe(MouseCard_t* card, uint8_t data) -> void {
  if ((data & port_b::read_strobe) != 0) {
    card->port_b_shadow &= static_cast<uint8_t>(~port_b::byte_ready);
    return;
  }
  if (card->reply_pos < card->in_len) {
    ++card->reply_pos;
  }
  present_reply(card);
  card->port_b_shadow |= port_b::byte_ready;
}

// PB6 is the read strobe's complement and PB7 the write strobe's level, from
// reset on and after every edge; PB0 is the PAL's input and never driven.
auto reply_levels(uint8_t strobes) -> uint8_t {
  uint8_t levels = 0;
  if ((strobes & port_b::read_strobe) == 0) {
    levels |= port_b::byte_ready;
  }
  if ((strobes & port_b::write_strobe) != 0) {
    levels |= port_b::busy;
  }
  return levels;
}

auto pia_listener_b(void* obj, uint8_t data) -> void {
  if (obj == nullptr) {
    return;
  }
  auto* card = static_cast<MouseCard_t*>(obj);

  const uint8_t diff = (card->port_b_shadow ^ data) & port_b::card_driven;
  if (diff == 0) {
    return;
  }
  card->port_b_shadow &= static_cast<uint8_t>(~port_b::card_driven);
  card->port_b_shadow |= (data & port_b::card_driven);

  if ((diff & port_b::write_strobe) != 0) {
    on_write_strobe(card, data);
  }
  if ((diff & port_b::read_strobe) != 0) {
    on_read_strobe(card, data);
  }
  pia_6821_set_port_b(&card->pia, card->port_b_shadow);

  if ((diff & port_b::bank_mask) != 0) {
    register_bank(card);
  }
}

// The tick is advanced at every access too, so a stepped program sees it.
auto mouse_io(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
              uint8_t val, uint32_t cycles) -> uint8_t {
  (void)pc;
  (void)cycles;
  if (instance == nullptr) {
    return 0;
  }
  auto* card = static_cast<MouseCard_t*>(instance);
  advance(card, card->host->GetCycles());
  // Only A0, A1 and DEVICE SELECT' reach the PIA (schematic zone C3), so its
  // four registers repeat through $C0n4-$C0nF.
  const auto rs = static_cast<uint8_t>(addr & io_register_mask);
  if (write != 0) {
    pia_6821_write(&card->pia, rs, val);
    return 0;
  }
  return pia_6821_read(&card->pia, rs);
}

// RES' reaches the PIA and the 6805 (schematic P1-31). A PIA reset zeroes every
// register (MC6821 data sheet, "Initialization"), so PB1-PB3 become inputs and
// the pull-downs select bank 0; the 6805's ports become inputs too (MC6805P
// data sheet 8.1), releasing IRQ'. PB6 is taken high because the 6502 firmware
// never reads before it has written. The defaults are the manual's (pp. 44,
// 48) and Tech Note Mouse #2's 60 Hz.
auto reset_card(MouseCard_t* card, uint64_t now) -> void {
  pia_6821_reset(&card->pia);
  pia_6821_set_listener_a(&card->pia, card, pia_listener_a);
  pia_6821_set_listener_b(&card->pia, card, pia_listener_b);
  card->port_a_shadow = 0;
  card->port_b_shadow = port_b::byte_ready;
  pia_6821_set_port_b(&card->pia, card->port_b_shadow);

  card->buffer.fill(0);
  card->pos = 0;
  card->out_len = 1;
  card->in_len = 0;
  card->reply_pos = 0;

  card->mode = 0;
  card->position_x = 0;
  card->position_y = 0;
  card->min_x = default_clamp_min;
  card->max_x = default_clamp_max;
  card->min_y = default_clamp_min;
  card->max_y = default_clamp_max;
  card->read_x = 0;
  card->read_y = 0;
  card->button_at_last_read = false;
  card->status = 0;
  card->pending = 0;
  set_irq(card, false);
  card->rate_50hz = false;
  card->tick_period = tick_period_60hz;
  card->next_tick = now + card->tick_period;

  register_bank(card);
}

// Better no card than a phantom one; the log names the member. Log itself is
// the one refusal nothing can report.
auto missing_host_member(const HostInterface_t* host) -> const char* {
  if (host->AssertIrq == nullptr) {
    return "AssertIrq";
  }
  if (host->RegisterIO == nullptr) {
    return "RegisterIO";
  }
  if (host->RegisterCxROM == nullptr) {
    return "RegisterCxROM";
  }
  if (host->GetCycles == nullptr) {
    return "GetCycles";
  }
  if (host->ScheduleEvent == nullptr) {
    return "ScheduleEvent";
  }
  return nullptr;
}

auto mouse_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->Log == nullptr) {
    return nullptr;
  }
  const char* missing = missing_host_member(host);
  if (missing != nullptr) {
    host->Log(nullptr, log_error,
              "Mouse Interface in slot %d: the host offers no %s\n", slot,
              missing);
    return nullptr;
  }
  if (slot < min_slot || slot > max_slot) {
    host->Log(nullptr, log_error,
              "Mouse Interface in slot %d: an expansion card sits in slots 1 "
              "to 7\n",
              slot);
    return nullptr;
  }

  auto card = std::unique_ptr<MouseCard_t>(new (std::nothrow) MouseCard_t());
  if (!card) {
    return nullptr;
  }
  card->host = host;
  card->slot = slot;
  // A schedule from init is dropped by the host, which stores the instance
  // only once init has returned; reset and the first think schedule.
  reset_card(card.get(), host->GetCycles());
  host->RegisterIO(slot, mouse_io, mouse_io, nullptr, nullptr);
  return card.release();
}

auto mouse_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* card = static_cast<MouseCard_t*>(instance);
  reset_card(card, card->host->GetCycles());
  card->host->ScheduleEvent(card, card->next_tick);
}

auto mouse_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  const std::unique_ptr<MouseCard_t> card(static_cast<MouseCard_t*>(instance));
  if (card->irq_asserted) {
    card->host->AssertIrq(card->slot, false);
  }
}

auto mouse_abi_think(void* instance, uint32_t elapsed_cycles) -> void {
  (void)elapsed_cycles;
  if (instance == nullptr) {
    return;
  }
  auto* card = static_cast<MouseCard_t*>(instance);
  advance(card, card->host->GetCycles());
}

auto mouse_abi_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  if (size == nullptr) {
    return peripheral_error;
  }
  constexpr size_t required = sizeof(MouseSaveState_t);
  if (buffer == nullptr) {
    *size = required;
    return peripheral_ok;
  }
  if (*size < required) {
    *size = required;
    return peripheral_error;
  }
  if (instance == nullptr) {
    return peripheral_error;
  }

  const auto* card = static_cast<const MouseCard_t*>(instance);
  const uint64_t now = card->host->GetCycles();
  MouseSaveState_t state{};
  state.version = MOUSE_STATE_VERSION;
  state.struct_size = static_cast<uint32_t>(required);
  state.position_x = static_cast<uint16_t>(card->position_x);
  state.position_y = static_cast<uint16_t>(card->position_y);
  state.min_x = static_cast<uint16_t>(card->min_x);
  state.max_x = static_cast<uint16_t>(card->max_x);
  state.min_y = static_cast<uint16_t>(card->min_y);
  state.max_y = static_cast<uint16_t>(card->max_y);
  state.tick_phase =
      static_cast<uint32_t>(card->next_tick > now ? card->next_tick - now : 0);
  state.read_x = static_cast<uint16_t>(card->read_x);
  state.read_y = static_cast<uint16_t>(card->read_y);
  state.parser_pos = card->pos;
  state.parser_out_len = card->out_len;
  state.pia_ora = card->pia.ora;
  state.pia_orb = card->pia.orb;
  state.pia_ddra = card->pia.ddra;
  state.pia_ddrb = card->pia.ddrb;
  state.pia_cra = card->pia.cra;
  state.pia_crb = card->pia.crb;
  state.pia_port_a_in = card->pia.port_a_in;
  state.pia_port_b_in = card->pia.port_b_in;
  state.rate_50hz = card->rate_50hz ? 1 : 0;
  state.pending = card->pending;
  state.irq_asserted = card->irq_asserted ? 1 : 0;
  state.parser_in_len = static_cast<uint8_t>(card->in_len);
  state.parser_reply_pos = static_cast<uint8_t>(card->reply_pos);
  state.pia_port_a_shadow = card->port_a_shadow;
  state.pia_port_b_shadow = card->port_b_shadow;
  state.mode = card->mode;
  state.status = card->status & frame_status_mask;
  state.button_at_last_read = card->button_at_last_read ? 1 : 0;
  state.button = card->button ? 1 : 0;
  std::copy(card->buffer.begin(), card->buffer.end(), state.buffer);
  std::memcpy(buffer, &state, required);

  *size = required;
  return peripheral_ok;
}

// Every refusal is of a frame no build of this card writes.
auto frame_refusal(const MouseSaveState_t& state) -> const char* {
  if (state.version != MOUSE_STATE_VERSION ||
      state.struct_size != sizeof(MouseSaveState_t)) {
    return "the header";
  }
  const std::array<uint32_t, 8> words = {
      state.position_x, state.position_y, state.min_x,  state.max_x,
      state.min_y,      state.max_y,      state.read_x, state.read_y,
  };
  for (uint32_t word : words) {
    if (word > frame_word_max) {
      return "a position or clamp above 16 bits";
    }
  }
  if (state.parser_pos >= frame_buffer_size || state.parser_out_len < 1 ||
      state.parser_out_len > frame_buffer_size) {
    return "the parser's cursor or length";
  }
  if (state.mode > mode::mask ||
      (state.pending & static_cast<uint8_t>(~status::interrupt_sources)) != 0) {
    return "the mode or pending bits";
  }
  if (state.rate_50hz > 1 || state.irq_asserted > 1 || state.button > 1 ||
      state.button_at_last_read > 1) {
    return "a flag above 1";
  }
  // Older frames hold a zero here.
  const uint32_t in_len = command_in_len(state.buffer[0]);
  if (state.parser_in_len != 0 && state.parser_in_len != in_len) {
    return "the reply length";
  }
  if (state.parser_reply_pos > in_len) {
    return "the reply cursor";
  }
  // A shadow whose replies do not answer its strobes parks the firmware: it
  // raises a strobe the shadow already holds, so no edge comes, and waits for
  // the reply (bank 6 $C49C and $C4B6, bank 3 $C40E and $C436).
  if ((state.pia_port_b_shadow & port_b::pia_inputs) !=
      reply_levels(state.pia_port_b_shadow)) {
    return "port B's replies against its strobes";
  }
  return nullptr;
}

// Any buffer of at least the frame's size loads, so an older file's 104-byte
// fixed region loads too, its host width at tick_phase bounded to one period.
// Sources with no line raised are a state the model cannot reach (the tick sets
// both, READMOUSE clears the bits, SERVEMOUSE both), so bits 1-3 go with the
// line. Bits 7 and 6 are never read: READMOUSE rebuilds them.
auto mouse_abi_load_state(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  if (instance == nullptr || buffer == nullptr ||
      size < sizeof(MouseSaveState_t)) {
    return peripheral_error;
  }
  MouseSaveState_t state{};
  std::memcpy(&state, buffer, sizeof(state));
  auto* card = static_cast<MouseCard_t*>(instance);
  const char* refusal = frame_refusal(state);
  if (refusal != nullptr) {
    card->host->Log(card, log_warn,
                    "Mouse Interface in slot %d: the save state's frame is "
                    "refused for %s; the card stays as it was\n",
                    card->slot, refusal);
    return peripheral_error;
  }

  // The shadow's bank and strobe bits are the PIA's pins as of the last ORB
  // write; a DDRB write after it moves the pins without a listener call, and
  // the pull-downs (R4-R7) hold an undriven line low, so a shadow that
  // disagrees with ORB & DDRB takes the pins. Byte 63 is the same shadow as the
  // chip saw it and is not read.
  const uint8_t pins = state.pia_orb & state.pia_ddrb & port_b::card_driven;
  uint8_t shadow = state.pia_port_b_shadow;
  if ((shadow & port_b::card_driven) != pins) {
    shadow = pins | reply_levels(pins);
    card->host->Log(
        card, log_warn,
        "Mouse Interface in slot %d: the save state's port B "
        "shadow $%02X disagrees with the PIA's pins; loading $%02X\n",
        card->slot, state.pia_port_b_shadow, shadow);
  }

  const uint64_t now = card->host->GetCycles();
  pia_6821_reset(&card->pia);
  pia_6821_set_listener_a(&card->pia, card, pia_listener_a);
  pia_6821_set_listener_b(&card->pia, card, pia_listener_b);
  card->pia.ora = state.pia_ora;
  card->pia.orb = state.pia_orb;
  card->pia.ddra = state.pia_ddra;
  card->pia.ddrb = state.pia_ddrb;
  card->pia.cra = state.pia_cra;
  card->pia.crb = state.pia_crb;
  card->pia.port_a_in = state.pia_port_a_in;
  card->port_a_shadow = state.pia_port_a_shadow;
  card->port_b_shadow = shadow;
  pia_6821_set_port_b(&card->pia, shadow);

  std::copy(state.buffer, state.buffer + card->buffer.size(),
            card->buffer.begin());
  card->pos = state.parser_pos;
  card->out_len = command_out_len(card->buffer.at(0));
  card->in_len = command_in_len(card->buffer.at(0));
  card->reply_pos = state.parser_reply_pos;

  card->mode = state.mode;
  card->position_x = static_cast<int16_t>(state.position_x);
  card->position_y = static_cast<int16_t>(state.position_y);
  card->min_x = static_cast<int16_t>(state.min_x);
  card->max_x = static_cast<int16_t>(state.max_x);
  card->min_y = static_cast<int16_t>(state.min_y);
  card->max_y = static_cast<int16_t>(state.max_y);
  card->read_x = static_cast<int16_t>(state.read_x);
  card->read_y = static_cast<int16_t>(state.read_y);
  card->button = state.button != 0;
  card->button_at_last_read = state.button_at_last_read != 0;

  card->status = state.status & frame_status_mask;
  if (state.irq_asserted == 0) {
    card->status &= static_cast<uint8_t>(~status::interrupt_sources);
  }
  card->pending = state.pending;
  card->rate_50hz = state.rate_50hz != 0;
  card->tick_period = card->rate_50hz ? tick_period_50hz : tick_period_60hz;
  card->next_tick =
      now + std::min<uint64_t>(state.tick_phase, card->tick_period);
  card->host->ScheduleEvent(card, card->next_tick);

  // The CPU's own restore may have cleared the slot's line under the latch,
  // so the level is driven whatever the latch held.
  card->irq_asserted = state.irq_asserted != 0;
  card->host->AssertIrq(card->slot, card->irq_asserted);

  register_bank(card);
  return peripheral_ok;
}

auto mouse_abi_command(void* instance, uint32_t cmd_id, const void* data,
                       size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  if (!peripheral_cmd_is_mine(cmd_id, PERIPHERAL_SUBSYSTEM_MOUSE)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }
  if (data == nullptr) {
    return peripheral_error;
  }

  auto* card = static_cast<MouseCard_t*>(instance);
  switch (static_cast<MouseCmd_t>(cmd_id)) {
    // Counts are added while the mouse is on and clamped; off, "any mouse
    // motion is ignored" (Tech Note Mouse #3). A count the clamp absorbs marks
    // nothing ("X or Y changed since last reading", manual p. 45). The
    // interrupt waits for the tick.
    case mouse_cmd_move: {
      if (size != sizeof(MouseMovePayload_t)) {
        return peripheral_error;
      }
      if ((card->mode & mode::tracking) == 0) {
        return peripheral_ok;
      }
      MouseMovePayload_t payload{};
      std::memcpy(&payload, data, sizeof(payload));
      const int16_t x = clamp_axis(
          static_cast<int32_t>(card->position_x) +
              std::max<int32_t>(INT16_MIN,
                                std::min<int32_t>(INT16_MAX, payload.dx)),
          card->min_x, card->max_x);
      const int16_t y = clamp_axis(
          static_cast<int32_t>(card->position_y) +
              std::max<int32_t>(INT16_MIN,
                                std::min<int32_t>(INT16_MAX, payload.dy)),
          card->min_y, card->max_y);
      if (x == card->position_x && y == card->position_y) {
        return peripheral_ok;
      }
      card->position_x = x;
      card->position_y = y;
      card->status |= status::moved;
      card->pending |= status::movement_interrupt;
      return peripheral_ok;
    }
    // Whether a release interrupts is unknown: the manual says "Enable
    // interrupts when button pressed" (p. 44) and "Interrupt caused by button
    // press" (p. 45); either edge is taken here. A second button is accepted
    // and ignored.
    case mouse_cmd_set_button: {
      if (size != sizeof(MouseButtonPayload_t)) {
        return peripheral_error;
      }
      MouseButtonPayload_t payload{};
      std::memcpy(&payload, data, sizeof(payload));
      if (payload.button != 0) {
        return peripheral_ok;
      }
      const bool down = payload.down != 0;
      if (card->button == down) {
        return peripheral_ok;
      }
      card->button = down;
      if ((card->mode & mode::tracking) != 0) {
        card->pending |= status::button_interrupt;
      }
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

auto mouse_abi_query(void* instance, uint32_t query_id, void* out,
                     size_t* out_size) -> PeripheralStatus_t {
  if (instance == nullptr || out_size == nullptr) {
    return peripheral_error;
  }
  if (!peripheral_cmd_is_mine(query_id, PERIPHERAL_SUBSYSTEM_MOUSE)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }
  size_t required = 0;
  if (query_id == mouse_query_is_active) {
    required = sizeof(uint8_t);
  } else if (query_id == mouse_query_position) {
    required = sizeof(MousePositionReport_t);
  } else {
    return peripheral_incompatible;
  }

  if (out == nullptr) {
    *out_size = required;
    return peripheral_ok;
  }
  if (*out_size < required) {
    *out_size = required;
    return peripheral_error;
  }
  *out_size = required;
  if (query_id == mouse_query_is_active) {
    *static_cast<uint8_t*>(out) = 1;
    return peripheral_ok;
  }

  const auto* card = static_cast<const MouseCard_t*>(instance);
  MousePositionReport_t report{};
  report.x = static_cast<int32_t>(card->position_x);
  report.y = static_cast<int32_t>(card->position_y);
  report.min_x = static_cast<int32_t>(card->min_x);
  report.max_x = static_cast<int32_t>(card->max_x);
  report.min_y = static_cast<int32_t>(card->min_y);
  report.max_y = static_cast<int32_t>(card->max_y);
  report.tracking = (card->mode & mode::tracking) != 0 ? 1 : 0;
  std::memcpy(out, &report, sizeof(report));
  return peripheral_ok;
}

}  // namespace

static Peripheral_t mouse_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.mouse",
    .name = "Mouse Interface",
    .description =
        "Apple AppleMouse II interface card (AppleMouse II User's Manual, "
        "1983)",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = 4,
    .init = mouse_abi_init,
    .reset = mouse_abi_reset,
    .shutdown = mouse_abi_shutdown,
    .think = mouse_abi_think,
    .on_vblank = nullptr,
    .save_state = mouse_abi_save_state,
    .load_state = mouse_abi_load_state,
    .command = mouse_abi_command,
    .query = mouse_abi_query,
};

// Peripheral registry requires non-const pointer.
auto mouse_get_descriptor() -> Peripheral_t* {
  return &mouse_peripheral;
}

PERIPHERAL_REGISTER(mouse_peripheral)
