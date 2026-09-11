// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>
#include <cstring>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "doctest.h"

// The emulator currently uses a global state.
// To make it testable, we provide a clean initialization for every test.
void reset_machine() {
  g_apple2_type = A2TYPE_APPLE2EENHANCED;
  mem_initialize();
  cpu_initialize();
}

// Helper to run a 6502 snippet
void machine_execute(const uint8_t* code, size_t size,
                     uint32_t max_cycles = 1000) {
  memcpy(mem + 0x0300, code, size);
  cpu_get_registers()->pc = 0x0300;

  uint32_t cycles = 0;
  while (mem[cpu_get_registers()->pc] != 0x60 && cycles < max_cycles) {
    cpu_execute(1);
    cycles++;
  }
}

TEST_CASE("Legacy: [MEM-01] Language Card RAM Banking") {
  reset_machine();

  // 1. Double-read $C081 to enable RAM Write
  // 2. Write $55 to $D000
  // 3. Single-read $C080 to enable RAM Read
  // 4. Read $D000 into Accumulator
  // 5. RTS
  uint8_t code[] = {
      0xAD, 0x81, 0xC0,  // LDA $C081
      0xAD, 0x81, 0xC0,  // LDA $C081 (Write Enable ON)
      0xA9, 0x55,        // LDA #$55
      0x8D, 0x00, 0xD0,  // STA $D000
      0xAD, 0x80, 0xC0,  // LDA $C080 (Read Enable ON)
      0xAD, 0x00, 0xD0,  // LDA $D000
      0x60               // RTS
  };

  machine_execute(code, sizeof(code));
  CHECK(cpu_get_registers()->a == 0x55);
}

TEST_CASE("Legacy: [ROM-01] Firmware Integrity (Autostart ROM)") {
  reset_machine();

  // Requirement: entry point $FF65 must exist (Monitor)
  // We check the memory directly
  CHECK(mem[0xFF65] != 0x00);
}

TEST_CASE("Core: Emulation Speed Controls and Multipliers") {
  linapple_set_speed(SPEED_NORMAL);
  CHECK(linapple_get_speed() == SPEED_NORMAL);

  g_state.clks_per_frame = 17030;
  CHECK(linapple_get_frame_cycles() == 17030);

  // Increase speed (+2 per step)
  CHECK(linapple_speed_increase() == 12);
  CHECK(linapple_get_speed() == 12);
  CHECK(linapple_get_frame_cycles() == 20436);

  // Set speed to 2x (20)
  linapple_set_speed(20);
  CHECK(linapple_get_speed() == 20);
  CHECK(linapple_get_frame_cycles() == 34060);

  // Decrease speed (-1 per step)
  CHECK(linapple_speed_decrease() == 19);
  CHECK(linapple_get_speed() == 19);

  // Reset speed to normal (10)
  CHECK(linapple_speed_reset() == SPEED_NORMAL);
  CHECK(linapple_get_speed() == SPEED_NORMAL);
  CHECK(linapple_get_frame_cycles() == 17030);

  // Slow motion: speed 0 (0.5x)
  linapple_set_speed(0);
  CHECK(linapple_get_speed() == 0);
  CHECK(linapple_get_frame_cycles() == 8515);

  // Clamp at max
  linapple_set_speed(100);
  CHECK(linapple_get_speed() == emulation_speed_max);

  // Restore normal speed
  linapple_speed_reset();
}

TEST_CASE("Core: Turbo Mode Toggle") {
  linapple_set_turbo(false);
  CHECK(linapple_get_turbo() == false);

  CHECK(linapple_toggle_turbo() == true);
  CHECK(linapple_get_turbo() == true);

  CHECK(linapple_toggle_turbo() == false);
  CHECK(linapple_get_turbo() == false);

  linapple_set_turbo(true);
  CHECK(linapple_get_turbo() == true);
  linapple_set_turbo(false);
}
