#ifndef STRUCTS_H
#define STRUCTS_H

#include <cstdint>
#include "core/Common.h"
#include "apple2/Speaker_Structs.h"

// Structs used by save-state file

// *** DON'T CHANGE ANY STRUCT WITHOUT CONSIDERING BACKWARDS COMPATIBILITY WITH .AWS FORMAT ***

#define MAKE_VERSION(a, b, c, d) ((a<<24) | (b<<16) | (c<<8) | (d))

#define AW_SS_TAG (('S'<<24)|('S'<<16)|('W'<<8)|'A')  // 'AWSS' = AppleWin SnapShot

typedef struct {
  uint32_t dwTag;    // "AWSS"
  uint32_t dwVersion;
  uint32_t dwChecksum;
} SS_FILE_HDR;

typedef struct {
  uint32_t dwLength;    // Byte length of this unit struct
  uint32_t dwVersion;
} SS_UNIT_HDR;

const uint32_t nMemMainSize = 64 * 1024;
const uint32_t nMemAuxSize = 64 * 1024;

typedef struct tagSS_CPU6502 {
  uint8_t A;
  uint8_t X;
  uint8_t Y;
  uint8_t P;
  uint8_t S;
  uint16_t PC;
  uint64_t g_nCumulativeCycles;
  // IRQ = OR-sum of all interrupt sources
} SS_CPU6502;

const uint32_t uRecvBufferSize = 9;

typedef struct {
  uint32_t baudrate;
  uint8_t bytesize;
  uint8_t commandbyte;
  uint32_t comminactivity;  // If non-zero then COM port open
  uint8_t controlbyte;
  uint8_t parity;
  uint8_t recvbuffer[uRecvBufferSize];
  uint32_t recvbytes;
  uint8_t stopbits;
} SS_IO_Comms;

typedef struct tagSS_IO_Joystick {
  uint64_t g_nJoyCntrResetCycle;
} SS_IO_Joystick;

#include "apple2/Keyboard_Structs.h"

typedef struct SS_IO_Video {
  bool bAltCharSet;  // charoffs
  uint32_t dwVidMode;
} SS_IO_Video;

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
  SS_IO_Keyboard Keyboard;
  //  SS_IO_Memory Memory;
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
  uint32_t dwSoundType;  // Sound Emulation
  uint32_t dwVideoType;  // Video Emulation
} SS_AW_CFG;

typedef struct {
  char StartingDir[PATH_MAX_LEN];
  uint32_t dwWindowXpos;
  uint32_t dwWindowYpos;
} SS_AW_PREFS;

typedef struct {
  SS_UNIT_HDR UnitHdr;
  uint32_t dwAppleWinVersion;
  SS_AW_PREFS Prefs;
  SS_AW_CFG Cfg;
} SS_APPLEWIN_CONFIG;

#define MAX_PERIPHERAL_NAME 32

typedef struct {
  char szName[MAX_PERIPHERAL_NAME];
  uint32_t dwVersion;
} SS_PERIPHERAL_INFO;

typedef struct {
  SS_UNIT_HDR UnitHdr;
  SS_PERIPHERAL_INFO Peripherals[8]; // Slots 0-7
} SS_PERIPHERAL_MANIFEST;

typedef struct {
  SS_UNIT_HDR UnitHdr;
  uint32_t dwType;    // SS_CARDTYPE
  uint32_t dwSlot;    // [1..7]
} SS_CARD_HDR;

enum SS_CARDTYPE {
  CT_Empty = 0, CT_Disk2,      // Apple Disk][
  CT_SSC,        // Apple Super Serial Card
  CT_Mockingboard, CT_GenericPrinter, CT_GenericHDD,    // Hard disk
  CT_GenericClock, CT_MouseInterface,
};

typedef struct {
  SS_CARD_HDR Hdr;
} SS_CARD_EMPTY;

typedef struct {
  union {
    struct {
      uint8_t l;
      uint8_t h;
    };
    uint16_t w;
  };
} IWORD;

typedef struct {
  uint8_t ORB;        // $00 - Port B
  uint8_t ORA;        // $01 - Port A (with handshaking)
  uint8_t DDRB;        // $02 - Data Direction Register B
  uint8_t DDRA;        // $03 - Data Direction Register A
  //
  // $04 - Read counter (L) / Write latch (L)
  // $05 - Read / Write & initiate count (H)
  // $06 - Read / Write & latch (L)
  // $07 - Read / Write & latch (H)
  // $08 - Read counter (L) / Write latch (L)
  // $09 - Read counter (H) / Write latch (H)
  IWORD TIMER1_COUNTER;
  IWORD TIMER1_LATCH;
  IWORD TIMER2_COUNTER;
  IWORD TIMER2_LATCH;
  //
  uint8_t SERIAL_SHIFT;    // $0A
  uint8_t ACR;        // $0B - Auxiliary Control Register
  uint8_t PCR;        // $0C - Peripheral Control Register
  uint8_t IFR;        // $0D - Interrupt Flag Register
  uint8_t IER;        // $0E - Interrupt Enable Register
  uint8_t ORA_NO_HS;      // $0F - Port A (without handshaking)
} SY6522;

typedef struct {
  uint8_t DurationPhonome;
  uint8_t Inflection;    // I10..I3
  uint8_t RateInflection;
  uint8_t CtrlArtAmp;
  uint8_t FilterFreq;
  //
  uint8_t CurrentMode;    // b7:6=Mode; b0=D7 pin (for IRQ)
} SSI263A;

typedef struct {
  SY6522 RegsSY6522;
  uint8_t RegsAY8910[16];
  SSI263A RegsSSI263;
  uint8_t nAYCurrentRegister;
  bool bTimer1IrqPending;
  bool bTimer2IrqPending;
  bool bSpeechIrqPending;
} MB_Unit;

const uint32_t MB_UNITS_PER_CARD = 2;

typedef struct tagSS_CARD_MOCKINGBOARD {
  SS_CARD_HDR Hdr;
  MB_Unit Unit[MB_UNITS_PER_CARD];
} SS_CARD_MOCKINGBOARD;

typedef struct {
  SS_FILE_HDR Hdr;
  SS_APPLE2_Unit Apple2Unit;
  SS_PERIPHERAL_MANIFEST Manifest;
  //  SS_APPLEWIN_CONFIG AppleWinCfg;
  SS_CARD_EMPTY Empty1;        // Slot1
  SS_CARD_EMPTY Empty2;        // Slot2
  SS_CARD_EMPTY Empty3;        // Slot3
  SS_CARD_MOCKINGBOARD Mockingboard1;  // Slot4
  SS_CARD_MOCKINGBOARD Mockingboard2;  // Slot5
  SS_CARD_EMPTY Empty6;        // Slot6
  SS_CARD_EMPTY Empty7;        // Slot7
} APPLEWIN_SNAPSHOT;
#endif // STRUCTS_H
