// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// cppcoreguidelines-use-enum-class) Justification: This header defines
// C-compatible structures and types for the keyboard mapping system to ensure
// interoperability across different frontends.
// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using)

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  keyb_map_size = 128,  // Covers the physical scancode range (max index 82)
  keyb_name_size = 32
};

typedef enum {
  keyb_idx_unknown = 0,
  keyb_idx_a = 4,
  keyb_idx_b = 5,
  keyb_idx_c = 6,
  keyb_idx_d = 7,
  keyb_idx_e = 8,
  keyb_idx_f = 9,
  keyb_idx_g = 10,
  keyb_idx_h = 11,
  keyb_idx_i = 12,
  keyb_idx_j = 13,
  keyb_idx_k = 14,
  keyb_idx_l = 15,
  keyb_idx_m = 16,
  keyb_idx_n = 17,
  keyb_idx_o = 18,
  keyb_idx_p = 19,
  keyb_idx_q = 20,
  keyb_idx_r = 21,
  keyb_idx_s = 22,
  keyb_idx_t = 23,
  keyb_idx_u = 24,
  keyb_idx_v = 25,
  keyb_idx_w = 26,
  keyb_idx_x = 27,
  keyb_idx_y = 28,
  keyb_idx_z = 29,

  keyb_idx_1 = 30,
  keyb_idx_2 = 31,
  keyb_idx_3 = 32,
  keyb_idx_4 = 33,
  keyb_idx_5 = 34,
  keyb_idx_6 = 35,
  keyb_idx_7 = 36,
  keyb_idx_8 = 37,
  keyb_idx_9 = 38,
  keyb_idx_0 = 39,

  keyb_idx_return = 40,
  keyb_idx_escape = 41,
  keyb_idx_backspace = 42,
  keyb_idx_tab = 43,
  keyb_idx_space = 44,

  keyb_idx_minus = 45,
  keyb_idx_equals = 46,
  keyb_idx_leftbracket = 47,
  keyb_idx_rightbracket = 48,
  keyb_idx_backslash = 49,
  keyb_idx_semicolon = 51,
  keyb_idx_apostrophe = 52,
  keyb_idx_grave = 53,
  keyb_idx_comma = 54,
  keyb_idx_period = 55,
  keyb_idx_slash = 56,

  keyb_idx_capslock = 57,

  keyb_idx_f1 = 58,
  keyb_idx_f2 = 59,
  keyb_idx_f3 = 60,
  keyb_idx_f4 = 61,
  keyb_idx_f5 = 62,
  keyb_idx_f6 = 63,
  keyb_idx_f7 = 64,
  keyb_idx_f8 = 65,
  keyb_idx_f9 = 66,
  keyb_idx_f10 = 67,
  keyb_idx_f11 = 68,
  keyb_idx_f12 = 69,

  keyb_idx_right = 79,
  keyb_idx_left = 80,
  keyb_idx_down = 81,
  keyb_idx_up = 82
} KeyboardIdx_t;

typedef struct {
  char name[keyb_name_size];
  uint8_t map[keyb_map_size];
  uint8_t shift_map[keyb_map_size];
  uint8_t ctrl_map[keyb_map_size];
} Apple2KeyboardMap_t;

extern const Apple2KeyboardMap_t map_us;
extern const Apple2KeyboardMap_t map_uk;
extern const Apple2KeyboardMap_t map_fr;
extern const Apple2KeyboardMap_t map_de;
extern const Apple2KeyboardMap_t map_es;
extern const Apple2KeyboardMap_t map_it;
extern const Apple2KeyboardMap_t map_se;
extern const Apple2KeyboardMap_t map_dk;
extern const Apple2KeyboardMap_t map_ch;
extern const Apple2KeyboardMap_t map_ca;
extern const Apple2KeyboardMap_t map_jp_roman;
extern const Apple2KeyboardMap_t map_jp_kana;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using)
