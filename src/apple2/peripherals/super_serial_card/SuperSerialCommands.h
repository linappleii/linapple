// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
//             cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification: This header defines a language-neutral C ABI. C system
// headers, typedefs, and C-style arrays are required for compatibility with
// C-based consumers.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

constexpr int SUPER_SERIAL_FIFO_SIZE = 9;

typedef enum {
  SUPER_SERIAL_BAUD_110 = 110,
  SUPER_SERIAL_BAUD_300 = 300,
  SUPER_SERIAL_BAUD_600 = 600,
  SUPER_SERIAL_BAUD_1200 = 1200,
  SUPER_SERIAL_BAUD_2400 = 2400,
  SUPER_SERIAL_BAUD_4800 = 4800,
  SUPER_SERIAL_BAUD_9600 = 9600,
  SUPER_SERIAL_BAUD_19200 = 19200
} SuperSerialBaudRate_t;

typedef enum {
  SUPER_SERIAL_BITS_5 = 5,
  SUPER_SERIAL_BITS_6 = 6,
  SUPER_SERIAL_BITS_7 = 7,
  SUPER_SERIAL_BITS_8 = 8
} SuperSerialByteSize_t;

typedef enum {
  SUPER_SERIAL_FIRMWARE_CIC = 0,
  SUPER_SERIAL_FIRMWARE_SIC_P8,
  SUPER_SERIAL_FIRMWARE_PPC,
  SUPER_SERIAL_FIRMWARE_SIC_P8A
} SuperSerialFirmwareMode_t;

typedef enum {
  SUPER_SERIAL_PARITY_NONE = 0,
  SUPER_SERIAL_PARITY_ODD = 1,
  SUPER_SERIAL_PARITY_EVEN = 2,
  SUPER_SERIAL_PARITY_MARK = 3,
  SUPER_SERIAL_PARITY_SPACE = 4
} SuperSerialParity_t;

typedef enum {
  SUPER_SERIAL_STOP_BITS_1 = 0,
  SUPER_SERIAL_STOP_BITS_1_5 = 1,
  SUPER_SERIAL_STOP_BITS_2 = 2
} SuperSerialStopBits_t;

typedef struct {
  SuperSerialBaudRate_t baud_rate;
  SuperSerialFirmwareMode_t firmware_mode;
  SuperSerialStopBits_t stop_bits;
  SuperSerialByteSize_t byte_size;
  SuperSerialParity_t parity;
  bool linefeed;
  bool interrupts;
} SuperSerialDipSwConfig_t;

typedef enum {
  SUPER_SERIAL_CMD_PUSH_RX_BYTE = 0x0001,
  SUPER_SERIAL_CMD_SET_CONFIG = 0x0002
} SuperSerialCmd_t;

typedef enum {
  SUPER_SERIAL_QUERY_CONFIG = 0x0001,
  SUPER_SERIAL_QUERY_RX_READY = 0x0002
} SuperSerialQuery_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
//           cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
