// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "apple2/chips/6522.h"
#include "apple2/chips/SSI263.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/Common.h"

#define MAKE_VERSION(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define AW_SS_TAG (('S' << 24) | ('S' << 16) | ('W' << 8) | 'A')

typedef struct {
  uint32_t dwTag;
  uint32_t dwVersion;
  uint32_t dwChecksum;
} SS_FILE_HDR;

typedef struct {
  uint32_t dwLength;
  uint32_t dwVersion;
} SS_UNIT_HDR;

typedef struct tagSS_CPU6502 {
  uint8_t A;
  uint8_t X;
  uint8_t Y;
  uint8_t P;
  uint8_t S;
  uint16_t PC;
  uint64_t g_nCumulativeCycles;
} SS_CPU6502;

typedef struct {
  uint32_t baudrate;
  uint8_t bytesize;
  uint8_t commandbyte;
  uint32_t comminactivity;
  uint8_t controlbyte;
  uint8_t parity;
  uint8_t recvbuffer[SUPER_SERIAL_FIFO_SIZE];
  uint32_t recvbytes;
  uint8_t stopbits;
} SS_IO_Comms;

typedef struct tagSS_IO_Joystick {
  uint64_t g_nJoyCntrResetCycle;
} SS_IO_Joystick;

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

  uint32_t repeat_key = 0xFFFFFFFF;
  uint32_t repeat_scancode = 0;
  uint32_t repeat_delay_cycles = 0;
  uint8_t repeating = 0;
};

typedef struct SS_IO_Video {
  bool bAltCharSet;
  uint32_t dwVidMode;
} SS_IO_Video;

constexpr uint32_t nMemMainSize = 64 * 1024;
constexpr uint32_t nMemAuxSize = 64 * 1024;

typedef struct tagSS_BaseMemory {
  uint32_t dwMemMode;
  bool bLastWriteRam;
  uint8_t nMemMain[nMemMainSize];
  uint8_t nMemAux[nMemAuxSize];
} SS_BaseMemory;

typedef struct {
  SS_UNIT_HDR UnitHdr;
  SS_CPU6502 CPU6502;
  SS_IO_Comms Comms;
  SS_IO_Joystick Joystick;
  KeyboardSaveState_t Keyboard;
  SS_IO_Speaker Speaker;
  SS_IO_Video Video;
  SS_BaseMemory Memory;
} SS_APPLE2_Unit;

typedef struct {
  uint32_t dwComputerEmulation;
  bool bCustomSpeed;
  uint32_t dwEmulationSpeed;
  bool bEnhancedDiskSpeed;
  uint32_t dwJoystickType[2];
  bool bMockingboardEnabled;
  uint32_t dwMonochromeColor;
  uint32_t dwSerialPort;
  uint32_t dwSoundType;
  uint32_t dwVideoType;
} SS_AW_CFG;

typedef struct {
  char StartingDir[path_max_len];
  uint32_t dwWindowXpos;
  uint32_t dwWindowYpos;
} SS_AW_PREFS;

typedef struct {
  SS_UNIT_HDR UnitHdr;
  uint32_t dwAppleWinVersion;
  SS_AW_PREFS Prefs;
  SS_AW_CFG Cfg;
} SS_APPLEWIN_CONFIG;

constexpr uint32_t max_peripheral_name = 32;

typedef struct {
  char szName[max_peripheral_name];
  uint32_t dwVersion;
} SS_PERIPHERAL_INFO;

typedef struct {
  SS_UNIT_HDR UnitHdr;
  SS_PERIPHERAL_INFO Peripherals[8];
} SS_PERIPHERAL_MANIFEST;

typedef struct {
  SS_UNIT_HDR UnitHdr;
  uint32_t dwType;
  uint32_t dwSlot;
} SS_CARD_HDR;

enum SS_CARDTYPE {
  CT_Empty = 0,
  CT_Disk2,
  CT_SSC,
  CT_Mockingboard,
  CT_GenericPrinter,
  CT_GenericHDD,
  CT_GenericClock,
  CT_MouseInterface,
};

typedef struct {
  SS_CARD_HDR Hdr;
} SS_CARD_EMPTY;

typedef struct {
  SY6522 RegsSY6522;
  uint8_t RegsAY8910[16];
  SSI263A RegsSSI263;
  uint8_t nAYCurrentRegister;
  bool bTimer1IrqPending;
  bool bTimer2IrqPending;
  bool bSpeechIrqPending;
} MB_Unit;

constexpr uint32_t mb_units_per_card = 2;

typedef struct tagSS_CARD_MOCKINGBOARD {
  SS_CARD_HDR Hdr;
  MB_Unit Unit[mb_units_per_card];
} SS_CARD_MOCKINGBOARD;

typedef struct {
  SS_FILE_HDR Hdr;
  SS_APPLE2_Unit Apple2Unit;
  SS_PERIPHERAL_MANIFEST Manifest;
  SS_CARD_EMPTY Empty1;
  SS_CARD_EMPTY Empty2;
  SS_CARD_EMPTY Empty3;
  SS_CARD_MOCKINGBOARD Mockingboard1;
  SS_CARD_MOCKINGBOARD Mockingboard2;
  SS_CARD_EMPTY Empty6;
  SS_CARD_EMPTY Empty7;
} APPLEWIN_SNAPSHOT;
