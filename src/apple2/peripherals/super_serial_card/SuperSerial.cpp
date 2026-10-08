// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/super_serial_card/SuperSerial.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/chips/6551.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "apple2/peripherals/super_serial_card/SuperSerialRom.h"

namespace {

constexpr int min_slot = 1;
constexpr int max_slot = 7;
constexpr uint16_t io_register_mask = 0x0F;
// A3 = 1, A2 = 0 selects the 6551, A1-A0 the register (1981 manual p. 47).
constexpr uint16_t acia_select_mask = 0x0C;
constexpr uint16_t acia_selected = 0x08;
constexpr double millihertz_per_hertz = 1000.0;

// SW1 OFF OFF OFF ON ON ON ON, SW2 ON ON ON ON OFF ON OFF: communications
// mode, 9600 8N1, no LF after CR, interrupts to the slot. Neither manual
// states a factory setting; this is the 1981 manual's communications example
// (Table 3-1) at the card's top rate.
constexpr uint8_t default_switches_1 = 0x78;
constexpr uint8_t default_switches_2 = 0x2F;
constexpr uint8_t switch_bit_7 = 0x80;
// SW2-6 ON connects the ACIA's IRQ to the slot (1981 manual p. 47).
constexpr uint8_t switch_2_6 = 0x20;
constexpr uint16_t switches_1_offset = 1;
constexpr uint16_t switches_2_offset = 2;
// Two 74LS365 buffers (1981 manual p. 47; Table A-7 p. 54): $C0n1 holds
// SW1-1..4 in bits 7-4 and SW1-5..6 in bits 1-0; $C0n2 holds SW2-1 in bit 7,
// SW2-2 in bit 5, SW2-3..5 in bits 3-1 and CTS in bit 0. A closed switch and
// an asserted CTS read 0. The undriven bits read 1 through the data-bus
// pull-ups (p. 48; inferred, the firmware masks them off).
constexpr uint8_t switches_1_undriven = 0x0C;
constexpr uint8_t switches_2_undriven = 0x50;
constexpr uint8_t cts_deasserted = 0x01;

static_assert(sizeof(SuperSerialSaveState_t) == 56,
              "the serial card's state frame is part of the plugin ABI");
static_assert(offsetof(SuperSerialSaveState_t, version) == 0,
              "the frame header is version then size");
static_assert(offsetof(SuperSerialSaveState_t, struct_size) == 4,
              "the frame header is version then size");
static_assert(offsetof(SuperSerialSaveState_t, rx_count) == 8,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(SuperSerialSaveState_t, control_byte) == 12,
              "the control register sits where every frame written has it");
static_assert(offsetof(SuperSerialSaveState_t, command_byte) == 13,
              "the command register sits where every frame written has it");
static_assert(offsetof(SuperSerialSaveState_t, is_irq_pending) == 14,
              "the IRQ latch sits where every frame written has it");
static_assert(offsetof(SuperSerialSaveState_t, is_rx_irq_enabled) == 15,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(SuperSerialSaveState_t, is_tx_irq_enabled) == 16,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(SuperSerialSaveState_t, was_tx_written) == 17,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(SuperSerialSaveState_t, rx_buffer) == 18,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(SuperSerialSaveState_t, status_latches) == 27,
              "the latch byte is the one byte older frames held in reserve");
static_assert(offsetof(SuperSerialSaveState_t, config) == 28,
              "the dead fields keep their place so every frame written loads");
static_assert(sizeof(SuperSerialSaveState_t::config) == 24,
              "the dead fields keep their size so every frame written loads");
static_assert(
    offsetof(SuperSerialSaveState_t, receive_data) == 52,
    "the data bytes take the four bytes older frames held in reserve");
static_assert(
    offsetof(SuperSerialSaveState_t, transmit_data) == 53,
    "the data bytes take the four bytes older frames held in reserve");
static_assert(
    offsetof(SuperSerialSaveState_t, shift_data) == 54,
    "the data bytes take the four bytes older frames held in reserve");
static_assert(offsetof(SuperSerialSaveState_t, reserved1) == 55,
              "one reserved byte remains");

struct SuperSerialCard_t {
  HostInterface_t* host = nullptr;
  void* sink = nullptr;
  int slot = 0;
  SuperSerialSwitches_t switches{default_switches_1, default_switches_2};
  Acia6551_t acia;
  AciaLine_t line_sent;
  bool slot_irq = false;
};

auto switch_reads_0(uint8_t image, int number) -> bool {
  return (image & static_cast<uint8_t>(1U << (number - 1))) != 0;
}

auto switches_1_byte(const SuperSerialCard_t* card) -> uint8_t {
  const uint8_t image = card->switches.sw1;
  uint8_t byte = switches_1_undriven;
  byte |= switch_reads_0(image, 1) ? 0 : 0x80;
  byte |= switch_reads_0(image, 2) ? 0 : 0x40;
  byte |= switch_reads_0(image, 3) ? 0 : 0x20;
  byte |= switch_reads_0(image, 4) ? 0 : 0x10;
  byte |= switch_reads_0(image, 5) ? 0 : 0x02;
  byte |= switch_reads_0(image, 6) ? 0 : 0x01;
  return byte;
}

auto switches_2_byte(const SuperSerialCard_t* card) -> uint8_t {
  const uint8_t image = card->switches.sw2;
  uint8_t byte = switches_2_undriven;
  byte |= switch_reads_0(image, 1) ? 0 : 0x80;
  byte |= switch_reads_0(image, 2) ? 0 : 0x20;
  byte |= switch_reads_0(image, 3) ? 0 : 0x08;
  byte |= switch_reads_0(image, 4) ? 0 : 0x04;
  byte |= switch_reads_0(image, 5) ? 0 : 0x02;
  byte |= (card->acia.lines & acia_line::cts) != 0 ? 0 : cts_deasserted;
  return byte;
}

// A host with nothing to say reads as no cable: the card's 15 kOhm pull-ups
// assert all three inputs (1981 manual p. 48).
auto host_lines(SuperSerialCard_t* card) -> uint8_t {
  uint8_t mask = 0;
  if (card->host->SinkGetLines(card->sink, &mask)) {
    return mask & acia_line::all_asserted;
  }
  return acia_line::all_asserted;
}

auto send_bytes(SuperSerialCard_t* card, uint64_t now) -> void {
  uint8_t byte = 0;
  while (acia_step(&card->acia, now, &byte)) {
    card->host->SinkWrite(card->sink, byte);
  }
}

// The far end is taken to honour RTS: a byte leaves the host only once the
// receiver can hold it, so a slow reader sees bytes wait rather than an
// overrun, and a disabled receiver's bytes wait instead of being lost as on
// hardware.
auto pull_byte(SuperSerialCard_t* card, uint64_t now) -> void {
  if (!acia_rx_ready(&card->acia)) {
    return;
  }
  uint8_t byte = 0;
  if (card->host->SinkRead(card->sink, &byte)) {
    acia_rx_start(&card->acia, byte, 0, now);
  }
}

auto send_line(SuperSerialCard_t* card) -> void {
  acia_line_view(&card->acia, &card->line_sent);
  PeripheralSerialLine_t line{};
  line.baud = card->line_sent.baud;
  line.data_bits = card->line_sent.data_bits;
  line.parity = card->line_sent.parity;
  line.stop_half_bits = card->line_sent.stop_half_bits;
  line.dtr = card->line_sent.dtr;
  line.rts = card->line_sent.rts;
  line.brk = card->line_sent.brk;
  card->host->SinkSetLine(card->sink, &line);
}

auto follow_line(SuperSerialCard_t* card) -> void {
  AciaLine_t line;
  acia_line_view(&card->acia, &line);
  const AciaLine_t& sent = card->line_sent;
  if (line.baud == sent.baud && line.data_bits == sent.data_bits &&
      line.parity == sent.parity &&
      line.stop_half_bits == sent.stop_half_bits && line.dtr == sent.dtr &&
      line.rts == sent.rts && line.brk == sent.brk) {
    return;
  }
  send_line(card);
}

auto follow_irq(SuperSerialCard_t* card) -> void {
  const bool level =
      acia_irq(&card->acia) && (card->switches.sw2 & switch_2_6) != 0;
  if (level == card->slot_irq) {
    return;
  }
  card->slot_irq = level;
  card->host->AssertIrq(card->slot, level);
}

auto sync(SuperSerialCard_t* card) -> uint64_t {
  const uint64_t now = card->host->GetCycles();
  acia_set_lines(&card->acia, host_lines(card), now);
  send_bytes(card, now);
  return now;
}

// The wake lands TDRE, RDRF and the receiver's free point within one
// instruction of their cycle; an idle chip's 0 cancels it.
auto settle(SuperSerialCard_t* card, uint64_t now) -> void {
  send_bytes(card, now);
  pull_byte(card, now);
  follow_line(card);
  follow_irq(card);
  card->host->ScheduleEvent(card, acia_next_event(&card->acia));
}

auto super_serial_io_read(void* instance, uint16_t program_counter,
                          uint16_t memory_address, uint8_t is_write,
                          uint8_t data_value, uint32_t executed_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)data_value;
  (void)is_write;
  if (instance == nullptr) {
    return 0;
  }
  auto* card = static_cast<SuperSerialCard_t*>(instance);
  const uint64_t now = sync(card);
  const uint16_t offset = memory_address & io_register_mask;
  uint8_t value = 0;
  if ((offset & acia_select_mask) == acia_selected) {
    value = acia_read(&card->acia, static_cast<uint8_t>(offset), now);
  } else if (offset == switches_1_offset) {
    value = switches_1_byte(card);
  } else if (offset == switches_2_offset) {
    value = switches_2_byte(card);
  } else {
    // Floating bus or $FF from the pull-ups depends on when the card's LS245
    // is enabled, which the legible part of the schematic does not settle
    // (1981 manual p. 48).
    value = card->host->ReadFloatingBus(executed_cycles);
  }
  settle(card, now);
  return value;
}

auto super_serial_io_write(void* instance, uint16_t program_counter,
                           uint16_t memory_address, uint8_t is_write,
                           uint8_t data_value, uint32_t executed_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)executed_cycles;
  (void)is_write;
  if (instance == nullptr) {
    return 0;
  }
  auto* card = static_cast<SuperSerialCard_t*>(instance);
  const uint64_t now = sync(card);
  const uint16_t offset = memory_address & io_register_mask;
  if ((offset & acia_select_mask) == acia_selected) {
    uint8_t byte = 0;
    if (acia_write(&card->acia, static_cast<uint8_t>(offset), data_value, now,
                   &byte)) {
      card->host->SinkWrite(card->sink, byte);
    }
  }
  settle(card, now);
  return 0;
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
  if (host->RegisterExpansionROM == nullptr) {
    return "RegisterExpansionROM";
  }
  if (host->GetCycles == nullptr) {
    return "GetCycles";
  }
  if (host->GetClockHz == nullptr) {
    return "GetClockHz";
  }
  if (host->ReadFloatingBus == nullptr) {
    return "ReadFloatingBus";
  }
  if (host->SinkOpen == nullptr) {
    return "SinkOpen";
  }
  if (host->SinkWrite == nullptr) {
    return "SinkWrite";
  }
  if (host->SinkClose == nullptr) {
    return "SinkClose";
  }
  if (host->SinkRead == nullptr) {
    return "SinkRead";
  }
  if (host->SinkSetLine == nullptr) {
    return "SinkSetLine";
  }
  if (host->SinkGetLines == nullptr) {
    return "SinkGetLines";
  }
  if (host->ScheduleEvent == nullptr) {
    return "ScheduleEvent";
  }
  return nullptr;
}

auto super_serial_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->Log == nullptr) {
    return nullptr;
  }
  const char* missing = missing_host_member(host);
  if (missing != nullptr) {
    host->Log(nullptr, log_error,
              "Super Serial Card in slot %d: the host offers no %s\n", slot,
              missing);
    return nullptr;
  }
  if (slot < min_slot || slot > max_slot) {
    host->Log(nullptr, log_error,
              "Super Serial Card in slot %d: an expansion card sits in slots "
              "1 to 7\n",
              slot);
    return nullptr;
  }

  auto card = std::unique_ptr<SuperSerialCard_t>(new (std::nothrow)
                                                     SuperSerialCard_t());
  if (!card) {
    return nullptr;
  }
  card->host = host;
  card->slot = slot;
  card->sink = host->SinkOpen(card.get(), slot, peripheral_sink_serial);
  if (card->sink == nullptr) {
    host->Log(nullptr, log_error,
              "Super Serial Card in slot %d: the host has no serial line for "
              "the slot\n",
              slot);
    return nullptr;
  }

  // The ACIA runs from the card's own 1.8432 MHz crystal (1981 manual p. 45),
  // so a character time in 6502 cycles varies with the machine's clock.
  acia_set_clock_mhz(&card->acia,
                     static_cast<uint64_t>(std::llround(host->GetClockHz() *
                                                        millihertz_per_hertz)));
  const uint64_t now = host->GetCycles();
  acia_reset(&card->acia, now);
  acia_set_lines(&card->acia, host_lines(card.get()), now);

  host->RegisterCxROM(slot,
                      super_serial_rom.data() + super_serial_rom_slot_page);
  host->RegisterExpansionROM(slot, super_serial_rom.data());
  host->RegisterIO(slot, super_serial_io_read, super_serial_io_write, nullptr,
                   nullptr);
  send_line(card.get());

  return card.release();
}

// RESET reaches the ACIA (inferred; the schematic's trace is not legible,
// 1981 manual p. 100), so Ctrl-Reset drops DTR and RTS. The switches survive.
auto super_serial_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* card = static_cast<SuperSerialCard_t*>(instance);
  const uint64_t now = card->host->GetCycles();
  acia_reset(&card->acia, now);
  acia_set_lines(&card->acia, host_lines(card), now);
  send_line(card);
  follow_irq(card);
  card->host->ScheduleEvent(card, 0);
}

auto super_serial_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<SuperSerialCard_t> card(
      static_cast<SuperSerialCard_t*>(instance));
  if (card->slot_irq) {
    card->host->AssertIrq(card->slot, false);
  }
  card->host->SinkClose(card->sink);
}

auto super_serial_abi_think(void* instance, uint32_t elapsed_cycles) -> void {
  (void)elapsed_cycles;
  if (instance == nullptr) {
    return;
  }
  auto* card = static_cast<SuperSerialCard_t*>(instance);
  const uint64_t now = sync(card);
  settle(card, now);
}

auto super_serial_abi_command(void* instance, uint32_t command_id,
                              const void* payload, size_t payload_size)
    -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  if (!peripheral_cmd_is_mine(command_id, PERIPHERAL_SUBSYSTEM_SERIAL)) {
    return peripheral_incompatible;
  }
  if (command_id != SUPER_SERIAL_CMD_SET_SWITCHES) {
    return peripheral_incompatible;
  }
  if (payload == nullptr || payload_size != sizeof(SuperSerialSwitches_t)) {
    return peripheral_error;
  }
  SuperSerialSwitches_t switches{};
  std::memcpy(&switches, payload, sizeof(switches));
  if ((switches.sw1 & switch_bit_7) != 0 ||
      (switches.sw2 & switch_bit_7) != 0) {
    return peripheral_error;
  }
  auto* card = static_cast<SuperSerialCard_t*>(instance);
  card->switches = switches;
  follow_irq(card);
  return peripheral_ok;
}

auto super_serial_abi_query(void* instance, uint32_t query_id, void* output,
                            size_t* output_size) -> PeripheralStatus_t {
  (void)instance;
  (void)query_id;
  (void)output;
  if (output_size == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

auto super_serial_abi_save_state(void* instance, void* state_buffer,
                                 size_t* buffer_size) -> PeripheralStatus_t {
  if (buffer_size == nullptr) {
    return peripheral_error;
  }
  constexpr size_t required_size = sizeof(SuperSerialSaveState_t);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  const auto* card = static_cast<const SuperSerialCard_t*>(instance);
  SuperSerialSaveState_t state{};
  state.version = SUPER_SERIAL_STATE_VERSION;
  state.struct_size = static_cast<uint32_t>(required_size);
  state.control_byte = card->acia.control;
  state.command_byte = card->acia.command;
  state.is_irq_pending = card->acia.irq ? 1 : 0;
  state.status_latches = card->acia.status_latches;
  state.receive_data = card->acia.receive_data;
  state.transmit_data = card->acia.transmit_data;
  state.shift_data = card->acia.shift_data;
  std::memcpy(state_buffer, &state, required_size);

  *buffer_size = required_size;
  return peripheral_ok;
}

// The slot buffer is sized for the biggest card, so the frame's own
// struct_size says how much to read. The switches are not in the frame and
// keep what the frontend set.
auto super_serial_abi_load_state(void* instance, const void* state_buffer,
                                 size_t buffer_size) -> PeripheralStatus_t {
  constexpr size_t header_size = offsetof(SuperSerialSaveState_t, rx_count);
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size < header_size) {
    return peripheral_error;
  }
  SuperSerialSaveState_t state{};
  std::memcpy(&state, state_buffer, header_size);
  if (state.struct_size != sizeof(state) || buffer_size < state.struct_size) {
    return peripheral_error;
  }
  if (state.version != SUPER_SERIAL_STATE_VERSION) {
    return peripheral_error;
  }
  std::memcpy(&state, state_buffer, state.struct_size);
  if ((state.status_latches & acia_latch::reserved) != 0) {
    return peripheral_error;
  }

  auto* card = static_cast<SuperSerialCard_t*>(instance);
  const uint64_t now = card->host->GetCycles();
  acia_reset(&card->acia, now);
  card->acia.control = state.control_byte;
  card->acia.command = state.command_byte;
  card->acia.irq = state.is_irq_pending != 0;
  card->acia.status_latches = state.status_latches;
  card->acia.receive_data = state.receive_data;
  card->acia.transmit_data = state.transmit_data;
  card->acia.shift_data = state.shift_data;
  acia_restart(&card->acia, now);
  acia_set_lines(&card->acia, host_lines(card), now);
  send_line(card);
  follow_irq(card);
  card->host->ScheduleEvent(card, acia_next_event(&card->acia));
  return peripheral_ok;
}

}  // namespace

static const Peripheral_t super_serial_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.ssc",
    .name = "Super Serial Card",
    .description =
        "Apple II Super Serial Card (Installation and Operating Manual, 1981)",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = 2,
    .init = super_serial_abi_init,
    .reset = super_serial_abi_reset,
    .shutdown = super_serial_abi_shutdown,
    .think = super_serial_abi_think,
    .on_vblank = nullptr,
    .save_state = super_serial_abi_save_state,
    .load_state = super_serial_abi_load_state,
    .command = super_serial_abi_command,
    .query = super_serial_abi_query,
};

// peripheral_register and ActivePeripheral_t::api take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
auto super_serial_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&super_serial_peripheral);
}

PERIPHERAL_REGISTER(super_serial_peripheral)
