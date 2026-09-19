// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification:
// This header defines a language-neutral C ABI. C system headers, typedefs, and
// C-style arrays are required for compatibility with C-based consumers.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  MOCKINGBOARD_STATE_VERSION = 1,
  MOCKINGBOARD_NUM_CHIPS = 2,
  MOCKINGBOARD_AY_REGS = 16
};

// Version 1 snapshot layout for one 6522 VIA plus one AY-3-8910 PSG.
// Field offsets are fixed for save-state backwards compatibility.
typedef struct {
  /* 6522 VIA registers (20 bytes) */
  uint8_t orb;
  uint8_t ora;
  uint8_t ddrb;
  uint8_t ddra;
  uint16_t t1_counter;
  uint16_t t1_latch;
  uint16_t t2_counter;
  uint16_t t2_latch;
  uint8_t serial_shift;
  uint8_t acr;
  uint8_t pcr;
  uint8_t ifr;
  uint8_t ier;
  uint8_t ora_no_hs;
  // Bit 0: t1_fired, bit 1: t2_fired, bit 2: pb7, bits 3-4: t1 phase, bits 5-6:
  // t2 phase
  uint8_t via_flags;
  uint8_t reserved_via;

  /* AY-3-8910 registers, generators and envelope (68 bytes) */
  uint8_t ay_regs[MOCKINGBOARD_AY_REGS];
  uint16_t count_a;
  uint16_t count_b;
  uint16_t count_c;
  uint8_t out_a;
  uint8_t out_b;
  uint8_t out_c;
  uint8_t out_n;
  uint8_t reserved_mid[2];
  uint32_t count_n;
  uint32_t rng;
  uint32_t count_e;
  uint32_t envelope_step;
  uint8_t envelope_vol;
  uint8_t env_holding;
  uint16_t ay_current_register;
  uint8_t env_attack;
  uint8_t reserved_ay[3];
  uint32_t reserved_timer_status;
  uint8_t reserved_end[4];
  uint8_t reserved_accum[8];
} MockingboardChipSaveState_t;

typedef struct {
  uint32_t version;
  uint32_t struct_size;

  MockingboardChipSaveState_t chips[MOCKINGBOARD_NUM_CHIPS];

  uint32_t psg_remainder;
  uint8_t reserved_card[8];
  uint8_t reserved_counts[4];
  uint8_t reserved_cycles[24];
  uint8_t reserved_final[8];
} MockingboardSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
