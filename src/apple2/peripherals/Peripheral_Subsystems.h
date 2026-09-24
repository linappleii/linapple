// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)

/* Upper 16 bits encode subsystem ID; lower 16 bits encode command index. */
typedef enum {
  PERIPHERAL_SUBSYSTEM_GENERIC = 0x00000000,
  PERIPHERAL_SUBSYSTEM_KEYBOARD = 0x00010000,
  PERIPHERAL_SUBSYSTEM_JOYSTICK = 0x00020000,
  PERIPHERAL_SUBSYSTEM_SPEAKER = 0x00030000,
  PERIPHERAL_SUBSYSTEM_DISK = 0x00040000,
  PERIPHERAL_SUBSYSTEM_HARDDISK = 0x00050000,
  PERIPHERAL_SUBSYSTEM_MOUSE = 0x00060000,
  PERIPHERAL_SUBSYSTEM_CLOCK = 0x00070000,
  PERIPHERAL_SUBSYSTEM_PRINTER = 0x00080000,
  PERIPHERAL_SUBSYSTEM_SERIAL = 0x00090000,
  PERIPHERAL_SUBSYSTEM_MOCKINGBOARD = 0x000A0000,
  PERIPHERAL_SUBSYSTEM_MASK = (int)0xFFFF0000,
  PERIPHERAL_COMMAND_INDEX_MASK = 0x0000FFFF
} PeripheralSubsystem_t;

/* Verify command ID belongs to this subsystem or generic namespace. */
static inline bool peripheral_cmd_is_mine(uint32_t cmd_id, uint32_t subsystem) {
  const uint32_t owner = cmd_id & (uint32_t)PERIPHERAL_SUBSYSTEM_MASK;
  return owner == subsystem || owner == (uint32_t)PERIPHERAL_SUBSYSTEM_GENERIC;
}

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)

#ifdef __cplusplus
}
#endif
