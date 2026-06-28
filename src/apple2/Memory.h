// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstdint>

#include "core/Common.h"

struct SsBaseMemory_t;
using SS_BaseMemory = SsBaseMemory_t;

#define MF_80STORE 0x00000001
#define MF_ALTZP 0x00000002
#define MF_AUXREAD 0x00000004
#define MF_AUXWRITE 0x00000008
#define MF_HRAM_BANK2 0x00000010
#define MF_HIGHRAM 0x00000020
#define MF_HIRES 0x00000040
#define MF_PAGE2 0x00000080
#define MF_SLOTC3ROM 0x00000100
#define MF_SLOTCXROM 0x00000200
#define MF_HRAM_WRITE 0x00000400
#define MF_IMAGEMASK 0x000007F7

enum {
  MEMORY_64K = 0x10000,
  PAGE_SIZE = 0x0100,
  NUM_PAGES_64K = MEMORY_64K / PAGE_SIZE,
  NUM_PAGES_48K = 192,

  LC_BANK_SIZE = 0x1000,     // 4K ($D000 .. $DFFF)
  CX_ROM_SIZE = 0x1000,      // 4K ($C000 .. $CFFF range)
  APPLE2_ROM_SIZE = 0x3000,  // 12K ($D000 .. $FFFF range)
  ROM_BUFFER_SIZE = 0x5000,  // 20K (enough for all ROMs)

  APPLE_SLOT_SIZE = 0x0100,   // 1 page  = $Cx00 .. $CxFF (slot 1 .. 7)
  APPLE_SLOT_BEGIN = 0xC100,  // each slot has 1 page reserved for it
  APPLE_SLOT_END = 0xC7FF,

  IO_RANGE_BEGIN = 0xC000,
  IO_RANGE_END = 0xCFFF,
  IO_PAGE_C0 = 0xC0,
  NUM_IO_HANDLERS = 512,

  FIRMWARE_EXPANSION_SIZE = 0x0800,   // 8 pages = $C800 .. $CFFF
  FIRMWARE_EXPANSION_BEGIN = 0xC800,  // [C800,CFFF)
  FIRMWARE_EXPANSION_END = 0xCFFF
};

enum {
  TXT1_BEGIN = 0x0400,
  TXT1_END = 0x07FF,
  TXT1_END_PAGE = 0x0700,
  TXT2_BEGIN = 0x0800,
  TXT2_END = 0x0BFF,

  HGR1_BEGIN = 0x2000,
  HGR1_END = 0x3FFF,
  HGR1_END_PAGE = 0x3F00,
  HGR2_BEGIN = 0x4000,
  HGR2_END = 0x5FFF
};

enum {
  PAGE_ZERO = 0x00,
  PAGE_ONE = 0x01,
  PAGE_STACK = 0x01,
  PAGE_TWO = 0x02,
  PAGE_TXT1_START = 0x04,
  PAGE_TXT1_END = 0x08,
  PAGE_HGR1_START = 0x20,
  PAGE_HGR1_END = 0x40,

  PAGE_C0 = 0xC0,
  PAGE_C3 = 0xC3,
  PAGE_C8 = 0xC8,
  PAGE_D0 = 0xD0,
  PAGE_E0 = 0xE0,
  PAGE_MAX = 0x100
};

constexpr uint16_t STACK_BEGIN = 0x0100;
constexpr uint16_t STACK_END = 0x01FF;
constexpr uint16_t IO_REGION_START = 0xC000;
constexpr uint16_t IO_REGION_MASK = 0xF000;

constexpr uint16_t PAGE_MASK = 0xFF00;
constexpr uint8_t ADDR_NIBBLE_MASK = 0x0F;

enum SoftSwitch_e {
  SS_80STORE_OFF = 0x00,
  SS_80STORE_ON = 0x01,
  SS_AUXREAD_OFF = 0x02,
  SS_AUXREAD_ON = 0x03,
  SS_AUXWRITE_OFF = 0x04,
  SS_AUXWRITE_ON = 0x05,
  SS_SLOTCXROM_ON = 0x06,
  SS_SLOTCXROM_OFF = 0x07,
  SS_ALTZP_OFF = 0x08,
  SS_ALTZP_ON = 0x09,
  SS_SLOTC3ROM_OFF = 0x0A,
  SS_SLOTC3ROM_ON = 0x0B,

  SS_LC_BEGIN = 0x80,
  SS_LC_END = 0x8F,

  SS_RDLCRAM = 0x11,
  SS_RDRAMRD = 0x12,
  SS_RDRAMWRT = 0x13,
  SS_RDCXROM = 0x14,
  SS_RDALTZP = 0x15,
  SS_RD80STORE = 0x16,
  SS_RDSLOTC3ROM = 0x17,
  SS_RD80COL = 0x18,
  SS_RDVBLBAR = 0x19,
  SS_RDTEXT = 0x1A,
  SS_RDMIXED = 0x1B,
  SS_RDPAGE2 = 0x1C,
  SS_RDHIRES = 0x1D,
  SS_RDALTCHAR = 0x1E,
  SS_RD80VID = 0x1F,

  SS_PAGE2_OFF = 0x54,
  SS_PAGE2_ON = 0x55,
  SS_HIRES_OFF = 0x56,
  SS_HIRES_ON = 0x57,

  SS_TEXT_OFF = 0x50,
  SS_TEXT_ON = 0x51,
  SS_MIXED_OFF = 0x52,
  SS_MIXED_ON = 0x53,

  SS_AN0_OFF = 0x58,
  SS_AN0_ON = 0x59,
  SS_AN1_OFF = 0x5A,
  SS_AN1_ON = 0x5B,
  SS_AN2_OFF = 0x5C,
  SS_AN2_ON = 0x5D,
  SS_AN3_OFF = 0x5E,
  SS_AN3_ON = 0x5F,

#ifdef RAMWORKS
  SS_RW_AUX_PAGE = 0x71,
  SS_RW_III_PAGE = 0x73,
#endif
};

#ifdef RAMWORKS
constexpr uint32_t MAX_RAMWORKS_PAGES = 128;
#endif

enum MemoryInitPattern_e { MIP_ZERO, MIP_FF_FF_00_00, NUM_MIP };
extern MemoryInitPattern_e g_eMemoryInitPattern;

enum eExpansionRomType { eExpRomNull = 0, eExpRomInternal, eExpRomPeripheral };

struct MemoryInstance_t {
  uint8_t* memaux = nullptr;
  uint8_t* memaux_allocated = nullptr;
  uint8_t* memmain = nullptr;
  uint8_t* memdirty = nullptr;
  uint8_t* memrom = nullptr;
  uint8_t* memimage = nullptr;
  uint8_t* pCxRomInternal = nullptr;
  uint8_t* pCxRomPeripheral = nullptr;
  uint8_t* mem = nullptr;

  uint8_t* memshadow[NUM_PAGES_64K]{};
  uint8_t* memwrite[NUM_PAGES_64K]{};

  iofunction io_read[NUM_IO_HANDLERS]{};
  iofunction io_write[NUM_IO_HANDLERS]{};

  void* slot_parameters[NUM_SLOTS]{};
  bool last_write_ram = false;
  uint32_t mem_mode = MF_HRAM_BANK2 | MF_SLOTCXROM | MF_HRAM_WRITE;
  bool mode_changing = false;

  uint32_t active_bank = 0;
#ifdef RAMWORKS
  uint8_t* rw_pages[MAX_RAMWORKS_PAGES]{};
#endif

  eExpansionRomType expansion_rom_type = eExpRomNull;
  uint32_t peripheral_rom_slot = 0;
  uint8_t io_select = 0;
  uint8_t io_select_internal_rom = 0;
  uint8_t* expansion_rom[NUM_SLOTS]{};

  ~MemoryInstance_t();
};

auto mem_get_active_context() -> MemoryInstance_t*;
auto mem_set_active_context(MemoryInstance_t* context) -> void;

extern iofunction* IORead;
extern iofunction* IOWrite;
extern uint8_t** memwrite;
extern uint8_t* mem;
extern uint8_t* memdirty;

#ifdef RAMWORKS
extern uint32_t g_uMaxExPages;
#endif

auto register_io_handler(uint32_t slot, iofunction io_read_c0,
                         iofunction io_write_c0, iofunction io_read_cx,
                         iofunction io_write_cx, void* slot_parameter,
                         uint8_t* expansion_rom) -> void;

auto register_direct_io_handler(uint16_t addr, iofunction read,
                                iofunction write, void* instance) -> void;

auto mem_destroy() -> void;
auto mem_get_80store() -> bool;
auto mem_check_slotcxrom() -> bool;
auto mem_get_aux_ptr(uint16_t addr) -> uint8_t*;
auto mem_get_main_ptr(uint16_t addr) -> uint8_t*;
auto mem_get_cx_rom_peripheral() -> uint8_t*;
auto get_mem_ptr(uint16_t addr) -> uint8_t*;
auto mem_get_bank_ptr(const uint32_t bank) -> uint8_t*;
auto get_mem_mode() -> uint32_t;
auto set_mem_mode(uint32_t mode) -> void;
auto mem_is_addr_code_memory(const uint16_t addr) -> bool;
auto mem_pre_initialize() -> void;
auto mem_initialize() -> int;
auto mem_read_floating_bus(const uint32_t executed_cycles) -> uint8_t;
auto mem_read_floating_bus(const uint8_t highbit,
                           const uint32_t executed_cycles) -> uint8_t;
auto mem_reset() -> void;
auto mem_reset_paging() -> void;
auto mem_return_random_data(uint8_t highbit) -> uint8_t;
auto mem_set_fast_paging(bool enable) -> void;
auto mem_set_80store(bool enable) -> void;
auto mem_trim_images() -> void;
auto mem_get_slot_parameters(uint32_t slot) -> void*;
auto mem_get_snapshot(SS_BaseMemory* snapshot) -> uint32_t;
auto mem_set_snapshot(SS_BaseMemory* snapshot) -> uint32_t;
auto io_null(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
             uint32_t cycles) -> uint8_t;
auto mem_update_paging(bool initialize, bool updatewriteonly) -> void;
auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                     uint32_t cycles) -> uint8_t;
auto mem_check_paging(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                      uint32_t cycles) -> uint8_t;
auto mem_set_paging(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                    uint32_t cycles) -> uint8_t;
auto get_ramworks_active_bank() -> uint32_t;

// Legacy Forwarding Declarations
auto MemGetActiveContext() -> MemoryInstance_t*;
auto MemSetActiveContext(MemoryInstance_t* context) -> void;
auto RegisterIoHandler(uint32_t uSlot, iofunction IOReadC0,
                       iofunction IOWriteC0, iofunction IOReadCx,
                       iofunction IOWriteCx, void* lpSlotParameter,
                       uint8_t* pExpansionRom) -> void;
auto RegisterDirectIoHandler(uint16_t addr, iofunction read, iofunction write,
                             void* instance) -> void;
auto MemDestroy() -> void;
auto MemGet80Store() -> bool;
auto MemCheckSLOTCXROM() -> bool;
auto MemGetAuxPtr(uint16_t addr) -> uint8_t*;
auto MemGetMainPtr(uint16_t addr) -> uint8_t*;
auto MemGetCxRomPeripheral() -> uint8_t*;
auto GetMemPtr(uint16_t addr) -> uint8_t*;
auto MemGetBankPtr(const uint32_t nBank) -> uint8_t*;
auto GetMemMode() -> uint32_t;
auto SetMemMode(uint32_t memmode) -> void;
auto MemIsAddrCodeMemory(const uint16_t addr) -> bool;
auto MemPreInitialize() -> void;
auto MemInitialize() -> int;
auto MemReadFloatingBus(const uint32_t uExecutedCycles) -> uint8_t;
auto MemReadFloatingBus(const uint8_t highbit, const uint32_t uExecutedCycles)
    -> uint8_t;
auto MemReset() -> void;
auto MemResetPaging() -> void;
auto MemReturnRandomData(uint8_t highbit) -> uint8_t;
auto MemSetFastPaging(bool enable) -> void;
auto MemSet80Store(bool enable) -> void;
auto MemTrimImages() -> void;
auto MemGetSlotParameters(uint32_t uSlot) -> void*;
auto MemGetSnapshot(SS_BaseMemory* pSS) -> uint32_t;
auto MemSetSnapshot(SS_BaseMemory* pSS) -> uint32_t;
auto IO_Null(uint16_t programcounter, uint16_t address, uint8_t write,
             uint8_t value, uint32_t nCycles) -> uint8_t;
auto MemUpdatePaging(bool initialize, bool updatewriteonly) -> void;
auto IOMap_Dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                    uint32_t cycles) -> uint8_t;
auto MemCheckPaging(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                    uint32_t nCyclesLeft) -> uint8_t;
auto MemSetPaging(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                  uint32_t nCyclesLeft) -> uint8_t;
auto GetRamWorksActiveBank() -> uint32_t;
