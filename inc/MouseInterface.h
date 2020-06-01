#include "6821.h"
#include "Common.h"

#define WRITE_HANDLER(func)    void func( void* objFrom, void* objTo, int nAddr, unsigned char byData )
#define CALLBACK_HANDLER(func)  void func( void* objFrom, void* objTo, LPARAM lParam )

extern class CMouseInterface sg_Mouse;

class CMouseInterface {
public:
  CMouseInterface();

  virtual ~CMouseInterface();

  void Initialize(LPBYTE pCxRomPeripheral, unsigned int uSlot);

  void Uninitialize() {
    m_bActive = false;
  }

  void SetSlotRom();

  static unsigned char IORead(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, ULONG nCyclesLeft);

  static unsigned char IOWrite(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, ULONG nCyclesLeft);

  void SetPosition(int xvalue, int xrange, int yvalue, int yrange);

  void SetButton(eBUTTON Button, eBUTTONSTATE State);

  bool Active() {
    return m_bActive;
  }

  void SetVBlank(bool bVBL);

protected:
  void On6821_A(unsigned char byData);

  void On6821_B(unsigned char byData);

  void OnCommand();

  void OnWrite();

  void OnMouseEvent();

  void Reset();

  friend WRITE_HANDLER(M6821_Listener_A);

  friend WRITE_HANDLER(M6821_Listener_B);

  void SetPosition(int xvalue, int yvalue);

  void ClampX(int iMinX, int iMaxX);

  void ClampY(int iMinY, int iMaxY);

  C6821 m_6821;

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
  LPBYTE m_pSlotRom;
  unsigned int m_uSlot;
};
