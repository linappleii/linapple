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

typedef enum {
  mockingboard_type_mockingboard = 0,
  mockingboard_type_phasor = 1
} MockingboardCardType_t;

typedef enum {
  mockingboard_cmd_set_type =
      0x0001, /**< data: uint8_t (MockingboardCardType_t) */
  mockingboard_cmd_reset_audio = 0x0002 /**< data: none */
} MockingboardCmd_t;

typedef enum {
  mockingboard_query_status = 0x0001 /**< out: MockingboardStatus_t */
} MockingboardQuery_t;

typedef struct {
  uint8_t card_type;
  uint8_t timer_irq_active;
  uint8_t phasor_native;
  uint8_t reserved[5]; /**< Padding for 8-byte ABI alignment */
} MockingboardStatus_t;

typedef struct {
  // 6522 VIA registers (20 bytes)
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
  uint8_t reserved_via[2]; /**< Align to 4 bytes */

  // AY-3-8910 PSG registers and counters (68 bytes)
  uint8_t ay_regs[MOCKINGBOARD_AY_REGS];
  uint16_t count_a;
  uint16_t count_b;
  uint16_t count_c;
  uint8_t out_a;
  uint8_t out_b;
  uint8_t out_c;
  uint8_t out_n;
  uint8_t reserved_mid[2]; /**< Align to 4 bytes */
  uint32_t count_n;
  uint32_t rng;
  uint32_t count_e;
  uint32_t envelope_step;
  uint8_t envelope_vol;
  uint8_t env_holding;
  uint16_t ay_current_register;
  uint8_t ay_number;
  uint8_t reserved_ay[3];
  int32_t timer_status;
  uint8_t reserved_end[4]; /**< Align double to 8 bytes */
  double count_accum;
} MockingboardChipSaveState_t;

typedef struct {
  uint32_t version;     /* 0..3 */
  uint32_t struct_size; /* 4..7 */

  MockingboardChipSaveState_t
      chips[MOCKINGBOARD_NUM_CHIPS]; /* 8..183 (176 bytes) */

  uint32_t timer_period_6522;
  uint16_t mb_timer_device;
  uint8_t mb_reg_accessed_flag;
  uint8_t mb_active;
  uint8_t timer_irq_active;
  uint8_t phasor_native;
  uint8_t card_type;
  uint8_t reserved[1];

  uint32_t timer1_irq_count;

  uint64_t last_cumulative_cycles;
  uint64_t mb_inactive_cycle_count;
  uint64_t last_60hz;
  uint8_t reserved_final[8]; /**< Align struct to 8 bytes */
} MockingboardSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
