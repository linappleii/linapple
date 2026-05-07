#include "apple2/Clock.h"

// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "apple2/Memory.h"
#include "apple2/Structs.h"
#include "core/Common.h"
#include "core/Peripheral.h"

/*
I/O map: (please add an offset of Slot#*16 to the address below)

        C080	(r)   latched month (tens)
        C081	(r)   latched month (units)
        C082	(r)   always zero
        C083	(r)   latched day-of-week
        C084	(r)   latched day-of-month (tens)
        C085	(r)   latched day-of-month (units)
        C086	(r)   latched hour (tens)
        C087	(r)   latched hour (units)
        C088	(r)   latched minute (tens)
        C089	(r)   latched minute (units)
        C08F	(r)   get system clock and update the latched values
*/

/*
  This emulates a ProDOS-compatible clock card.
  The interface is specified in chapter 6 of

      ProDOS 8 Tech. Ref.
      http://apple2.info/wiki/index.php?title=P8_Tech_Ref_Chapter_6

  Briefly speaking, ProDOS detects the clock card if it finds that
    $Cx00 => $08
    $Cx02 => $28
    $Cx04 => $58
    $Cx06 => $70

  When ProDOS needs to get the time, it calls $Cx0B with A=0xA3 ("#")
  (which this ROM ignores) and then calls $Cx08.
  Upon returning from the latter, it expects the line buffer (beginning
  at $0200) to contain an ASCII (with 8-th bit on) like:
    01,02,03,04,05
  terminated by a trailing $80.
  The five 2-digit numbers, separated by commas, are:
    month (1--12), day-of-week (0=Sun - 6=Sat), day-of-month,
    hour (24hr clock), minute

  You can easily test this interface by enterint the monitor (CALL -151)
  and then type (the first "*" is the prompt character):

  *                    Cx08G 200.20F

  where x is the slot number of this clock card.
  Please leave at least 16 blanks before the character "C" so that
  the command line buffer won't be trashed by the ROM routine before
  it is processed.
*/

static const std::array<uint8_t, 95> Clock_ROM =
    /*
    *
    * ROM code for a simplistic ProDOS-compatible clock card
    * for the Apple II emulator 'linapple'
    *
    * (C) 2008 by LEE Sau Dan
    *
     ORG $C000
    STACK EQU $100
    IN EQU $200
    DEVSEL EQU $C080
    MONRTS EQU $FF58
    LATCHIT EQU DEVSEL+$F

    ST
     PHP  ; this opcode byte must be $08 for prodos to recognize
     BCC D1 ; offset byte must be $28 for prodos to recognize
    B1
     BCS D2 ; offset byte must be $58 for prodos to recognize
    B2
     DB 00
     DB $70 ; byte must be $70 for prodos to recognize
     DB 00
    RDCLK ; ProDOS calls $Cx08 to get clock data
     NOP
     NOP
     DB $A9 ; opcode for LDA #xx, just to skip the next byte
    WRCLK ; ProDOS calls $Cx0B with AX="#" before calling RDCLK
     RTS

    RDCLK1
     PHP
     SEI
     JSR MONRTS ; known to be RTS
     TSX
     LDA STACK,X ; high byte of PC
     PLP
     ASL
     ASL
     ASL
     ASL
     TAY

     LDA LATCHIT,Y ; latch current time to regs
     LDX #$0
     BEQ RDCLK1_CONT


     DS B1+$28-*
    D1
     PLP
     RTS


    RDCLK1_CONT
     LDA DEVSEL,Y
     INY
     ORA #"0"
     STA IN,X
     INX
     LDA DEVSEL,Y
     INY
     ORA #"0"
     STA IN,X
     INX
     LDA #","
     STA IN,X
     INX
     TYA
     AND #$0F
     CMP #10
     BCC RDCLK1_CONT
     LDA #$80 ; EOS
     STA IN-1,X ; overwrite the last ','
     RTS


     DS B2+$58-*
    D2
     BCS D1


     END
    */
    {{
        0x08, 0x90, 0x28, 0xb0, 0x58, 0x00, 0x70, 0x00, 0xea, 0xea, 0xa9, 0x60,
        0x08, 0x78, 0x20, 0x58, 0xff, 0xba, 0xbd, 0x00, 0x01, 0x28, 0x0a, 0x0a,
        0x0a, 0x0a, 0xa8, 0xb9, 0x8f, 0xc0, 0xa2, 0x00, 0xf0, 0x0b, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x60, 0xb9, 0x80, 0xc0,
        0xc8, 0x09, 0xb0, 0x9d, 0x00, 0x02, 0xe8, 0xb9, 0x80, 0xc0, 0xc8, 0x09,
        0xb0, 0x9d, 0x00, 0x02, 0xe8, 0xa9, 0xac, 0x9d, 0x00, 0x02, 0xe8, 0x98,
        0x29, 0x0f, 0xc9, 0x0a, 0x90, 0xdf, 0xa9, 0x80, 0x9d, 0xff, 0x01, 0x60,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xcc,
    }};

struct ClockPeripheral_t {
  std::array<uint8_t, 10> latches{};
  HostInterface_t* host = nullptr;
  int slot = 0;
};

static void set_latch_pair(ClockPeripheral_t* cp, int index, int value) {
  cp->latches[static_cast<size_t>(index &= 0x0E)] =
      static_cast<uint8_t>((value %= 100) / 10);
  cp->latches[static_cast<size_t>(index | 1)] =
      static_cast<uint8_t>(value % 10);
}

static void update_latches(ClockPeripheral_t* cp) {
  time_t t = 0;
  struct tm tm{};

  time(&t);
  localtime_r(&t, &tm);
  set_latch_pair(cp, 0, 1 + tm.tm_mon);
  set_latch_pair(cp, 2, tm.tm_wday);
  set_latch_pair(cp, 4, tm.tm_mday);
  set_latch_pair(cp, 6, tm.tm_hour);
  set_latch_pair(cp, 8, tm.tm_min);
}

static auto Clock_IORead(void* instance, uint16_t pc, uint16_t addr,
                         uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  if (!instance) {
    return IO_Null(pc, addr, write, val, cycles_left);
  }
  auto* cp = static_cast<ClockPeripheral_t*>(instance);

  switch (addr & 0x0F) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
      return cp->latches[static_cast<size_t>(addr & 0x0F)];

    case 0xF:
      update_latches(cp);
      return 0;

    default:
      return IO_Null(pc, addr, write, val, cycles_left);
  }
}

static auto Clock_ABI_Init(int slot, HostInterface_t* host) -> void* {
  auto* cp = new ClockPeripheral_t();
  cp->slot = slot;
  cp->host = host;

  uint8_t slot_rom[256];
  memset(slot_rom, 0, 256);
  memcpy(slot_rom, Clock_ROM.data(), Clock_ROM.size());

  // ProDOS-compatible clock cards expect ROM at $Cx00
  if (slot > 0 && slot < 8) {
    host->RegisterCxROM(slot, slot_rom);
  }

  host->RegisterIO(slot, Clock_IORead, nullptr, nullptr, nullptr);

  return cp;
}

static void Clock_ABI_Reset(void* instance) {
  if (!instance) {
    return;
  }
  auto* cp = static_cast<ClockPeripheral_t*>(instance);
  cp->latches.fill(0);
}

static void Clock_ABI_Shutdown(void* instance) {
  if (!instance) {
    return;
  }
  auto* cp = static_cast<ClockPeripheral_t*>(instance);
  delete cp;
}

static auto Clock_ABI_SaveState(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus {
  if (!instance || !size) {
    return PERIPHERAL_ERROR;
  }

  auto* cp = static_cast<ClockPeripheral_t*>(instance);
  constexpr size_t state_size = sizeof(cp->latches);

  if (!buffer) {
    *size = state_size;
    return PERIPHERAL_OK;
  }

  if (*size < state_size) {
    return PERIPHERAL_ERROR;
  }

  memcpy(buffer, cp->latches.data(), state_size);
  *size = state_size;
  return PERIPHERAL_OK;
}

static auto Clock_ABI_LoadState(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus {
  if (!instance || !buffer) {
    return PERIPHERAL_ERROR;
  }

  auto* cp = static_cast<ClockPeripheral_t*>(instance);
  constexpr size_t state_size = sizeof(cp->latches);

  if (size != state_size) {
    return PERIPHERAL_ERROR;
  }

  memcpy(cp->latches.data(), buffer, state_size);
  return PERIPHERAL_OK;
}

Peripheral_t g_clock_peripheral = {
    LINAPPLE_ABI_VERSION,
    "Clock Card",
    LINAPPLE_ANY_SLOT_MASK,
    Clock_ABI_Init,
    Clock_ABI_Reset,
    Clock_ABI_Shutdown,
    nullptr,  // think
    nullptr,  // on_vblank
    Clock_ABI_SaveState,
    Clock_ABI_LoadState,
    nullptr,  // command
    nullptr   // query
};

#ifdef BUILD_SHARED_PERIPHERAL
EXPORT_PERIPHERAL(g_clock_peripheral)
#endif

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
