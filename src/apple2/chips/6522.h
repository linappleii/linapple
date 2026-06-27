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
} IWORD;

typedef struct {
  uint8_t ORB;
  uint8_t ORA;
  uint8_t DDRB;
  uint8_t DDRA;
  IWORD TIMER1_COUNTER;
  IWORD TIMER1_LATCH;
  IWORD TIMER2_COUNTER;
  IWORD TIMER2_LATCH;
  uint8_t SERIAL_SHIFT;
  uint8_t ACR;
  uint8_t PCR;
  uint8_t IFR;
  uint8_t IER;
  uint8_t ORA_NO_HS;
} SY6522;
