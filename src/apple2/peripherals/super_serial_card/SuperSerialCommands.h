// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification:
// This header defines a language-neutral C ABI. C system headers, typedefs, and
// C-style arrays are required for compatibility with C-based consumers.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  SUPER_SERIAL_STATE_VERSION = 1,
  SUPER_SERIAL_FIFO_SIZE = 9,
  super_serial_default_slot = 2
};

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
  uint8_t padding[2];
} SuperSerialDipSwConfig_t;

typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint32_t rx_count;
  uint8_t control_byte;
  uint8_t command_byte;
  uint8_t is_irq_pending;
  uint8_t is_rx_irq_enabled;
  uint8_t is_tx_irq_enabled;
  uint8_t was_tx_written;
  uint8_t rx_buffer[SUPER_SERIAL_FIFO_SIZE];
  uint8_t reserved0;
  SuperSerialDipSwConfig_t config;
  uint8_t reserved1[4];
} SuperSerialSaveState_t;

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

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
