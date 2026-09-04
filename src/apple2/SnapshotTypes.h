// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "apple2/Apple2Types.h"
#include "apple2/chips/6522.h"
#include "apple2/chips/AY8910.h"
#include "apple2/chips/SSI263.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

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

struct KeyboardSaveState_t {
  uint8_t current_latch = 0;
  uint8_t strobe = 0;
  uint8_t rocker_switch = 0;
  uint8_t shift_key = 0;
  uint8_t ctrl_key = 0;
  uint8_t open_apple = 0;
  uint8_t closed_apple = 0;
  uint8_t caps_lock = 1;
  uint32_t keys_down_count = 0;
  uint8_t alternate_layout = 0;

  uint32_t repeat_key = NO_REPEAT_KEY;
  uint32_t repeat_scancode = 0;
  uint32_t repeat_delay_cycles = 0;
  uint8_t repeating = 0;
};

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

struct MbUnit_t {
  Sy6522_t regs_sy6522;
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
};
using APPLEWIN_SNAPSHOT = ApplewinSnapshot_t;
