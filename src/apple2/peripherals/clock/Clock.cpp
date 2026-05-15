// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory)
#include "apple2/peripherals/clock/Clock.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

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

constexpr size_t CLOCK_LATCHES_COUNT = 10;
constexpr uint16_t IO_ADDR_MASK = 0x0F;
constexpr uint8_t LATCH_UPDATE_REG = 0x0F;
constexpr int MAX_SLOTS = 8;

struct ClockPeripheral_t {
  std::array<uint8_t, CLOCK_LATCHES_COUNT> latches{};
  HostInterface_t* host = nullptr;
  int slot = 0;
};

constexpr int LATCH_MONTH = 0;
constexpr int LATCH_WEEKDAY = 2;
constexpr int LATCH_DAY = 4;
constexpr int LATCH_HOUR = 6;
constexpr int LATCH_MINUTE = 8;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void set_latch_pair(ClockPeripheral_t* cp, size_t index, int value) {
  constexpr int kValueMax = 100;
  constexpr int kDivisor = 10;
  constexpr size_t kIndexMask = 0x0E;

  const auto base_index = static_cast<size_t>(index & kIndexMask);
  if (base_index + 1 < CLOCK_LATCHES_COUNT) {
    int val = value % kValueMax;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,
    // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    // Justification: base_index is bounds-checked above against
    // CLOCK_LATCHES_COUNT.
    cp->latches[base_index] = static_cast<uint8_t>(val / kDivisor);
    cp->latches[base_index + 1] = static_cast<uint8_t>(val % kDivisor);
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,
    // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  }
}

static void update_latches(ClockPeripheral_t* cp) {
  time_t t = 0;
  struct tm tm{};

  time(&t);
  localtime_r(&t, &tm);
  set_latch_pair(cp, LATCH_MONTH, 1 + tm.tm_mon);
  set_latch_pair(cp, LATCH_WEEKDAY, tm.tm_wday);
  set_latch_pair(cp, LATCH_DAY, tm.tm_mday);
  set_latch_pair(cp, LATCH_HOUR, tm.tm_hour);
  set_latch_pair(cp, LATCH_MINUTE, tm.tm_min);
}

static auto Clock_IORead(void* instance, uint16_t pc, uint16_t addr,
                         uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  if (!instance) {
    return IO_Null(pc, addr, write, val, cycles_left);
  }
  auto* cp = static_cast<ClockPeripheral_t*>(instance);

  const uint16_t reg = addr & IO_ADDR_MASK;
  if (reg < CLOCK_LATCHES_COUNT) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
    // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    // Justification: reg is explicitly checked against CLOCK_LATCHES_COUNT.
    return cp->latches[reg];
  }

  if (reg == LATCH_UPDATE_REG) {
    update_latches(cp);
    return 0;
  }

  return IO_Null(pc, addr, write, val, cycles_left);
}

static auto Clock_ABI_Init(int slot, HostInterface_t* host) -> void* {
  auto cp = std::unique_ptr<ClockPeripheral_t>(new ClockPeripheral_t());
  cp->slot = slot;
  cp->host = host;

  std::array<uint8_t, CX_ROM_SIZE> slot_rom{};
  // ProDOS-compatible clock cards expect ROM at $Cx00
  if (slot > 0 && slot < MAX_SLOTS) {
    const size_t copy_size =
        std::min(Clock_ROM.size(), static_cast<size_t>(CX_ROM_SIZE));
    memcpy(slot_rom.data(), Clock_ROM.data(), copy_size);
    host->RegisterCxROM(slot, slot_rom.data());
  }

  host->RegisterIO(slot, Clock_IORead, nullptr, nullptr, nullptr);

  return cp.release();
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
  std::unique_ptr<ClockPeripheral_t> cp(
      static_cast<ClockPeripheral_t*>(instance));
  // unique_ptr will delete it when it goes out of scope
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
// Justification: ABI-required function signature.
static auto Clock_ABI_SaveState(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus {
  if (!instance || !size) {
    return PERIPHERAL_ERROR;
  }

  auto* cp = static_cast<ClockPeripheral_t*>(instance);
  const size_t state_size = cp->latches.size();

  if (!buffer) {
    *size = state_size;
    return PERIPHERAL_OK;
  }

  if (*size < state_size) {
    return PERIPHERAL_ERROR;
  }

  std::copy(cp->latches.begin(), cp->latches.end(),
            static_cast<uint8_t*>(buffer));
  *size = state_size;
  return PERIPHERAL_OK;
}

static auto Clock_ABI_LoadState(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus {
  if (!instance || !buffer) {
    return PERIPHERAL_ERROR;
  }

  auto* cp = static_cast<ClockPeripheral_t*>(instance);
  const size_t state_size = cp->latches.size();

  if (size != state_size) {
    return PERIPHERAL_ERROR;
  }

  const auto* src = static_cast<const uint8_t*>(buffer);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  // Justification: Standard iterator pattern for std::copy.
  std::copy(src, src + state_size, cp->latches.begin());
  return PERIPHERAL_OK;
}

Peripheral_t g_clock_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.clock",
    .name = "Clock Card",
    .description = "Thunderclock and No-Slot Clock emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = -1,
    .init = Clock_ABI_Init,
    .reset = Clock_ABI_Reset,
    .shutdown = Clock_ABI_Shutdown,
    .think = nullptr,
    .on_vblank = nullptr,
    .save_state = Clock_ABI_SaveState,
    .load_state = Clock_ABI_LoadState,
    .command = nullptr,
    .query = nullptr};

PERIPHERAL_REGISTER(g_clock_peripheral)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory)
