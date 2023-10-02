#pragma once

#include <pthread.h>

extern class CSuperSerialCard sg_SSC;
class DataRing;

enum {
  COMMEVT_WAIT = 0, COMMEVT_ACK, COMMEVT_TERM, COMMEVT_MAX
};
enum eFWMODE {
  FWMODE_CIC = 0, FWMODE_SIC_P8, FWMODE_PPC, FWMODE_SIC_P8A
};  // NB. CIC = SSC

// Windows specific values
#define NOPARITY            0
#define ODDPARITY           1
#define EVENPARITY          2
#define MARKPARITY          3
#define SPACEPARITY         4

#define ONESTOPBIT          0
#define ONE5STOPBITS        1
#define TWOSTOPBITS         2

// Undocumented CMSPAR flag for MARKPARITY and SPACEPARITY???
#ifndef CMSPAR
#define CMSPAR   010000000000
#endif

typedef struct {
  //DIPSW1
  unsigned int uBaudRate;
  eFWMODE eFirmwareMode;

  //DIPSW2
  unsigned int uStopBits;
  unsigned int uByteSize;
  unsigned int uParity;
  bool bLinefeed;
  bool bInterrupts;
} SSC_DIPSW;

class CSuperSerialCard {
public:
  CSuperSerialCard();
  virtual ~CSuperSerialCard();

  void CommInitialize(LPBYTE pCxRomPeripheral, unsigned int uSlot);

  void CommReset();

  void CommDestroy();

  void CommSetSerialPort(unsigned int);

  void CommUpdate(unsigned int);

  unsigned int CommGetSnapshot(SS_IO_Comms *pSS);

  unsigned int CommSetSnapshot(SS_IO_Comms *pSS);

  unsigned int GetSerialPort() {
    return m_dwSerialPort;
  }

  void SetSerialPort(unsigned int dwSerialPort) {
    m_dwSerialPort = dwSerialPort;
  }

  static unsigned char SSC_IORead(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, ULONG nCyclesLeft);

  static unsigned char SSC_IOWrite(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, ULONG nCyclesLeft);

private:
  unsigned char CommCommand(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, ULONG nCyclesLeft);

  unsigned char CommControl(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, ULONG nCyclesLeft);

  unsigned char CommDipSw(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, ULONG nCyclesLeft);

  unsigned char CommReceive(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, ULONG nCyclesLeft);

  unsigned char CommStatus(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, ULONG nCyclesLeft);

  unsigned char CommTransmit(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, ULONG nCyclesLeft);

  void GetDIPSW();

  void SetDIPSWDefaults();

  unsigned char GenerateControl();

  unsigned int BaudRateToIndex(unsigned int uBaudRate);

  void UpdateCommState();

  bool CheckComm();

  void CloseComm();

  void CheckCommEvent(unsigned int dwEvtMask);

  static unsigned int CommThread(LPVOID lpParameter);

  bool CommThInit();

  void CommThUninit();

private:
  unsigned int m_dwSerialPort;

  static SSC_DIPSW m_DIPSWDefault;
  SSC_DIPSW m_DIPSWCurrent;

  // Derived from DIPSW1
  unsigned int m_uBaudRate;

  // Derived from DIPSW2
  unsigned int m_uStopBits;
  unsigned int m_uByteSize;
  unsigned int m_uParity;

  // SSC Registers
  unsigned char m_uControlByte;
  unsigned char m_uCommandByte;

  int m_hCommHandle;  // file for communication with COM
  unsigned int m_dwCommInactivity;

	DataRing *rxbuf = nullptr;

  bool m_bTxIrqEnabled;
  bool m_bRxIrqEnabled;

  bool m_bWrittenTx;
  unsigned int m_uLastBytesWritten;

  volatile bool m_vbCommIRQ;
  HANDLE m_hCommThread;

  HANDLE m_hCommEvent[COMMEVT_MAX];
  OVERLAPPED m_o;

  unsigned char *m_pExpansionRom;
};
