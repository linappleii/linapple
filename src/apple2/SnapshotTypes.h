// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#include "apple2/Apple2Types.h"
#include "apple2/chips/AY8910.h"
#include "apple2/chips/SSI263.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/LinAppleCore.h"

constexpr uint32_t NO_REPEAT_KEY = 0xFFFFFFFF;

constexpr uint32_t BYTE3_SHIFT = 24;
constexpr uint32_t BYTE2_SHIFT = 16;
constexpr uint32_t BYTE1_SHIFT = 8;

constexpr auto make_version(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
    -> uint32_t {
  return ((a) << BYTE3_SHIFT) | ((b) << BYTE2_SHIFT) | ((c) << BYTE1_SHIFT) |
         (d);
}
constexpr uint32_t aw_ss_tag =
    (('S' << BYTE3_SHIFT) | ('S' << BYTE2_SHIFT) | ('W' << BYTE1_SHIFT) | 'A');

#define MAKE_VERSION(a, b, c, d) make_version(a, b, c, d)
#define AW_SS_TAG aw_ss_tag

struct SsFileHdr_t {
  uint32_t tag;
  uint32_t version;
  uint32_t checksum;
};
using SS_FILE_HDR = SsFileHdr_t;

struct SsUnitHdr_t {
  uint32_t length;
  uint32_t version;
};
using SS_UNIT_HDR = SsUnitHdr_t;

struct SsCpu6502_t {
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t p;
  uint8_t s;
  uint16_t pc;
  uint64_t cumulative_cycles;
};
using SS_CPU_6502 = SsCpu6502_t;

struct SsIoComms_t {
  uint32_t baud_rate;
  uint8_t byte_size;
  uint8_t command_byte;
  uint32_t comm_inactivity;
  uint8_t control_byte;
  uint8_t parity;
  uint8_t recv_buffer[SUPER_SERIAL_FIFO_SIZE];
  uint32_t recv_bytes;
  uint8_t stop_bits;
};
using SS_IO_Comms = SsIoComms_t;

struct SsIoJoystick_t {
  uint64_t joy_cntr_reset_cycle;
};
using SS_IO_Joystick = SsIoJoystick_t;

struct SsIoVideo_t {
  uint8_t alt_char_set;
  uint32_t vid_mode;
};
using SS_IO_Video = SsIoVideo_t;

constexpr uint32_t MEM_64K = 65536;
constexpr uint32_t mem_main_size = MEM_64K;
constexpr uint32_t mem_aux_size = MEM_64K;
constexpr uint32_t nMemMainSize = mem_main_size;
constexpr uint32_t nMemAuxSize = mem_aux_size;

struct SsBaseMemory_t {
  uint32_t mem_mode;
  uint8_t last_write_ram;
  uint8_t mem_main[mem_main_size];
  uint8_t mem_aux[mem_aux_size];
};
using SS_BaseMemory = SsBaseMemory_t;

struct SsApple2Unit_t {
  SsUnitHdr_t unit_hdr;
  SsCpu6502_t cpu_6502;
  SsIoComms_t comms;
  SsIoJoystick_t joystick;
  KeyboardSaveState_t keyboard;
  SsIoSpeaker_t speaker;
  SsIoVideo_t video;
  SsBaseMemory_t memory;
};
using SS_APPLE2_Unit = SsApple2Unit_t;

struct SsAwCfg_t {
  uint32_t computer_emulation;
  uint8_t custom_speed;
  uint32_t emulation_speed;
  uint8_t enhanced_disk_speed;
  uint32_t joystick_type[2];
  uint8_t mockingboard_enabled;
  uint32_t monochrome_color;
  uint32_t serial_port;
  uint32_t sound_type;
  uint32_t video_type;
};
using SS_AW_CFG = SsAwCfg_t;

struct SsAwPrefs_t {
  char starting_dir[path_max_len];
  uint32_t window_x_pos;
  uint32_t window_y_pos;
};
using SS_AW_PREFS = SsAwPrefs_t;

struct SsApplewinConfig_t {
  SsUnitHdr_t unit_hdr;
  uint32_t applewin_version;
  SsAwPrefs_t prefs;
  SsAwCfg_t cfg;
};
using SS_APPLEWIN_CONFIG = SsApplewinConfig_t;

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
using SS_CARD_HDR = SsCardHdr_t;

enum SsCardType_t {
  ct_empty = 0,
  ct_disk2,
  ct_ssc,
  ct_mockingboard,
  ct_generic_printer,
  ct_generic_hdd,
  ct_generic_clock,
  ct_mouse_interface,
};
using SS_CARDTYPE = SsCardType_t;
constexpr SsCardType_t CT_Empty = ct_empty;
constexpr SsCardType_t CT_Disk2 = ct_disk2;
constexpr SsCardType_t CT_SSC = ct_ssc;
constexpr SsCardType_t CT_Mockingboard = ct_mockingboard;
constexpr SsCardType_t CT_GenericPrinter = ct_generic_printer;
constexpr SsCardType_t CT_GenericHDD = ct_generic_hdd;
constexpr SsCardType_t CT_GenericClock = ct_generic_clock;
constexpr SsCardType_t CT_MouseInterface = ct_mouse_interface;

struct SsCardEmpty_t {
  SsCardHdr_t hdr;
};
using SS_CARD_EMPTY = SsCardEmpty_t;

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
  uint8_t regs_ay8910[AY8910_NUM_REGISTERS];
  Ssi263A_t regs_ssi263;
  uint8_t ay_current_register;
  bool timer1_irq_pending;
  bool timer2_irq_pending;
  bool speech_irq_pending;
};
using MB_Unit = MbUnit_t;

constexpr uint32_t mb_units_per_card = 2;

struct SsCardMockingboard_t {
  SsCardHdr_t hdr;
  MbUnit_t unit[mb_units_per_card];
};
using SS_CARD_MOCKINGBOARD = SsCardMockingboard_t;

// The fixed body above offers each slot the region AppleWin's format had for
// it: 16 bytes for slots 1, 3 and 7, 32 for slot 2, 104 for 4 and 5. No card
// frame this emulator writes fits any of them (the smallest, the clock's, is
// 32 bytes in a 16-byte slot; the Mockingboard's is 232 in 104), so a card's
// state travels in this trailer instead: one length-prefixed blob per slot,
// filled by the card's own save_state and handed back whole to load_state.
// The capacity holds every card frame written today except the SmartPort
// harddisk's (2160 bytes), which is not carried. Slot 6 has no
// entry on purpose: the Disk II has a real save_state (13897 bytes), and a
// generic probe of every slot would start persisting disk state that the
// snapshot layer deliberately leaves to the mounted image.
constexpr uint32_t snapshot_slot_state_capacity = 256;

struct SsSlotState_t {
  uint32_t length;
  // Brings the entry to an 8-byte multiple so the trailer, and with it the
  // file, ends where the last entry does rather than in alignment padding.
  uint32_t reserved;
  uint8_t data[snapshot_slot_state_capacity];
};

// Slots 1 through 5 and 7 at index slot - 1. Slot 6's entry stays empty.
constexpr uint32_t snapshot_trailer_slots = 7;

struct SsSlotTrailer_t {
  SsUnitHdr_t unit_hdr;
  SsSlotState_t slots[snapshot_trailer_slots];
};

struct ApplewinSnapshot_t {
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
using APPLEWIN_SNAPSHOT = ApplewinSnapshot_t;

// A file is the fixed body alone or the fixed body followed by the slot
// trailer, under one header version: the reader tells the two layouts apart
// by the file's length. Readers from before the trailer read their fixed
// body's bytes and stop, so they still load a file that carries one.
constexpr uint32_t snapshot_version = make_version(1, 0, 0, 1);
constexpr size_t snapshot_size_fixed_body =
    offsetof(ApplewinSnapshot_t, slot_trailer);

// Both sizes are what files on disk already are; a change to either is a
// format change, not a silent shift.
static_assert(snapshot_size_fixed_body == 132344,
              "the fixed-body snapshot layout is a wire format frozen on disk");
static_assert(sizeof(ApplewinSnapshot_t) ==
                  snapshot_size_fixed_body + sizeof(SsSlotTrailer_t),
              "the trailer must follow the fixed body with no padding");
static_assert(snapshot_slot_state_capacity >= sizeof(SsCardMockingboard_t),
              "a slot must hold at least the largest fixed-body card region");
