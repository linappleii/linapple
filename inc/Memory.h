#pragma once

// Memory Flag
#define  MF_80STORE    0x00000001
#define  MF_ALTZP      0x00000002
#define  MF_AUXREAD    0x00000004
#define  MF_AUXWRITE   0x00000008
#define  MF_HRAM_BANK2 0x00000010
#define  MF_HIGHRAM    0x00000020
#define  MF_HIRES      0x00000040
#define  MF_PAGE2      0x00000080
#define  MF_SLOTC3ROM  0x00000100
#define  MF_SLOTCXROM  0x00000200
#define  MF_HRAM_WRITE 0x00000400
#define  MF_IMAGEMASK  0x000007F7

enum
{
  // Note: All are in bytes!
  APPLE_SLOT_SIZE          = 0x0100, // 1 page  = $Cx00 .. $CxFF (slot 1 .. 7)
  APPLE_SLOT_BEGIN         = 0xC100, // each slot has 1 page reserved for it
  APPLE_SLOT_END           = 0xC7FF, //

  FIRMWARE_EXPANSION_SIZE  = 0x0800, // 8 pages = $C800 .. $CFFF
  FIRMWARE_EXPANSION_BEGIN = 0xC800, // [C800,CFFF)
  FIRMWARE_EXPANSION_END   = 0xCFFF //
};

enum MemoryInitPattern_e {
  MIP_ZERO, MIP_FF_FF_00_00, NUM_MIP
};
extern MemoryInitPattern_e g_eMemoryInitPattern;

extern iofunction IORead[256];
extern iofunction IOWrite[256];
extern LPBYTE memwrite[0x100];
extern LPBYTE mem;
extern LPBYTE memdirty;

#ifdef RAMWORKS
extern unsigned int       g_uMaxExPages;  // user requested ram pages (from cmd line)
#endif

void RegisterIoHandler(unsigned int uSlot, iofunction IOReadC0, iofunction IOWriteC0, iofunction IOReadCx, iofunction IOWriteCx,
                       LPVOID lpSlotParameter, unsigned char *pExpansionRom);

void MemDestroy();

bool MemGet80Store();

bool MemCheckSLOTCXROM();

LPBYTE MemGetAuxPtr(unsigned short);
LPBYTE MemGetMainPtr(unsigned short);

LPBYTE MemGetCxRomPeripheral();
LPBYTE MemGetBankPtr(const unsigned int nBank);

unsigned int GetMemMode(void);
void SetMemMode(unsigned int memmode);
bool MemIsAddrCodeMemory(const USHORT addr);

void MemPreInitialize();

int MemInitialize();

unsigned char MemReadFloatingBus(const ULONG uExecutedCycles);

unsigned char MemReadFloatingBus(const unsigned char highbit, const ULONG uExecutedCycles);

void MemReset();

void MemResetPaging();

unsigned char MemReturnRandomData(unsigned char highbit);

void MemSetFastPaging(bool);

void MemTrimImages();

LPVOID MemGetSlotParameters(unsigned int uSlot);

unsigned int MemGetSnapshot(SS_BaseMemory *pSS);

unsigned int MemSetSnapshot(SS_BaseMemory *pSS);

unsigned char IO_Null(unsigned short programcounter, unsigned short address, unsigned char write, unsigned char value, ULONG nCycles);

void MemUpdatePaging(bool initialize, bool updatewriteonly);

unsigned char MemCheckPaging(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, ULONG nCyclesLeft);

unsigned char MemSetPaging(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, ULONG nCyclesLeft);

unsigned int GetRamWorksActiveBank(void);
