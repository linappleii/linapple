#ifndef SERIALCOMMS_H
#define SERIALCOMMS_H

#include <cstdint>
#include "Structs.h"

// SSC DIPSW structure
enum eFWMODE {
  FWMODE_CIC = 0, FWMODE_SIC_P8, FWMODE_PPC, FWMODE_SIC_P8A
};

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

// Internal baud rate constants
#define SSC_B110    110
#define SSC_B300    300
#define SSC_B600    600
#define SSC_B1200   1200
#define SSC_B2400   2400
#define SSC_B4800   4800
#define SSC_B9600   9600
#define SSC_B19200  19200

// SuperSerialCard core state
struct SuperSerialCard {
  SSC_DIPSW m_DIPSWCurrent;

  // Derived from DIPSWs
  unsigned int m_uBaudRate;
  unsigned int m_uStopBits;
  unsigned int m_uByteSize;
  unsigned int m_uParity;

  // SSC Registers
  unsigned char m_uControlByte;
  unsigned char m_uCommandByte;

  // State
  unsigned char m_RecvBuffer[uRecvBufferSize];
  volatile unsigned int m_vRecvBytes;

  bool m_bTxIrqEnabled;
  bool m_bRxIrqEnabled;

  bool m_bWrittenTx;
  volatile bool m_vbCommIRQ;

  unsigned char *m_pExpansionRom;
};

// Procedural functions for the core logic
void SSC_Reset(SuperSerialCard* pSSC);
void SSC_Initialize(SuperSerialCard* pSSC, uint8_t* pCxRomPeripheral, unsigned int uSlot);
void SSC_Destroy(SuperSerialCard* pSSC);

// Snapshot
unsigned int SSC_GetSnapshot(SuperSerialCard* pSSC, SS_IO_Comms *pSS);
unsigned int SSC_SetSnapshot(SuperSerialCard* pSSC, SS_IO_Comms *pSS);

// IO Handlers
unsigned char SSC_IORead(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, uint32_t nCyclesLeft);
unsigned char SSC_IOWrite(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, uint32_t nCyclesLeft);

// Interface for Frontend to Core
void SSC_PushRxByte(SuperSerialCard* pSSC, uint8_t byte);

// Interface for Core to call Frontend (implemented in Frontend)
extern void SSCFrontend_SendByte(uint8_t byte);
extern bool SSCFrontend_IsActive();
extern void SSCFrontend_UpdateState(unsigned int baud, unsigned int bits, unsigned int parity, unsigned int stop);

// Windows specific values (needed for core parity logic)
#ifndef NOPARITY
#define NOPARITY            0
#define ODDPARITY           1
#define EVENPARITY          2
#define MARKPARITY          3
#define SPACEPARITY         4
#endif

#ifndef ONESTOPBIT
#define ONESTOPBIT          0
#define ONE5STOPBITS        1
#define TWOSTOPBITS         2
#endif

// Global instance (to be moved/handled)
extern struct SuperSerialCard sg_SSC;

#endif // SERIALCOMMS_H
