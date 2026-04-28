/*
 * KeyboardCommands.h - LinApple Keyboard Peripheral Command Interface
 *
 * This header defines the binary interface for keyboard commands.
 * It is C-compatible for use in all LinApple frontends.
 */

#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
// cppcoreguidelines-use-enum-class) Justification: This header defines a
// C-compatible binary interface for the keyboard command and query system,
// requiring C-style headers, structs, and enums.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  /**
   * @brief Report a key event (press or release).
   * Payload: KeyboardEvent_t
   */
  KEYB_CMD_EVENT = 0x0001,

  /**
   * @brief Set the absolute state of the Caps Lock latch.
   * Payload: uint8_t (0 = OFF, 1 = ON)
   */
  KEYB_CMD_SET_CAPS = 0x0002,

  /**
   * @brief Set the absolute state of the keyboard rocker switch (Local vs US).
   * Payload: uint8_t (0 = US, 1 = Local)
   */
  KEYB_CMD_SET_ROCKER = 0x0003,

  /**
   * @brief Set absolute modifier states (Shift, Ctrl, Alt/Apple).
   * Payload: KeyboardModifiers_t
   */
  KEYB_CMD_SET_MODS = 0x0004
} KeyboardCmd_e;

typedef enum {
  /**
   * @brief Query current modifier key states.
   * Out: KeyboardModifiers_t
   */
  KEYB_QUERY_MODS = 0x0001,

  /**
   * @brief Query the current rocker switch state.
   * Out: uint8_t (0 = US, 1 = Local)
   */
  KEYB_QUERY_ROCKER = 0x0002
} KeyboardQuery_e;

#pragma pack(push, 1)

/**
 * @brief Payload for KEYB_CMD_SET_MODS and out-buffer for KEYB_QUERY_MODS
 */
typedef struct {
  uint8_t shift;
  uint8_t ctrl;
  uint8_t alt;   // Closed Apple
  uint8_t gui;   // Open Apple
  uint8_t caps;  // Caps Lock (read-only in KEYB_QUERY_MODS; ignored in
                 // KEYB_CMD_SET_MODS)
} KeyboardModifiers_t;

/**
 * @brief Payload for KEYB_CMD_EVENT
 */
typedef struct {
  uint8_t ascii;    // The Apple II ASCII code (0-127)
  uint8_t is_down;  // 1 if pressed, 0 if released
} KeyboardEvent_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
// cppcoreguidelines-use-enum-class)
