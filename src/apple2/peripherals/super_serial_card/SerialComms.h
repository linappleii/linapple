#ifndef SERIALCOMMS_H
#define SERIALCOMMS_H

#include <cstdint>

#include "apple2/Structs.h"

// SSC DIPSW structure
enum FirmwareMode {
  FIRMWARE_CIC = 0,
  FIRMWARE_SIC_P8,
  FIRMWARE_PPC,
  FIRMWARE_SIC_P8A
};

enum SscParity {
  SSC_PARITY_NONE = 0,
  SSC_PARITY_ODD = 1,
  SSC_PARITY_EVEN = 2,
  SSC_PARITY_MARK = 3,
  SSC_PARITY_SPACE = 4
};

enum SscStopBits {
  SSC_STOP_BITS_1 = 0,
  SSC_STOP_BITS_1_5 = 1,
  SSC_STOP_BITS_2 = 2
};

typedef struct SSC_DIPSW_tag {
  // DIPSW1
  uint32_t uBaudRate;
  FirmwareMode eFirmwareMode;

  // DIPSW2
  SscStopBits eStopBits;
  uint32_t uByteSize;
  SscParity eParity;
  bool bLinefeed;
  bool bInterrupts;
} SSC_DIPSW;

// Internal baud rate constants
#define SSC_B110 110
#define SSC_B300 300
#define SSC_B600 600
#define SSC_B1200 1200
#define SSC_B2400 2400
#define SSC_B4800 4800
#define SSC_B9600 9600
#define SSC_B19200 19200

// SuperSerialCard core state
struct SuperSerialCard {
  SSC_DIPSW m_DIPSWCurrent{};

  // Derived from DIPSWs
  uint32_t m_uBaudRate = 0;
  SscStopBits m_eStopBits = SSC_STOP_BITS_1;
  uint32_t m_uByteSize = 0;
  SscParity m_eParity = SSC_PARITY_NONE;

  // SSC Registers
  uint8_t m_uControlByte = 0;
  uint8_t m_uCommandByte = 0;

  // State
  uint8_t m_RecvBuffer[uRecvBufferSize] = {0};
  volatile uint32_t m_vRecvBytes = 0;

  bool m_bTxIrqEnabled = false;
  bool m_bRxIrqEnabled = false;

  bool m_bWrittenTx = false;
  volatile bool m_vbCommIRQ = false;

  uint8_t* m_pExpansionRom = nullptr;
};

// Procedural functions for the core logic
void SSC_Reset(SuperSerialCard* pSSC);
void SSC_Destroy(SuperSerialCard* pSSC);

// Snapshot
auto SSC_GetSnapshot(SuperSerialCard* pSSC, SS_IO_Comms* pSS) -> uint32_t;
auto SSC_SetSnapshot(SuperSerialCard* pSSC, SS_IO_Comms* pSS) -> uint32_t;

// IO Handlers
auto SSC_IORead(void* instance, uint16_t PC, uint16_t uAddr, uint8_t bWrite,
                uint8_t uValue, uint32_t nCyclesLeft) -> uint8_t;
auto SSC_IOWrite(void* instance, uint16_t PC, uint16_t uAddr, uint8_t bWrite,
                 uint8_t uValue, uint32_t nCyclesLeft) -> uint8_t;

// Interface for Frontend to Core
void SSC_PushRxByte(SuperSerialCard* pSSC, uint8_t byte);

#endif  // SERIALCOMMS_H
