// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/peripherals/keyboard/Keyboard_Maps.h"

// NOLINTBEGIN(modernize-use-using, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-constant-array-index,
// modernize-use-designated-initializers) Justification: This file implements
// C99-compatible structures and types for the keyboard mapping system.

const Apple2KeyboardMap_t map_us = {
    "US",
    {[keyb_idx_a] = 'a',           [keyb_idx_b] = 'b',
     [keyb_idx_c] = 'c',           [keyb_idx_d] = 'd',
     [keyb_idx_e] = 'e',           [keyb_idx_f] = 'f',
     [keyb_idx_g] = 'g',           [keyb_idx_h] = 'h',
     [keyb_idx_i] = 'i',           [keyb_idx_j] = 'j',
     [keyb_idx_k] = 'k',           [keyb_idx_l] = 'l',
     [keyb_idx_m] = 'm',           [keyb_idx_n] = 'n',
     [keyb_idx_o] = 'o',           [keyb_idx_p] = 'p',
     [keyb_idx_q] = 'q',           [keyb_idx_r] = 'r',
     [keyb_idx_s] = 's',           [keyb_idx_t] = 't',
     [keyb_idx_u] = 'u',           [keyb_idx_v] = 'v',
     [keyb_idx_w] = 'w',           [keyb_idx_x] = 'x',
     [keyb_idx_y] = 'y',           [keyb_idx_z] = 'z',

     [keyb_idx_1] = '1',           [keyb_idx_2] = '2',
     [keyb_idx_3] = '3',           [keyb_idx_4] = '4',
     [keyb_idx_5] = '5',           [keyb_idx_6] = '6',
     [keyb_idx_7] = '7',           [keyb_idx_8] = '8',
     [keyb_idx_9] = '9',           [keyb_idx_0] = '0',

     [keyb_idx_return] = 0x0D,     [keyb_idx_escape] = 0x1B,
     [keyb_idx_backspace] = 0x7F,  [keyb_idx_tab] = 0x09,
     [keyb_idx_space] = 0x20,

     [keyb_idx_minus] = '-',       [keyb_idx_equals] = '=',
     [keyb_idx_leftbracket] = '[', [keyb_idx_rightbracket] = ']',
     [keyb_idx_backslash] = '\\',  [keyb_idx_semicolon] = ';',
     [keyb_idx_apostrophe] = '\'', [keyb_idx_grave] = '`',
     [keyb_idx_comma] = ',',       [keyb_idx_period] = '.',
     [keyb_idx_slash] = '/',

     [keyb_idx_up] = 0x0B,         [keyb_idx_down] = 0x0A,
     [keyb_idx_left] = 0x08,       [keyb_idx_right] = 0x15},

    {[keyb_idx_a] = 'A',           [keyb_idx_b] = 'B',
     [keyb_idx_c] = 'C',           [keyb_idx_d] = 'D',
     [keyb_idx_e] = 'E',           [keyb_idx_f] = 'F',
     [keyb_idx_g] = 'G',           [keyb_idx_h] = 'H',
     [keyb_idx_i] = 'I',           [keyb_idx_j] = 'J',
     [keyb_idx_k] = 'K',           [keyb_idx_l] = 'L',
     [keyb_idx_m] = 'M',           [keyb_idx_n] = 'N',
     [keyb_idx_o] = 'O',           [keyb_idx_p] = 'P',
     [keyb_idx_q] = 'Q',           [keyb_idx_r] = 'R',
     [keyb_idx_s] = 'S',           [keyb_idx_t] = 'T',
     [keyb_idx_u] = 'U',           [keyb_idx_v] = 'V',
     [keyb_idx_w] = 'W',           [keyb_idx_x] = 'X',
     [keyb_idx_y] = 'Y',           [keyb_idx_z] = 'Z',

     [keyb_idx_1] = '!',           [keyb_idx_2] = '@',
     [keyb_idx_3] = '#',           [keyb_idx_4] = '$',
     [keyb_idx_5] = '%',           [keyb_idx_6] = '^',
     [keyb_idx_7] = '&',           [keyb_idx_8] = '*',
     [keyb_idx_9] = '(',           [keyb_idx_0] = ')',

     [keyb_idx_minus] = '_',       [keyb_idx_equals] = '+',
     [keyb_idx_leftbracket] = '{', [keyb_idx_rightbracket] = '}',
     [keyb_idx_backslash] = '|',   [keyb_idx_semicolon] = ':',
     [keyb_idx_apostrophe] = '"',  [keyb_idx_grave] = '~',
     [keyb_idx_comma] = '<',       [keyb_idx_period] = '>',
     [keyb_idx_slash] = '?'},

    {[keyb_idx_a] = 0x01,
     [keyb_idx_b] = 0x02,
     [keyb_idx_c] = 0x03,
     [keyb_idx_d] = 0x04,
     [keyb_idx_e] = 0x05,
     [keyb_idx_f] = 0x06,
     [keyb_idx_g] = 0x07,
     [keyb_idx_h] = 0x08,
     [keyb_idx_i] = 0x09,
     [keyb_idx_j] = 0x0A,
     [keyb_idx_k] = 0x0B,
     [keyb_idx_l] = 0x0C,
     [keyb_idx_m] = 0x0D,
     [keyb_idx_n] = 0x0E,
     [keyb_idx_o] = 0x0F,
     [keyb_idx_p] = 0x10,
     [keyb_idx_q] = 0x11,
     [keyb_idx_r] = 0x12,
     [keyb_idx_s] = 0x13,
     [keyb_idx_t] = 0x14,
     [keyb_idx_u] = 0x15,
     [keyb_idx_v] = 0x16,
     [keyb_idx_w] = 0x17,
     [keyb_idx_x] = 0x18,
     [keyb_idx_y] = 0x19,
     [keyb_idx_z] = 0x1A,

     [keyb_idx_leftbracket] = 0x1B,
     [keyb_idx_backslash] = 0x1C,
     [keyb_idx_rightbracket] = 0x1D,
     [keyb_idx_grave] = 0x1E,
     [keyb_idx_slash] = 0x1F}};

// UK layout is identical to US but produces £ (0x23) instead of # (0x23) in the
// UK Video ROM.
const Apple2KeyboardMap_t map_uk = {"UK", {0}, {0}, {0}};

const Apple2KeyboardMap_t map_fr = {
    "French",
    {[keyb_idx_q] = 'a',
     [keyb_idx_a] = 'q',
     [keyb_idx_w] = 'z',
     [keyb_idx_z] = 'w',
     [keyb_idx_m] = ',',
     [keyb_idx_comma] = 'm',
     [keyb_idx_semicolon] = 'm',
     [keyb_idx_1] = '&',
     [keyb_idx_2] = 0x7B,  // é
     [keyb_idx_3] = '\"',
     [keyb_idx_4] = '\'',
     [keyb_idx_5] = '(',
     [keyb_idx_6] = '-',
     [keyb_idx_7] = 0x7D,  // è
     [keyb_idx_8] = '_',
     [keyb_idx_9] = 0x5C,  // ç
     [keyb_idx_0] = 0x40,  // à
     [keyb_idx_minus] = ')',
     [keyb_idx_equals] = '=',
     [keyb_idx_leftbracket] = 0x5B,  // °
     [keyb_idx_rightbracket] = '+',
     [keyb_idx_backslash] = '*',
     [keyb_idx_apostrophe] = 0x7E,  // ù
     [keyb_idx_grave] = 0x23,       // £
     [keyb_idx_slash] = '!'},
    {[keyb_idx_1] = '1',
     [keyb_idx_2] = '2',
     [keyb_idx_3] = '3',
     [keyb_idx_4] = '4',
     [keyb_idx_5] = '5',
     [keyb_idx_6] = '6',
     [keyb_idx_7] = '7',
     [keyb_idx_8] = '8',
     [keyb_idx_9] = '9',
     [keyb_idx_0] = '0',
     [keyb_idx_minus] = 0x5D,  // §
     [keyb_idx_slash] = '?'},
    {0}  // Standard bitmask Ctrl suffices
};

const Apple2KeyboardMap_t map_de = {
    "German",
    {[keyb_idx_y] = 'z',
     [keyb_idx_z] = 'y',
     [keyb_idx_leftbracket] = 0x5B,   // Ä
     [keyb_idx_backslash] = 0x5C,     // Ö
     [keyb_idx_rightbracket] = 0x5D,  // Ü
     [keyb_idx_grave] = 0x5E,         // ^
     [keyb_idx_minus] = 0x7E,         // ß
     [keyb_idx_slash] = '-'},
    {[keyb_idx_leftbracket] = 'A',  // Uppercase mapped to the same code point
     [keyb_idx_backslash] = 'O',  // hardware handles capitalization of umlauts
     [keyb_idx_rightbracket] = 'U',  // via the same ROM entry usually
     [keyb_idx_minus] = '?'},
    {0}};

const Apple2KeyboardMap_t map_es = {"Spanish",
                                    {[keyb_idx_semicolon] = 0x5C,     // Ñ
                                     [keyb_idx_leftbracket] = 0x5B,   // ¡
                                     [keyb_idx_rightbracket] = 0x5D,  // ¿
                                     [keyb_idx_grave] = 0x23,         // £
                                     [keyb_idx_backslash] = 0x7D,     // ç
                                     [keyb_idx_minus] = '\''},
                                    {[keyb_idx_semicolon] = 0x7C,  // ñ
                                     [keyb_idx_minus] = '?'},
                                    {0}};

const Apple2KeyboardMap_t map_it = {"Italian",
                                    {
                                        [keyb_idx_leftbracket] = 0x5B,   // °
                                        [keyb_idx_rightbracket] = 0x5D,  // é
                                        [keyb_idx_backslash] = 0x5C,     // ç
                                        [keyb_idx_grave] = 0x60,         // ù
                                        [keyb_idx_semicolon] = 0x7B,     // à
                                        [keyb_idx_apostrophe] = 0x7C,    // ò
                                        [keyb_idx_minus] = 0x7D,         // è
                                        [keyb_idx_slash] = 0x7E          // ì
                                    },
                                    {0},
                                    {0}};

const Apple2KeyboardMap_t map_se = {"Swedish",
                                    {
                                        [keyb_idx_leftbracket] = 0x7D,  // å
                                        [keyb_idx_semicolon] = 0x7C,    // ö
                                        [keyb_idx_apostrophe] = 0x7B    // ä
                                    },
                                    {
                                        [keyb_idx_leftbracket] = 0x5D,  // Å
                                        [keyb_idx_semicolon] = 0x5C,    // Ö
                                        [keyb_idx_apostrophe] = 0x5B    // Ä
                                    },
                                    {0}};

const Apple2KeyboardMap_t map_dk = {"Danish",
                                    {
                                        [keyb_idx_leftbracket] = 0x7D,  // å
                                        [keyb_idx_semicolon] = 0x7B,    // æ
                                        [keyb_idx_apostrophe] = 0x7C    // ø
                                    },
                                    {
                                        [keyb_idx_leftbracket] = 0x5D,  // Å
                                        [keyb_idx_semicolon] = 0x5B,    // Æ
                                        [keyb_idx_apostrophe] = 0x5C    // Ø
                                    },
                                    {0}};

const Apple2KeyboardMap_t map_ch = {"Swiss",
                                    {
                                        [keyb_idx_y] = 'z',
                                        [keyb_idx_z] = 'y',
                                        [keyb_idx_leftbracket] = 0x7D,  // ü
                                        [keyb_idx_semicolon] = 0x5B,    // é
                                        [keyb_idx_apostrophe] = 0x7B    // ä
                                    },
                                    {
                                        [keyb_idx_leftbracket] = 0x5D,  // ê
                                        [keyb_idx_semicolon] = 0x5C,    // ç
                                        [keyb_idx_apostrophe] = 0x7C    // ö
                                    },
                                    {0}};

const Apple2KeyboardMap_t map_ca = {
    "Canadian French",
    {
        [keyb_idx_semicolon] = 0x7B,   // é
        [keyb_idx_apostrophe] = 0x7D,  // è
        [keyb_idx_grave] = 0x40        // à
    },
    {[keyb_idx_semicolon] = 'E',  // capitalized dynamically or mapped
     [keyb_idx_apostrophe] = 'E'},
    {0}};

// Japanese Roman layout (Standard QWERTY)
const Apple2KeyboardMap_t map_jp_roman = {"Japanese (Roman)", {0}, {0}, {0}};

// Japanese Kana layout (Standard JIS layout translation codes)
const Apple2KeyboardMap_t map_jp_kana = {
    "Japanese (Kana)",
    {[keyb_idx_a] = 't', [keyb_idx_b] = 'c', [keyb_idx_c] = 's',
     [keyb_idx_d] = 's', [keyb_idx_e] = 'i', [keyb_idx_f] = 'h',
     [keyb_idx_g] = 'k', [keyb_idx_h] = 'm', [keyb_idx_i] = 'y',
     [keyb_idx_j] = 'n', [keyb_idx_k] = 'n', [keyb_idx_l] = 'r',
     [keyb_idx_m] = 'm', [keyb_idx_n] = 'm', [keyb_idx_o] = 'r',
     [keyb_idx_p] = 's', [keyb_idx_q] = 't', [keyb_idx_r] = 's',
     [keyb_idx_s] = 't', [keyb_idx_t] = 'k', [keyb_idx_u] = 'n',
     [keyb_idx_v] = 'h', [keyb_idx_w] = 't', [keyb_idx_x] = 's',
     [keyb_idx_y] = 'n', [keyb_idx_z] = 't'},
    {0},
    {0}};

// NOLINTEND(modernize-use-using, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-constant-array-index,
// modernize-use-designated-initializers)
