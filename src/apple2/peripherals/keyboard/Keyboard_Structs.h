#pragma once

#include <cstdint>

/**
 * @brief Snapshot of the keyboard state for save-files.
 */
struct SS_IO_Keyboard {
  uint8_t last_key = 0;
};

/**
 * @brief Encapsulates the state of the Apple II keyboard hardware.
 */
struct Keyboard_t {
  // --- Register State ---
  uint8_t last_key = 0;
  uint32_t keys_down_count = 0;  // Physical counter for Bit 7 of $C010
  bool caps_lock = true;

  // --- Modifiers ---
  bool shift_key = false;
  bool ctrl_key = false;
  bool alt_key = false;
  bool gui_key = false;

  // --- Auto-repeat State ---
  uint32_t repeat_key = 0;
  uint32_t repeat_scancode = 0;
  uint32_t repeat_delay_cycles = 0;
  bool repeating = false;
};
