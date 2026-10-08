// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "apple2/Apple2Types.h"
#include "apple2/chips/AY8910.h"
#include "apple2/chips/SSI263.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "core/LinAppleCore.h"

constexpr uint32_t byte3_shift = 24;
constexpr uint32_t byte2_shift = 16;
constexpr uint32_t byte1_shift = 8;

constexpr auto make_version(uint32_t a, uint32_t b, uint32_t c,
                            uint32_t d) noexcept -> uint32_t {
  return (a << byte3_shift) | (b << byte2_shift) | (c << byte1_shift) | d;
}

constexpr uint32_t snapshot_file_tag = make_version('S', 'S', 'W', 'A');
constexpr uint32_t aw_ss_tag = snapshot_file_tag;

struct SsFileHdr_t {
  uint32_t tag;
  uint32_t version;
  uint32_t checksum;
};

struct SsUnitHdr_t {
  uint32_t length;
  uint32_t version;
};

struct SsCpu6502_t {
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t p;
  uint8_t s;
  uint16_t pc;
  uint64_t cumulative_cycles;
};

// The 32 bytes an AppleWin .aws spends on the serial card: layout only, kept
// as zeros, since the card's own frame rides the slot trailer.
struct SsIoComms_t {
  uint32_t baud_rate;
  uint8_t byte_size;
  uint8_t command_byte;
  uint32_t comm_inactivity;
  uint8_t control_byte;
  uint8_t parity;
  uint8_t recv_buffer[9];
  uint32_t recv_bytes;
  uint8_t stop_bits;
};
static_assert(sizeof(SsIoComms_t) == 32,
              "SsIoComms_t is an .aws wire format and must stay 32 bytes");

// Eight zero bytes that nothing reads. They keep the fixed body at its length;
// the game port lives in slot 0, which has no entry in the trailer and no
// frame that fits here, so its state has no home in this format.
struct SsIoJoystick_t {
  uint64_t joy_cntr_reset_cycle;
};

struct SsIoVideo_t {
  uint8_t alt_char_set;
  uint32_t vid_mode;
};

// Opaque: the layout is the card's own ABI; a build without the card writes
// zeros here.
struct SsKeyboardRegion_t {
  uint8_t bytes[552];
};

constexpr uint32_t mem_main_size = 65536;
constexpr uint32_t mem_aux_size = 65536;

struct SsBaseMemory_t {
  uint32_t mem_mode;
  uint8_t last_write_ram;
  uint8_t mem_main[mem_main_size];
  uint8_t mem_aux[mem_aux_size];
};

struct SsApple2Unit_t {
  SsUnitHdr_t unit_hdr;
  SsCpu6502_t cpu_6502;
  SsIoComms_t comms;
  SsIoJoystick_t joystick;
  SsKeyboardRegion_t keyboard;
  SsIoSpeaker_t speaker;
  SsIoVideo_t video;
  SsBaseMemory_t memory;
};
static_assert(offsetof(SsApple2Unit_t, keyboard) == 64,
              "the keyboard region is where every .aws written has it");

constexpr uint32_t max_peripheral_name = 32;

struct SsPeripheralInfo_t {
  char name[max_peripheral_name];
  uint32_t version;
};
using SS_PERIPHERAL_INFO = SsPeripheralInfo_t;

struct SsPeripheralManifest_t {
  SsUnitHdr_t unit_hdr;
  SsPeripheralInfo_t peripherals[NUM_SLOTS];
};
using SS_PERIPHERAL_MANIFEST = SsPeripheralManifest_t;

struct SsCardHdr_t {
  SsUnitHdr_t unit_hdr;
  uint32_t type;
  uint32_t slot;
};

struct SsCardEmpty_t {
  SsCardHdr_t hdr;
};

// The eighteen bytes an AppleWin .aws file spends on one VIA. This is a wire
// format frozen at the shape it had when it was written, not a view of the
// live 6522 model, which is free to grow state the format never carried.
struct SsVia6522Regs_t {
  uint8_t orb;
  uint8_t ora;
  uint8_t ddrb;
  uint8_t ddra;
  uint16_t timer1_counter;
  uint16_t timer1_latch;
  uint16_t timer2_counter;
  uint16_t timer2_latch;
  uint8_t serial_shift;
  uint8_t acr;
  uint8_t pcr;
  uint8_t ifr;
  uint8_t ier;
  uint8_t ora_no_handshake;
};
static_assert(sizeof(SsVia6522Regs_t) == 18,
              "SsVia6522Regs_t is an .aws wire format and must stay 18 bytes");

struct MbUnit_t {
  SsVia6522Regs_t regs_sy6522;
  uint8_t regs_ay8910[ay8910_num_registers];
  Ssi263A regs_ssi263;
  uint8_t ay_current_register;
  bool timer1_irq_pending;
  bool timer2_irq_pending;
  bool speech_irq_pending;
};

constexpr uint32_t mb_units_per_card = 2;

struct SsCardMockingboard_t {
  SsCardHdr_t hdr;
  MbUnit_t unit[mb_units_per_card];
};

// Variable-length peripheral states appended after fixed body (slots 1-5, 7).
constexpr uint32_t snapshot_slot_state_capacity = 256;

struct SsSlotState_t {
  uint32_t length;
  // Pad entry to 8-byte boundary.
  uint32_t reserved;
  uint8_t data[snapshot_slot_state_capacity];
};

// Slots 1 through 5 and 7 at index slot - 1. Slot 6's entry stays empty.
constexpr uint32_t snapshot_trailer_slots = 7;

struct SsSlotTrailer_t {
  SsUnitHdr_t unit_hdr;
  SsSlotState_t slots[snapshot_trailer_slots];
};

struct Snapshot_t {
  SsFileHdr_t hdr;
  SsApple2Unit_t apple2_unit;
  SsPeripheralManifest_t manifest;
  SsCardEmpty_t empty1;
  SsCardEmpty_t empty2;
  SsCardEmpty_t empty3;
  SsCardMockingboard_t mockingboard1;
  SsCardMockingboard_t mockingboard2;
  SsCardEmpty_t empty6;
  SsCardEmpty_t empty7;
  SsSlotTrailer_t slot_trailer;
};
using Snapshot = Snapshot_t;
using ApplewinSnapshot_t = Snapshot_t;
using ApplewinSnapshot = Snapshot_t;
using SsFileHdr = SsFileHdr_t;
using SsUnitHdr = SsUnitHdr_t;
using SsCpu6502 = SsCpu6502_t;
using SsIoComms = SsIoComms_t;
using SsIoJoystick = SsIoJoystick_t;
using SsIoVideo = SsIoVideo_t;
using SsKeyboardRegion = SsKeyboardRegion_t;
using SsBaseMemory = SsBaseMemory_t;
using SsApple2Unit = SsApple2Unit_t;
using SsPeripheralInfo = SsPeripheralInfo_t;
using SsPeripheralManifest = SsPeripheralManifest_t;
using SsCardHdr = SsCardHdr_t;
using SsCardEmpty = SsCardEmpty_t;
using SsVia6522Regs = SsVia6522Regs_t;
using MbUnit = MbUnit_t;
using SsCardMockingboard = SsCardMockingboard_t;
using SsSlotState = SsSlotState_t;
using SsSlotTrailer = SsSlotTrailer_t;

// Differentiate snapshot format with slot trailer by file size.
constexpr uint32_t snapshot_version = make_version(1, 0, 0, 1);
constexpr size_t snapshot_size_fixed_body = offsetof(Snapshot_t, slot_trailer);

static_assert(std::is_standard_layout<Snapshot_t>::value,
              "Snapshot_t must be standard layout");
static_assert(snapshot_size_fixed_body == 132344,
              "the fixed-body snapshot layout is a wire format frozen on disk");
static_assert(sizeof(Snapshot_t) ==
                  snapshot_size_fixed_body + sizeof(SsSlotTrailer_t),
              "the trailer must follow the fixed body with no padding");
static_assert(snapshot_slot_state_capacity >= sizeof(SsCardMockingboard_t),
              "a slot must hold at least the largest fixed-body card region");
