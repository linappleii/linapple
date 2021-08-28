// Structs used by save-state file

// *** DON'T CHANGE ANY STRUCT WITHOUT CONSIDERING BACKWARDS COMPATIBILITY WITH .AWS FORMAT ***

#define MAKE_VERSION(a, b, c, d) ((a<<24) | (b<<16) | (c<<8) | (d))

#define AW_SS_TAG (('S'<<24)|('S'<<16)|('W'<<8)|'A')  // 'AWSS' = AppleWin SnapShot

typedef struct {
  unsigned int dwTag;    // "AWSS"
  unsigned int dwVersion;
  unsigned int dwChecksum;
} SS_FILE_HDR;

typedef struct {
  unsigned int dwLength;    // Byte length of this unit struct
  unsigned int dwVersion;
} SS_UNIT_HDR;

const unsigned int nMemMainSize = 64 * 1024;
const unsigned int nMemAuxSize = 64 * 1024;

typedef struct {
  unsigned char A;
  unsigned char X;
  unsigned char Y;
  unsigned char P;
  unsigned char S;
  USHORT PC;
  UINT64 g_nCumulativeCycles;
  // IRQ = OR-sum of all interrupt sources
} SS_CPU6502;

const unsigned int uRecvBufferSize = 9;

typedef struct {
  unsigned int baudrate;
  unsigned char bytesize;
  unsigned char commandbyte;
  unsigned int comminactivity;  // If non-zero then COM port open
  unsigned char controlbyte;
  unsigned char parity;
  unsigned char recvbuffer[uRecvBufferSize];
  unsigned int recvbytes;
  unsigned char stopbits;
} SS_IO_Comms;

typedef struct {
  UINT64 g_nJoyCntrResetCycle;
} SS_IO_Joystick;

typedef struct {
  unsigned int keyboardqueries;
  unsigned char nLastKey;
} SS_IO_Keyboard;

typedef struct {
  UINT64 g_nSpkrLastCycle;
} SS_IO_Speaker;

typedef struct {
  bool bAltCharSet;  // charoffs
  unsigned int dwVidMode;
} SS_IO_Video;

typedef struct {
  unsigned int dwMemMode;
  bool bLastWriteRam;
  unsigned char nMemMain[nMemMainSize];
  unsigned char nMemAux[nMemAuxSize];
} SS_BaseMemory;

typedef struct {
  SS_UNIT_HDR UnitHdr;
  SS_CPU6502 CPU6502;
  SS_IO_Comms Comms;
  SS_IO_Joystick Joystick;
  SS_IO_Keyboard Keyboard;
  //  SS_IO_Memory Memory;
  SS_IO_Speaker Speaker;
  SS_IO_Video Video;
  SS_BaseMemory Memory;
} SS_APPLE2_Unit;

typedef struct {
  unsigned int dwComputerEmulation;
  bool bCustomSpeed;
  unsigned int dwEmulationSpeed;
  bool bEnhancedDiskSpeed;
  unsigned int dwJoystickType[2];
  bool bMockingboardEnabled;
  unsigned int dwMonochromeColor;
  unsigned int dwSerialPort;
  unsigned int dwSoundType;  // Sound Emulation
  unsigned int dwVideoType;  // Video Emulation
} SS_AW_CFG;

typedef struct {
  char StartingDir[MAX_PATH];
  unsigned int dwWindowXpos;
  unsigned int dwWindowYpos;
} SS_AW_PREFS;

typedef struct {
  SS_UNIT_HDR UnitHdr;
  unsigned int dwAppleWinVersion;
  SS_AW_PREFS Prefs;
  SS_AW_CFG Cfg;
} SS_APPLEWIN_CONFIG;

typedef struct {
  SS_UNIT_HDR UnitHdr;
  unsigned int dwType;    // SS_CARDTYPE
  unsigned int dwSlot;    // [1..7]
} SS_CARD_HDR;

enum SS_CARDTYPE {
  CT_Empty = 0, CT_Disk2,      // Apple Disk][
  CT_SSC,        // Apple Super Serial Card
  CT_Mockingboard, CT_GenericPrinter, CT_GenericHDD,    // Hard disk
  CT_GenericClock, CT_MouseInterface,
};

typedef struct {
  SS_CARD_HDR Hdr;
} SS_CARD_EMPTY;

const unsigned int NIBBLES_PER_TRACK = 0x1A00;

typedef struct {
  char szFileName[MAX_PATH];
  int track;
  int phase;
  int byte;
  bool writeprotected;
  bool trackimagedata;
  bool trackimagedirty;
  unsigned int spinning;
  unsigned int writelight;
  int nibbles;
  unsigned char nTrack[NIBBLES_PER_TRACK];
} DISK2_Unit;

typedef struct {
  SS_CARD_HDR Hdr;
  DISK2_Unit Unit[2];
  unsigned short phases;
  unsigned short currdrive;
  bool diskaccessed;
  bool enhancedisk;
  unsigned char floppylatch;
  bool floppymotoron;
  bool floppywritemode;
} SS_CARD_DISK2;

typedef struct {
  union {
    struct {
      unsigned char l;
      unsigned char h;
    };
    USHORT w;
  };
} IWORD;

typedef struct {
  unsigned char ORB;        // $00 - Port B
  unsigned char ORA;        // $01 - Port A (with handshaking)
  unsigned char DDRB;        // $02 - Data Direction Register B
  unsigned char DDRA;        // $03 - Data Direction Register A
  //
  // $04 - Read counter (L) / Write latch (L)
  // $05 - Read / Write & initiate count (H)
  // $06 - Read / Write & latch (L)
  // $07 - Read / Write & latch (H)
  // $08 - Read counter (L) / Write latch (L)
  // $09 - Read counter (H) / Write latch (H)
  IWORD TIMER1_COUNTER;
  IWORD TIMER1_LATCH;
  IWORD TIMER2_COUNTER;
  IWORD TIMER2_LATCH;
  //
  unsigned char SERIAL_SHIFT;    // $0A
  unsigned char ACR;        // $0B - Auxiliary Control Register
  unsigned char PCR;        // $0C - Peripheral Control Register
  unsigned char IFR;        // $0D - Interrupt Flag Register
  unsigned char IER;        // $0E - Interrupt Enable Register
  unsigned char ORA_NO_HS;      // $0F - Port A (without handshaking)
} SY6522;

typedef struct {
  unsigned char DurationPhonome;
  unsigned char Inflection;    // I10..I3
  unsigned char RateInflection;
  unsigned char CtrlArtAmp;
  unsigned char FilterFreq;
  //
  unsigned char CurrentMode;    // b7:6=Mode; b0=D7 pin (for IRQ)
} SSI263A;

typedef struct {
  SY6522 RegsSY6522;
  unsigned char RegsAY8910[16];
  SSI263A RegsSSI263;
  unsigned char nAYCurrentRegister;
  bool bTimer1IrqPending;
  bool bTimer2IrqPending;
  bool bSpeechIrqPending;
} MB_Unit;

const unsigned int MB_UNITS_PER_CARD = 2;

typedef struct {
  SS_CARD_HDR Hdr;
  MB_Unit Unit[MB_UNITS_PER_CARD];
} SS_CARD_MOCKINGBOARD;

typedef struct {
  SS_FILE_HDR Hdr;
  SS_APPLE2_Unit Apple2Unit;
  //  SS_APPLEWIN_CONFIG AppleWinCfg;
  SS_CARD_EMPTY Empty1;        // Slot1
  SS_CARD_EMPTY Empty2;        // Slot2
  SS_CARD_EMPTY Empty3;        // Slot3
  SS_CARD_MOCKINGBOARD Mockingboard1;  // Slot4
  SS_CARD_MOCKINGBOARD Mockingboard2;  // Slot5
  SS_CARD_DISK2 Disk2;        // Slot6
  SS_CARD_EMPTY Empty7;        // Slot7
} APPLEWIN_SNAPSHOT;
