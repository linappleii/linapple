#pragma once

#include <pthread.h>

extern class CSuperSerialCard sg_SSC;

enum {COMMEVT_WAIT=0, COMMEVT_ACK, COMMEVT_TERM, COMMEVT_MAX};
enum eFWMODE {FWMODE_CIC=0, FWMODE_SIC_P8, FWMODE_PPC, FWMODE_SIC_P8A};	// NB. CIC = SSC

//////// windows specific values
#define NOPARITY            0
#define ODDPARITY           1
#define EVENPARITY          2
#define MARKPARITY          3
#define SPACEPARITY         4

#define ONESTOPBIT          0
#define ONE5STOPBITS        1
#define TWOSTOPBITS         2
///////////////////////////////
// Undocumented CMSPAR flag for MARKPARITY and SPACEPARITY???
#ifndef CMSPAR
#define CMSPAR   010000000000
#endif
/////////////////////////////////////

typedef struct
{
	//DIPSW1
	UINT	uBaudRate;
	eFWMODE	eFirmwareMode;

	//DIPSW2
	UINT	uStopBits;
	UINT	uByteSize;
	UINT	uParity;
	bool	bLinefeed;
	bool	bInterrupts;
} SSC_DIPSW;

class CSuperSerialCard
{
public:
	CSuperSerialCard();
	virtual ~CSuperSerialCard() {}

	void	CommInitialize(LPBYTE pCxRomPeripheral, UINT uSlot);
	void    CommReset();
	void    CommDestroy();
	void    CommSetSerialPort(/*HWND,*/DWORD);
	void    CommUpdate(DWORD);
	DWORD   CommGetSnapshot(SS_IO_Comms* pSS);
	DWORD   CommSetSnapshot(SS_IO_Comms* pSS);

	DWORD	GetSerialPort() { return m_dwSerialPort; }
	void	SetSerialPort(DWORD dwSerialPort) { m_dwSerialPort = dwSerialPort; }

	static BYTE SSC_IORead(WORD PC, WORD uAddr, BYTE bWrite, BYTE uValue, ULONG nCyclesLeft);
	static BYTE SSC_IOWrite(WORD PC, WORD uAddr, BYTE bWrite, BYTE uValue, ULONG nCyclesLeft);

private:
	BYTE CommCommand(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
	BYTE CommControl(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
	BYTE CommDipSw(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
	BYTE CommReceive(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
	BYTE CommStatus(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
	BYTE CommTransmit(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);

	void	GetDIPSW();
	void	SetDIPSWDefaults();
	BYTE	GenerateControl();
	UINT	BaudRateToIndex(UINT uBaudRate);
	void	UpdateCommState();
	BOOL	CheckComm();
	void	CloseComm();
	void	CheckCommEvent(DWORD dwEvtMask);
	static DWORD CommThread(LPVOID lpParameter);
	bool	CommThInit();
	void	CommThUninit();

	//

private:
	DWORD	m_dwSerialPort;

	static SSC_DIPSW	m_DIPSWDefault;
	SSC_DIPSW			m_DIPSWCurrent;

	// Derived from DIPSW1
	UINT	m_uBaudRate;

	// Derived from DIPSW2
	UINT	m_uStopBits;
	UINT	m_uByteSize;
	UINT	m_uParity;

	// SSC Registers
	BYTE   m_uControlByte;
	BYTE   m_uCommandByte;

	//

	int    m_hCommHandle;	// file for communication with COM
	DWORD  m_dwCommInactivity;


// how does CRITICAL_SECTION work in Linux? -- see in Wikipedia: http://en.wikipedia.org/wiki/Critical_section
// --> to main file
//	CRITICAL_SECTION	m_CriticalSection;	// To guard /g_vRecvBytes/
	BYTE			m_RecvBuffer[uRecvBufferSize];	// NB: More work required if >1 is used
	volatile DWORD		m_vRecvBytes;

	//

	bool m_bTxIrqEnabled;
	bool m_bRxIrqEnabled;

	bool m_bWrittenTx;

	//

	volatile bool m_vbCommIRQ;
	HANDLE m_hCommThread;

	HANDLE m_hCommEvent[COMMEVT_MAX];
	OVERLAPPED m_o;

	BYTE* m_pExpansionRom;
};
