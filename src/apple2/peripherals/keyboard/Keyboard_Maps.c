/*
 * Keyboard_Maps.c - Shared Keyboard Mapping Implementation
 */

#include "apple2/peripherals/keyboard/Keyboard_Maps.h"

// US Layout hardware encoder mapping (un-shifted ASCII)
const Apple2KeyboardMap_t Map_US = {
    "US", {[KEYB_IDX_A] = 'a',           [KEYB_IDX_B] = 'b',
           [KEYB_IDX_C] = 'c',           [KEYB_IDX_D] = 'd',
           [KEYB_IDX_E] = 'e',           [KEYB_IDX_F] = 'f',
           [KEYB_IDX_G] = 'g',           [KEYB_IDX_H] = 'h',
           [KEYB_IDX_I] = 'i',           [KEYB_IDX_J] = 'j',
           [KEYB_IDX_K] = 'k',           [KEYB_IDX_L] = 'l',
           [KEYB_IDX_M] = 'm',           [KEYB_IDX_N] = 'n',
           [KEYB_IDX_O] = 'o',           [KEYB_IDX_P] = 'p',
           [KEYB_IDX_Q] = 'q',           [KEYB_IDX_R] = 'r',
           [KEYB_IDX_S] = 's',           [KEYB_IDX_T] = 't',
           [KEYB_IDX_U] = 'u',           [KEYB_IDX_V] = 'v',
           [KEYB_IDX_W] = 'w',           [KEYB_IDX_X] = 'x',
           [KEYB_IDX_Y] = 'y',           [KEYB_IDX_Z] = 'z',

           [KEYB_IDX_1] = '1',           [KEYB_IDX_2] = '2',
           [KEYB_IDX_3] = '3',           [KEYB_IDX_4] = '4',
           [KEYB_IDX_5] = '5',           [KEYB_IDX_6] = '6',
           [KEYB_IDX_7] = '7',           [KEYB_IDX_8] = '8',
           [KEYB_IDX_9] = '9',           [KEYB_IDX_0] = '0',

           [KEYB_IDX_RETURN] = 0x0D,     [KEYB_IDX_ESCAPE] = 0x1B,
           [KEYB_IDX_BACKSPACE] = 0x7F,  [KEYB_IDX_TAB] = 0x09,
           [KEYB_IDX_SPACE] = 0x20,

           [KEYB_IDX_MINUS] = '-',       [KEYB_IDX_EQUALS] = '=',
           [KEYB_IDX_LEFTBRACKET] = '[', [KEYB_IDX_RIGHTBRACKET] = ']',
           [KEYB_IDX_BACKSLASH] = '\\',  [KEYB_IDX_SEMICOLON] = ';',
           [KEYB_IDX_APOSTROPHE] = '\'', [KEYB_IDX_GRAVE] = '`',
           [KEYB_IDX_COMMA] = ',',       [KEYB_IDX_PERIOD] = '.',
           [KEYB_IDX_SLASH] = '/',

           [KEYB_IDX_UP] = 0x0B,         [KEYB_IDX_DOWN] = 0x0A,
           [KEYB_IDX_LEFT] = 0x08,       [KEYB_IDX_RIGHT] = 0x15}};

// French Layout hardware encoder mapping (Physical position -> AZERTY symbol)
const Apple2KeyboardMap_t Map_FR = {
    "French",
    {[KEYB_IDX_A] = 'q',
     [KEYB_IDX_B] = 'b',
     [KEYB_IDX_C] = 'c',
     [KEYB_IDX_D] = 'd',
     [KEYB_IDX_E] = 'e',
     [KEYB_IDX_F] = 'f',
     [KEYB_IDX_G] = 'g',
     [KEYB_IDX_H] = 'h',
     [KEYB_IDX_I] = 'i',
     [KEYB_IDX_M] = ',',
     [KEYB_IDX_N] = 'n',
     [KEYB_IDX_O] = 'o',
     [KEYB_IDX_P] = 'p',
     [KEYB_IDX_Q] = 'a',
     [KEYB_IDX_R] = 'r',
     [KEYB_IDX_S] = 's',
     [KEYB_IDX_T] = 't',
     [KEYB_IDX_U] = 'u',
     [KEYB_IDX_V] = 'v',
     [KEYB_IDX_W] = 'z',
     [KEYB_IDX_X] = 'x',
     [KEYB_IDX_Y] = 'y',
     [KEYB_IDX_Z] = 'w',

     // AZERTY accented number-row chars (é, è, ç, à).
     // On a real French Apple IIe, these keys sent 7-bit codes that the
     // French CGROM would display as accented characters.
     [KEYB_IDX_1] = '&',
     [KEYB_IDX_2] = 0x7B,  // é (mapped to '{' position)
     [KEYB_IDX_3] = '"',
     [KEYB_IDX_4] = '\'',
     [KEYB_IDX_5] = '(',
     [KEYB_IDX_6] = '-',
     [KEYB_IDX_7] = 0x7D,  // è (mapped to '}' position)
     [KEYB_IDX_8] = '_',
     [KEYB_IDX_9] = 0x5C,  // ç (mapped to '\' position)
     [KEYB_IDX_0] = 0x40,  // à (mapped to '@' position)

     [KEYB_IDX_RETURN] = 0x0D,
     [KEYB_IDX_ESCAPE] = 0x1B,
     [KEYB_IDX_BACKSPACE] = 0x7F,
     [KEYB_IDX_SPACE] = 0x20,
     [KEYB_IDX_UP] = 0x0B,
     [KEYB_IDX_DOWN] = 0x0A,
     [KEYB_IDX_LEFT] = 0x08,
     [KEYB_IDX_RIGHT] = 0x15}};

const Apple2KeyboardMap_t Map_DE = {
    "German", {[KEYB_IDX_A] = 'a',          [KEYB_IDX_B] = 'b',
               [KEYB_IDX_C] = 'c',          [KEYB_IDX_D] = 'd',
               [KEYB_IDX_E] = 'e',          [KEYB_IDX_F] = 'f',
               [KEYB_IDX_G] = 'g',          [KEYB_IDX_H] = 'h',
               [KEYB_IDX_I] = 'i',          [KEYB_IDX_J] = 'j',
               [KEYB_IDX_K] = 'k',          [KEYB_IDX_L] = 'l',
               [KEYB_IDX_M] = 'm',          [KEYB_IDX_N] = 'n',
               [KEYB_IDX_O] = 'o',          [KEYB_IDX_P] = 'p',
               [KEYB_IDX_Q] = 'q',          [KEYB_IDX_R] = 'r',
               [KEYB_IDX_S] = 's',          [KEYB_IDX_T] = 't',
               [KEYB_IDX_U] = 'u',          [KEYB_IDX_V] = 'v',
               [KEYB_IDX_W] = 'w',          [KEYB_IDX_X] = 'x',
               [KEYB_IDX_Y] = 'z',          [KEYB_IDX_Z] = 'y',  // QWERTZ

               [KEYB_IDX_RETURN] = 0x0D,    [KEYB_IDX_ESCAPE] = 0x1B,
               [KEYB_IDX_BACKSPACE] = 0x7F, [KEYB_IDX_SPACE] = 0x20,
               [KEYB_IDX_UP] = 0x0B,        [KEYB_IDX_DOWN] = 0x0A,
               [KEYB_IDX_LEFT] = 0x08,      [KEYB_IDX_RIGHT] = 0x15}};

const Apple2KeyboardMap_t Map_JP_Roman = {
    "Japanese (Roman)", {[KEYB_IDX_A] = 'a',          [KEYB_IDX_B] = 'b',
                         [KEYB_IDX_C] = 'c',          [KEYB_IDX_D] = 'd',
                         [KEYB_IDX_E] = 'e',          [KEYB_IDX_F] = 'f',
                         [KEYB_IDX_G] = 'g',          [KEYB_IDX_H] = 'h',
                         [KEYB_IDX_I] = 'i',          [KEYB_IDX_J] = 'j',
                         [KEYB_IDX_K] = 'k',          [KEYB_IDX_L] = 'l',
                         [KEYB_IDX_M] = 'm',          [KEYB_IDX_N] = 'n',
                         [KEYB_IDX_O] = 'o',          [KEYB_IDX_P] = 'p',
                         [KEYB_IDX_Q] = 'q',          [KEYB_IDX_R] = 'r',
                         [KEYB_IDX_S] = 's',          [KEYB_IDX_T] = 't',
                         [KEYB_IDX_U] = 'u',          [KEYB_IDX_V] = 'v',
                         [KEYB_IDX_W] = 'w',          [KEYB_IDX_X] = 'x',
                         [KEYB_IDX_Y] = 'y',          [KEYB_IDX_Z] = 'z',

                         [KEYB_IDX_RETURN] = 0x0D,    [KEYB_IDX_ESCAPE] = 0x1B,
                         [KEYB_IDX_BACKSPACE] = 0x7F, [KEYB_IDX_SPACE] = 0x20,
                         [KEYB_IDX_UP] = 0x0B,        [KEYB_IDX_DOWN] = 0x0A,
                         [KEYB_IDX_LEFT] = 0x08,      [KEYB_IDX_RIGHT] = 0x15}};

const Apple2KeyboardMap_t Map_JP_Kana = {"Japanese (Kana)",
                                         {[KEYB_IDX_RETURN] = 0x0D,
                                          [KEYB_IDX_ESCAPE] = 0x1B,
                                          [KEYB_IDX_BACKSPACE] = 0x7F,
                                          [KEYB_IDX_SPACE] = 0x20,
                                          [KEYB_IDX_UP] = 0x0B,
                                          [KEYB_IDX_DOWN] = 0x0A,
                                          [KEYB_IDX_LEFT] = 0x08,
                                          [KEYB_IDX_RIGHT] = 0x15}};
