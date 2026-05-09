/*
 * Keyboard_Maps.h - Shared Keyboard Mapping Data
 *
 * Defines hardware encoder layouts for various locales.
 * C-compatible for use in all LinApple frontends.
 */

#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
// cppcoreguidelines-use-enum-class) Justification: This header defines
// C-compatible structures and types for the keyboard mapping system to ensure
// interoperability across different frontends.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  KEYB_MAP_SIZE = 128,  // Covers the physical scancode range (max index 82)
  KEYB_NAME_SIZE = 32
};

/**
 * @brief Positional Indices for the hardware encoder maps.
 * These are physical locations on a standard 101/102-key keyboard.
 */
typedef enum {
  KEYB_IDX_UNKNOWN = 0,
  KEYB_IDX_A = 4,
  KEYB_IDX_B = 5,
  KEYB_IDX_C = 6,
  KEYB_IDX_D = 7,
  KEYB_IDX_E = 8,
  KEYB_IDX_F = 9,
  KEYB_IDX_G = 10,
  KEYB_IDX_H = 11,
  KEYB_IDX_I = 12,
  KEYB_IDX_J = 13,
  KEYB_IDX_K = 14,
  KEYB_IDX_L = 15,
  KEYB_IDX_M = 16,
  KEYB_IDX_N = 17,
  KEYB_IDX_O = 18,
  KEYB_IDX_P = 19,
  KEYB_IDX_Q = 20,
  KEYB_IDX_R = 21,
  KEYB_IDX_S = 22,
  KEYB_IDX_T = 23,
  KEYB_IDX_U = 24,
  KEYB_IDX_V = 25,
  KEYB_IDX_W = 26,
  KEYB_IDX_X = 27,
  KEYB_IDX_Y = 28,
  KEYB_IDX_Z = 29,

  KEYB_IDX_1 = 30,
  KEYB_IDX_2 = 31,
  KEYB_IDX_3 = 32,
  KEYB_IDX_4 = 33,
  KEYB_IDX_5 = 34,
  KEYB_IDX_6 = 35,
  KEYB_IDX_7 = 36,
  KEYB_IDX_8 = 37,
  KEYB_IDX_9 = 38,
  KEYB_IDX_0 = 39,

  KEYB_IDX_RETURN = 40,
  KEYB_IDX_ESCAPE = 41,
  KEYB_IDX_BACKSPACE = 42,
  KEYB_IDX_TAB = 43,
  KEYB_IDX_SPACE = 44,

  KEYB_IDX_MINUS = 45,
  KEYB_IDX_EQUALS = 46,
  KEYB_IDX_LEFTBRACKET = 47,
  KEYB_IDX_RIGHTBRACKET = 48,
  KEYB_IDX_BACKSLASH = 49,
  KEYB_IDX_SEMICOLON = 51,
  KEYB_IDX_APOSTROPHE = 52,
  KEYB_IDX_GRAVE = 53,
  KEYB_IDX_COMMA = 54,
  KEYB_IDX_PERIOD = 55,
  KEYB_IDX_SLASH = 56,

  KEYB_IDX_CAPSLOCK = 57,

  KEYB_IDX_F1 = 58,
  KEYB_IDX_F2 = 59,
  KEYB_IDX_F3 = 60,
  KEYB_IDX_F4 = 61,
  KEYB_IDX_F5 = 62,
  KEYB_IDX_F6 = 63,
  KEYB_IDX_F7 = 64,
  KEYB_IDX_F8 = 65,
  KEYB_IDX_F9 = 66,
  KEYB_IDX_F10 = 67,
  KEYB_IDX_F11 = 68,
  KEYB_IDX_F12 = 69,

  KEYB_IDX_RIGHT = 79,
  KEYB_IDX_LEFT = 80,
  KEYB_IDX_DOWN = 81,
  KEYB_IDX_UP = 82
} KeyboardIdx_e;

typedef struct {
  char name[KEYB_NAME_SIZE];
  uint8_t map[KEYB_MAP_SIZE];
} Apple2KeyboardMap_t;

extern const Apple2KeyboardMap_t Map_US;
extern const Apple2KeyboardMap_t Map_FR;
extern const Apple2KeyboardMap_t Map_DE;
extern const Apple2KeyboardMap_t Map_JP_Roman;
extern const Apple2KeyboardMap_t Map_JP_Kana;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
// cppcoreguidelines-use-enum-class)
