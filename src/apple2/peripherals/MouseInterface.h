#ifndef MOUSEINTERFACE_H
#define MOUSEINTERFACE_H

#include <cstdint>

#include "apple2/chips/6821.h"
#include "core/Common.h"

struct MouseInterface {
  Pia6821 m_6821;

  int m_nDataLen;
  uint8_t m_byMode;

  uint8_t m_by6821B;
  uint8_t m_by6821A;
  uint8_t m_byBuff[8];  // m_byBuff[0] is mode byte
  int m_nBuffPos;

  uint8_t m_byState;
  int m_nX;
  int m_nY;
  bool m_bBtn0;
  bool m_bBtn1;

  bool m_bVBL;

  uint32_t m_iX;
  uint32_t m_iRangeX;
  uint32_t m_iMinX;
  uint32_t m_iMaxX;
  uint32_t m_iY;
  uint32_t m_iRangeY;
  uint32_t m_iMinY;
  uint32_t m_iMaxY;

  bool m_bButtons[2];

  bool m_bActive;
  uint8_t* m_pSlotRom;
  uint32_t m_uSlot;
};

void Mouse_SetSlotRom();
void Mouse_SetPosition(int xvalue, int xrange, int yvalue, int yrange);
void Mouse_SetButton(eBUTTON Button, eBUTTONSTATE State);
bool Mouse_Active();
void Mouse_SetVBlank(bool bVBL);

extern struct MouseInterface sg_Mouse;

#endif
