// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification: C99-compatible public ABI of the AppleMouse II interface card.

#include <stdint.h>

#include "apple2/peripherals/Peripheral_Subsystems.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MOUSE_STATE_VERSION = 1 };

// Command id 0x0000 carried an absolute host position and a host range; it
// is retired and never reused.
typedef enum {
  mouse_cmd_set_button =
      PERIPHERAL_SUBSYSTEM_MOUSE | 0x0001, /**< data: MouseButtonPayload_t */
  mouse_cmd_move =
      PERIPHERAL_SUBSYSTEM_MOUSE | 0x0002 /**< data: MouseMovePayload_t */
} MouseCmd_t;

typedef enum {
  mouse_query_is_active = PERIPHERAL_SUBSYSTEM_MOUSE |
                          0x0001 /**< out: uint8_t (0=inactive, 1=active) */
} MouseQuery_t;

// Counts of the mouse's own quadrature, one per step of about 0.020 inch
// (AppleMouse II User's Manual p. 45): X positive to the right, Y positive
// toward the user. The card adds them while the mouse is on and clamps.
typedef struct {
  int32_t dx;
  int32_t dy;
} MouseMovePayload_t;

// The card has one button (schematic 050-0101-A: SW on J1-4 to the 6805's
// PB7); a payload naming button 1 is accepted and ignored.
typedef struct {
  uint8_t button; /**< 0 or 1 */
  uint8_t down;   /**< 0 released, 1 pressed */
  uint8_t padding[2];
} MouseButtonPayload_t;

// The frame has ridden the slot trailer and the slot-4 fixed region of every
// .aws written with the card, so every field keeps its offset. Positions and
// clamps are signed 16-bit values carried in the low 16 bits of their words.
// tick_phase is the cycles from the saved instant to the card's next 60 Hz
// (or 50 Hz) tick; older frames hold the host window's width there and its
// height in reserved0, and the loader takes the width as a phase bounded by
// one period. Bytes 64-71 were the PIA's CA1, CA2, CB1 and CB2 inputs and
// outputs and its two IRQ outputs, pins the card leaves unconnected (schematic
// zone C3), so every frame ever written holds them as zero and the 6805's
// rate, pending sources, IRQ level and the parser's reply fields take them.
// The reserved bytes are written as zero and read past.
typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint32_t position_x;
  uint32_t position_y;
  uint32_t min_x;
  uint32_t max_x;
  uint32_t min_y;
  uint32_t max_y;
  uint32_t tick_phase;
  uint32_t reserved0;
  uint32_t read_x;
  uint32_t read_y;
  uint32_t parser_pos;
  uint32_t parser_out_len;
  uint8_t pia_ora;
  uint8_t pia_orb;
  uint8_t pia_ddra;
  uint8_t pia_ddrb;
  uint8_t pia_cra;
  uint8_t pia_crb;
  uint8_t pia_port_a_in;
  uint8_t pia_port_b_in;
  uint8_t rate_50hz;
  uint8_t pending;
  uint8_t irq_asserted;
  uint8_t reserved1;
  uint8_t parser_in_len;
  uint8_t parser_reply_pos;
  uint8_t reserved2[2];
  uint8_t pia_port_a_shadow;
  uint8_t pia_port_b_shadow;
  uint8_t mode;
  uint8_t reserved3;
  uint8_t status;
  uint8_t button_at_last_read;
  uint8_t reserved4;
  uint8_t button;
  uint8_t reserved5;
  uint8_t buffer[8];
  uint8_t padding[3];
} MouseSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
