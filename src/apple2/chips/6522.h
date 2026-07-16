// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

typedef struct {
  union {
    struct {
      uint8_t l;
      uint8_t h;
    };
    uint16_t w;
  };
} IWord_t;

typedef struct {
  uint8_t ORB;
  uint8_t ORA;
  uint8_t DDRB;
  uint8_t DDRA;
  IWord_t TIMER1_COUNTER;
  IWord_t TIMER1_LATCH;
  IWord_t TIMER2_COUNTER;
  IWord_t TIMER2_LATCH;
  uint8_t SERIAL_SHIFT;
  uint8_t ACR;
  uint8_t PCR;
  uint8_t IFR;
  uint8_t IER;
  uint8_t ORA_NO_HS;
} Sy6522_t;
