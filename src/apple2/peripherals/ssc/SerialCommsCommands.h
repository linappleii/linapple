/*
 * SerialCommsCommands.h - LinApple SSC Peripheral Command Interface
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  /**
   * @brief Push a byte received from the host into the card's RX buffer.
   * Payload: uint8_t
   */
  SSC_CMD_PUSH_RX_BYTE = 0x0001
} SerialCommsCmd_e;

#ifdef __cplusplus
}
#endif
