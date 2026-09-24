// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)

/* Command and query ids are dispatched inside a slot, and slot 0 holds every
 * built-in peripheral at once - the keyboard, the joystick and the speaker.
 * Small per-peripheral ids therefore collided: keyboard_cmd_set_rocker and
 * JOY_CMD_RESET were both 3, and only the payload size told them apart.
 *
 * The high 16 bits of an id name the subsystem that owns it and the low 16
 * are the index within that subsystem, so the ids cannot collide at all. A
 * dispatcher rejects a foreign subsystem before it looks at anything else.
 *
 * GENERIC is for ids every peripheral may be offered, such as
 * PERIPHERAL_QUERY_AUDIO_INFO, which both the speaker and the Mockingboard
 * answer.
 *
 * This lives apart from Peripheral_Types.h because every command header needs
 * it and Peripheral_Types.h includes five of those headers back. */
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

/* True when a dispatcher should look at cmd_id at all: it is either one of
 * this peripheral's own ids or a generic one. Anything else belongs to
 * another peripheral in the slot and must be answered with
 * peripheral_incompatible, never peripheral_error - peripheral_query() stops
 * the search at the first status that is not incompatible. */
static inline bool peripheral_cmd_is_mine(uint32_t cmd_id, uint32_t subsystem) {
  const uint32_t owner = cmd_id & (uint32_t)PERIPHERAL_SUBSYSTEM_MASK;
  return owner == subsystem || owner == (uint32_t)PERIPHERAL_SUBSYSTEM_GENERIC;
}

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)

#ifdef __cplusplus
}
#endif
