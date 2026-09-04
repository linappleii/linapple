// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory)
#include "apple2/peripherals/clock/Clock.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>

#include "apple2/Memory.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"

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

constexpr uint16_t IO_ADDR_MASK = 0x0F;
constexpr uint8_t LATCH_UPDATE_REG = 0x0F;

struct ClockPeripheral_t {
  std::array<uint8_t, CLOCK_LATCHES_COUNT> latches{};
  HostInterface_t* host = nullptr;
  int slot = 0;
};

static auto set_latch_pair(ClockPeripheral_t* clock_peripheral, size_t index,
                           int value) -> void {
  constexpr size_t index_mask = 0x0E;
  constexpr int radix = 10;
  constexpr int value_max = 100;

  const size_t base_index = index & index_mask;
  if (base_index + 1 >= CLOCK_LATCHES_COUNT) {
    return;
  }

  const int clamped_value = std::abs(value) % value_max;
  const auto digits = std::div(clamped_value, radix);

  clock_peripheral->latches.at(base_index) = static_cast<uint8_t>(digits.quot);
  clock_peripheral->latches.at(base_index + 1) =
      static_cast<uint8_t>(digits.rem);
}

static auto update_latches(ClockPeripheral_t* clock_peripheral) -> void {
  time_t now = 0;
  if (time(&now) == static_cast<time_t>(-1)) {
    return;
  }

  struct tm local_time{};
  if (localtime_r(&now, &local_time) == nullptr) {
    return;
  }

  const int month = local_time.tm_mon + 1;
  const int weekday = local_time.tm_wday;
  const int day = local_time.tm_mday;
  const int hour = local_time.tm_hour;
  const int minute = local_time.tm_min;

  set_latch_pair(clock_peripheral, LATCH_MONTH, month);
  set_latch_pair(clock_peripheral, LATCH_WEEKDAY, weekday);
  set_latch_pair(clock_peripheral, LATCH_DAY, day);
  set_latch_pair(clock_peripheral, LATCH_HOUR, hour);
  set_latch_pair(clock_peripheral, LATCH_MINUTE, minute);
}

static auto clock_io_read(void* instance, uint16_t program_counter,
                          uint16_t memory_address, uint8_t is_write,
                          uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  if (instance == nullptr) {
    return io_null(program_counter, memory_address, is_write, data_value,
                   remaining_cycles);
  }
  auto* clock_peripheral = static_cast<ClockPeripheral_t*>(instance);

  const uint16_t register_offset = memory_address & IO_ADDR_MASK;
  if (register_offset < CLOCK_LATCHES_COUNT) {
    return clock_peripheral->latches.at(register_offset);
  } else if (register_offset == LATCH_UPDATE_REG) {
    update_latches(clock_peripheral);
    return 0;
  }

  return io_null(program_counter, memory_address, is_write, data_value,
                 remaining_cycles);
}

static auto clock_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }

  auto clock_peripheral =
      std::unique_ptr<ClockPeripheral_t>(new ClockPeripheral_t());
  clock_peripheral->slot = slot;
  clock_peripheral->host = host;

  std::array<uint8_t, CX_ROM_SIZE> cx_rom_data{};
  const size_t bytes_to_copy =
      std::min(Clock_ROM.size(), static_cast<size_t>(CX_ROM_SIZE));

  std::copy(Clock_ROM.begin(), Clock_ROM.begin() + bytes_to_copy,
            cx_rom_data.begin());

  host->RegisterCxROM(slot, cx_rom_data.data());
  host->RegisterIO(slot, clock_io_read, nullptr, nullptr, nullptr);

  return clock_peripheral.release();
}

static auto clock_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* clock_peripheral = static_cast<ClockPeripheral_t*>(instance);
  clock_peripheral->latches.fill(0);
}

static auto clock_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }

  std::unique_ptr<ClockPeripheral_t> clock_peripheral(
      static_cast<ClockPeripheral_t*>(instance));
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
// Justification: ABI-required function signature.
static auto clock_abi_save_state(void* instance, void* state_buffer,
                                 size_t* buffer_size) -> PeripheralStatus_t {
  if (instance == nullptr || buffer_size == nullptr) {
    return peripheral_error;
  }

  auto* clock_peripheral = static_cast<ClockPeripheral_t*>(instance);
  const size_t state_size = clock_peripheral->latches.size();

  if (state_buffer == nullptr) {
    *buffer_size = state_size;
    return peripheral_ok;
  }

  if (*buffer_size < state_size) {
    return peripheral_error;
  }

  std::copy(clock_peripheral->latches.begin(), clock_peripheral->latches.end(),
            static_cast<uint8_t*>(state_buffer));
  *buffer_size = state_size;
  return peripheral_ok;
}

static auto clock_abi_load_state(void* instance, const void* state_buffer,
                                 size_t buffer_size) -> PeripheralStatus_t {
  if (instance == nullptr || state_buffer == nullptr) {
    return peripheral_error;
  }

  auto* clock_peripheral = static_cast<ClockPeripheral_t*>(instance);
  const size_t state_size = clock_peripheral->latches.size();

  if (buffer_size != state_size) {
    return peripheral_error;
  }

  const auto* src = static_cast<const uint8_t*>(state_buffer);
  std::copy_n(src, state_size, clock_peripheral->latches.begin());
  return peripheral_ok;
}

static Peripheral_t g_clock_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.clock",
    .name = "Clock Card",
    .description = "ProDOS compatible Clock",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = -1,
    .init = clock_abi_init,
    .reset = clock_abi_reset,
    .shutdown = clock_abi_shutdown,
    .think = nullptr,
    .on_vblank = nullptr,
    .save_state = clock_abi_save_state,
    .load_state = clock_abi_load_state,
    .command = nullptr,
    .query = nullptr};

auto clock_get_descriptor() -> Peripheral_t* { return &g_clock_peripheral; }

PERIPHERAL_REGISTER(g_clock_peripheral)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory)
