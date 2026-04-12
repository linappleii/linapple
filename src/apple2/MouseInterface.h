#ifndef MOUSEINTERFACE_H
#define MOUSEINTERFACE_H

#include <cstdint>
#include "apple2/6821.h"
#include "core/Common.h"

struct MouseInterface {
  Pia6821 m_6821;

  int m_nDataLen;
  unsigned char m_byMode;

  unsigned char m_by6821B;
  unsigned char m_by6821A;
  unsigned char m_byBuff[8]; // m_byBuff[0] is mode byte
  int m_nBuffPos;

  unsigned char m_byState;
  int m_nX;
  int m_nY;
  bool m_bBtn0;
  bool m_bBtn1;

  bool m_bVBL;

  unsigned int m_iX;
  unsigned int m_iRangeX;
  unsigned int m_iMinX;
  unsigned int m_iMaxX;
  unsigned int m_iY;
  unsigned int m_iRangeY;
  unsigned int m_iMinY;
  unsigned int m_iMaxY;

  bool m_bButtons[2];

  bool m_bActive;
  uint8_t* m_pSlotRom;
  unsigned int m_uSlot;
};

void Mouse_Initialize(uint8_t* pCxRomPeripheral, unsigned int uSlot);
void Mouse_Uninitialize();
void Mouse_SetSlotRom();
unsigned char Mouse_IORead(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, uint32_t nCyclesLeft);
unsigned char Mouse_IOWrite(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, uint32_t nCyclesLeft);
void Mouse_SetPosition(int xvalue, int xrange, int yvalue, int yrange);
void Mouse_SetButton(eBUTTON Button, eBUTTONSTATE State);
bool Mouse_Active();
void Mouse_SetVBlank(bool bVBL);

extern struct MouseInterface sg_Mouse;

#endif
