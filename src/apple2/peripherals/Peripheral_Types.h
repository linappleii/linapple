// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, readability-identifier-naming)

typedef enum {
  peripheral_ok = 0,
  peripheral_error = -1,
  peripheral_incompatible = -2,
  peripheral_busy = -3,
} PeripheralStatus;

typedef enum {
  log_debug = 0,
  log_info,
  log_warn,
  log_error,
} PeripheralLogLevel;

// What a byte stream leaving a card is for. The host names its destination by
// slot and kind, so two printer cards get two destinations.
typedef enum {
  peripheral_sink_printer = 1,
  peripheral_sink_serial = 2,
} PeripheralSinkKind;

typedef enum {
  peripheral_serial_parity_none = 0,
  peripheral_serial_parity_odd,
  peripheral_serial_parity_even,
  peripheral_serial_parity_mark,
  peripheral_serial_parity_space,
} PeripheralSerialParity;

// baud 0 means the card has selected no clock; stop_half_bits is 2, 3 or 4 so
// 1.5 stop bits are exact; dtr, rts and brk are 1 = asserted, as the kernel's
// TIOCM_DTR and TIOCM_RTS are.
typedef struct {
  uint32_t baud;
  uint8_t data_bits;
  uint8_t parity;
  uint8_t stop_half_bits;
  uint8_t dtr;
  uint8_t rts;
  uint8_t brk;
  uint8_t padding[2];
} PeripheralSerialLine;

// The II's keyboard encoder is on the keyboard (Apple II Reference Manual
// 1979, p. 102), the //e's on the main board (IIe Technical Reference Manual,
// p. 187), so a card behind the same addresses has to be told which it is in.
// Clones of the II Plus ROM are apple2_plus; every //e variant is apple2e.
typedef enum {
  peripheral_machine_apple2 = 0,
  peripheral_machine_apple2_plus = 1,
  peripheral_machine_apple2e = 2,
} PeripheralMachine;

enum IrqSrc {
  is_6522 = 0,
  is_speech,
  is_ssc,
  is_mouse,
  is_slot1,
  is_slot2,
  is_slot3,
  is_slot4,
  is_slot5,
  is_slot6,
  is_slot7,
};

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, readability-identifier-naming)
