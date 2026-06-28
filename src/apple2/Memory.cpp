// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/Memory.h"

#include <sys/mman.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "apple2/CPU.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/Log.h"
#include "core/resource.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-no-malloc,
// cppcoreguidelines-owning-memory, cppcoreguidelines-pro-type-reinterpret-cast,
// bugprone-easily-swappable-parameters, bugprone-branch-clone,
// cppcoreguidelines-macro-usage, modernize-use-auto,
// cppcoreguidelines-init-variables,
// cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays): Unavoidable
// hardware architectural constraints for Apple II memory management unit and
// page table multiplexer
#define SW_80STORE (memmode & MF_80STORE)
#define SW_ALTZP (memmode & MF_ALTZP)
#define SW_AUXREAD (memmode & MF_AUXREAD)
#define SW_AUXWRITE (memmode & MF_AUXWRITE)
#define SW_HRAM_BANK2 (memmode & MF_HRAM_BANK2)
#define SW_HIGHRAM (memmode & MF_HIGHRAM)
#define SW_HIRES (memmode & MF_HIRES)
#define SW_PAGE2 (memmode & MF_PAGE2)
#define SW_SLOTC3ROM (memmode & MF_SLOTC3ROM)
#define SW_SLOTCXROM (memmode & MF_SLOTCXROM)
#define SW_HRAM_WRITE (memmode & MF_HRAM_WRITE)

static inline auto read_uint32_le(const uint8_t* ptr) -> uint32_t {
  uint32_t val = 0;
  std::memcpy(&val, ptr, sizeof(val));
  return val;
}

static MemoryInstance_t g_default_memory_context;
static MemoryInstance_t* g_active_memory = &g_default_memory_context;

iofunction* IORead = g_default_memory_context.io_read;
iofunction* IOWrite = g_default_memory_context.io_write;
uint8_t** memwrite = g_default_memory_context.memwrite;
uint8_t* mem = nullptr;
uint8_t* memdirty = nullptr;
MemoryInitPattern_e g_eMemoryInitPattern = MIP_FF_FF_00_00;

static auto SetMem(uint8_t* val) -> void {
  mem = val;
  if (g_active_memory) g_active_memory->mem = val;
}
static auto SetMemDirty(uint8_t* val) -> void {
  memdirty = val;
  if (g_active_memory) g_active_memory->memdirty = val;
}

#define memaux (g_active_memory->memaux)
#define memaux_allocated (g_active_memory->memaux_allocated)
#define memmain (g_active_memory->memmain)
#define memrom (g_active_memory->memrom)
#define memimage (g_active_memory->memimage)
#define pCxRomInternal (g_active_memory->pCxRomInternal)
#define pCxRomPeripheral (g_active_memory->pCxRomPeripheral)
#define memshadow (g_active_memory->memshadow)
#define SlotParameters (g_active_memory->slot_parameters)
#define lastwriteram (g_active_memory->last_write_ram)
#define memmode (g_active_memory->mem_mode)
#define modechanging (g_active_memory->mode_changing)
#define g_uActiveBank (g_active_memory->active_bank)
#define g_eExpansionRomType (g_active_memory->expansion_rom_type)
#define g_uPeripheralRomSlot (g_active_memory->peripheral_rom_slot)
#define IO_SELECT (g_active_memory->io_select)
#define IO_SELECT_InternalROM (g_active_memory->io_select_internal_rom)
#define ExpansionRom (g_active_memory->expansion_rom)
#ifdef RAMWORKS
#define RWpages (g_active_memory->rw_pages)
#endif

auto MemGetActiveContext() -> MemoryInstance_t* { return g_active_memory; }

auto MemSetActiveContext(MemoryInstance_t* context) -> void {
  if (!context) return;
  g_active_memory = context;
  IORead = context->io_read;
  IOWrite = context->io_write;
  memwrite = context->memwrite;
  mem = context->mem;
  memdirty = context->memdirty;
}

MemoryInstance_t::~MemoryInstance_t() {
  if (memimage) {
    munlock(memimage, MEMORY_64K);
  }
  free(memaux_allocated);
  free(memmain);
  free(memdirty);
  free(memrom);
  free(memimage);
  free(pCxRomInternal);
  free(pCxRomPeripheral);

#ifdef RAMWORKS
  for (uint32_t i = 0; i < MAX_RAMWORKS_PAGES; ++i) {
    if (rw_pages[i]) {
      free(rw_pages[i]);
    }
  }
#endif
}

auto IOMap_Dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                    uint32_t cycles) -> uint8_t {
  if ((addr & PAGE_MASK) == IO_RANGE_BEGIN) {
    uint8_t index = static_cast<uint8_t>(addr & 0xFF);
    if (write) {
      return IOWrite[index](pc, addr, write, d, cycles);
    } else {
      return IORead[index](pc, addr, write, d, cycles);
    }
  } else {
    uint8_t page = static_cast<uint8_t>((addr >> 8) & ADDR_NIBBLE_MASK);
    if (write) {
      return IOWrite[NUM_PAGES_64K + page](pc, addr, write, d, cycles);
    } else {
      return IORead[NUM_PAGES_64K + page](pc, addr, write, d, cycles);
    }
  }
}

#ifdef RAMWORKS
uint32_t g_uMaxExPages = 1;
#endif

auto GetRamWorksActiveBank() -> uint32_t { return g_uActiveBank; }

auto IO_Annunciator(uint16_t programcounter, uint16_t address, uint8_t write,
                    uint8_t value, uint32_t nCycles) -> uint8_t;

auto MemUpdatePaging(bool initialize, bool updatewriteonly) -> void;

static auto IORead_C00x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                        uint32_t nCyclesLeft) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)bWrite;
  (void)d;
  // $C000-$C00F are owned by the keyboard peripheral once initialized.
  // RegisterDirectIO overwrites these dispatch slots before any CPU execution.
  return MemReadFloatingBus(nCyclesLeft);
}

static const uint8_t LAST_MEM_SOFT_SWITCH_OFFSET = 0x0B;

static auto IOWrite_C00x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                         uint32_t nCyclesLeft) -> uint8_t {
  if ((addr & ADDR_NIBBLE_MASK) <= LAST_MEM_SOFT_SWITCH_OFFSET) {
    return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
  } else {
    return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
  }
}

static auto IORead_C01x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                        uint32_t nCyclesLeft) -> uint8_t {
  switch (addr & ADDR_NIBBLE_MASK) {
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
    case 0x8:
      return MemCheckPaging(pc, addr, bWrite, d, nCyclesLeft);
    case 0x9:
      return VideoCheckVbl(pc, addr, bWrite, d, nCyclesLeft);
    case 0xA:
    case 0xB:
      return VideoCheckMode(pc, addr, bWrite, d, nCyclesLeft);
    case 0xC:
    case 0xD:
      return MemCheckPaging(pc, addr, bWrite, d, nCyclesLeft);
    case 0xE:
    case 0xF:
      return VideoCheckMode(pc, addr, bWrite, d, nCyclesLeft);
    default:
      break;
  }
  return 0;
}

static auto IOWrite_C01x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                         uint32_t nCyclesLeft) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)bWrite;
  (void)d;
  return MemReadFloatingBus(nCyclesLeft);
}

static auto IORead_C02x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                        uint32_t nCyclesLeft) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)bWrite;
  (void)d;
  return MemReadFloatingBus(nCyclesLeft);
}

static auto IOWrite_C02x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                         uint32_t nCyclesLeft) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)bWrite;
  (void)d;
  (void)nCyclesLeft;
  return 0;
}

static auto IORead_C03x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                        uint32_t nCyclesLeft) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)bWrite;
  (void)d;
  return MemReadFloatingBus(nCyclesLeft);
}

static auto IOWrite_C03x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                         uint32_t nCyclesLeft) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)bWrite;
  (void)d;
  return MemReadFloatingBus(nCyclesLeft);
}

static auto IORead_C04x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                        uint32_t nCyclesLeft) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)bWrite;
  (void)d;
  return MemReadFloatingBus(nCyclesLeft);
}

static auto IOWrite_C04x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                         uint32_t nCyclesLeft) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)bWrite;
  (void)d;
  (void)nCyclesLeft;
  return 0;
}

static auto IORead_C05x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                        uint32_t nCyclesLeft) -> uint8_t {
  switch (addr & ADDR_NIBBLE_MASK) {
    case SS_TEXT_OFF& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    case SS_TEXT_ON& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    case SS_MIXED_OFF& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    case SS_MIXED_ON& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    case SS_PAGE2_OFF& ADDR_NIBBLE_MASK:
      return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
    case SS_PAGE2_ON& ADDR_NIBBLE_MASK:
      return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
    case SS_HIRES_OFF& ADDR_NIBBLE_MASK:
      return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
    case SS_HIRES_ON& ADDR_NIBBLE_MASK:
      return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN0_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN0_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN1_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN1_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN2_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN2_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN3_OFF& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN3_ON& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    default:
      break;
  }

  return 0;
}

static auto IOWrite_C05x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                         uint32_t nCyclesLeft) -> uint8_t {
  switch (addr & ADDR_NIBBLE_MASK) {
    case SS_TEXT_OFF& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    case SS_TEXT_ON& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    case SS_MIXED_OFF& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    case SS_MIXED_ON& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    case SS_PAGE2_OFF& ADDR_NIBBLE_MASK:
      return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
    case SS_PAGE2_ON& ADDR_NIBBLE_MASK:
      return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
    case SS_HIRES_OFF& ADDR_NIBBLE_MASK:
      return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
    case SS_HIRES_ON& ADDR_NIBBLE_MASK:
      return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN0_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN0_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN1_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN1_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN2_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN2_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN3_OFF& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    case SS_AN3_ON& ADDR_NIBBLE_MASK:
      return VideoSetMode(pc, addr, bWrite, d, nCyclesLeft);
    default:
      break;
  }

  return 0;
}

static auto IORead_C06x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                        uint32_t nCyclesLeft) -> uint8_t {
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto IOWrite_C06x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                         uint32_t nCyclesLeft) -> uint8_t {
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto IORead_C07x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                        uint32_t nCyclesLeft) -> uint8_t {
  if ((addr & 0xF) == 0xF) {
    return VideoCheckMode(pc, addr, bWrite, d, nCyclesLeft);
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto IOWrite_C07x(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                         uint32_t nCyclesLeft) -> uint8_t {
  switch (addr & 0xf) {
    case 0x0:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
#ifdef RAMWORKS
    case 0x1:
      return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
    case 0x2:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0x3:
      return MemSetPaging(pc, addr, bWrite, d, nCyclesLeft);
#else
    case 0x1:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0x2:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0x3:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
#endif
    case 0x4:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0x5:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0x6:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0x7:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0x8:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0x9:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0xA:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0xB:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0xC:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0xD:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0xE:
      return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    case 0xF:
      return VideoCheckMode(pc, addr, bWrite, d, nCyclesLeft);
    default:
      break;
  }

  return 0;
}

static iofunction IORead_C0xx[8] = {
    IORead_C00x,               // Keyboard
    IORead_C01x,               // Memory/Video
    IORead_C02x,               // Cassette
    IORead_C03x,               // Speaker
    IORead_C04x, IORead_C05x,  // Video
    IORead_C06x,               // Joystick
    IORead_C07x,               // Joystick/Video
};

static iofunction IOWrite_C0xx[8] = {
    IOWrite_C00x,                // Memory/Video
    IOWrite_C01x,                // Keyboard
    IOWrite_C02x,                // Cassette
    IOWrite_C03x,                // Speaker
    IOWrite_C04x, IOWrite_C05x,  // Video/Memory
    IOWrite_C06x, IOWrite_C07x,  // Joystick/Ramworks
};

auto IO_Null(uint16_t programcounter, uint16_t address, uint8_t write,
             uint8_t value, uint32_t nCyclesLeft) -> uint8_t {
  (void)value;
  (void)programcounter;
  (void)address;
  if (!write) {
    return MemReadFloatingBus(nCyclesLeft);
  }
  return 0;
}

auto IO_Annunciator(uint16_t programcounter, uint16_t address, uint8_t write,
                    uint8_t value, uint32_t nCyclesLeft) -> uint8_t {
  (void)value;
  (void)nCyclesLeft;
  (void)programcounter;
  (void)address;
  (void)write;
  // Apple//e ROM:
  // . PC=FA6F: LDA $C058 (SETAN0)
  // . PC=FA72: LDA $C05A (SETAN1)
  // . PC=C2B5: LDA $C05D (CLRAN2)

  // NB. AN3: For //e & //c these locations are now used to enabled/disabled
  // DHIRES
  return 0;
}

// Enabling expansion ROM ($C800..$CFFF]:
// . Enable if: Enable1 && Enable2
// . Enable1 = I/O SELECT' (6502 accesses $Csxx)
//   - Reset when 6502 accesses $CFFF
// . Enable2 = I/O STROBE' (6502 accesses [$C800..$CFFF])

auto IORead_Cxxx(uint16_t programcounter, uint16_t address, uint8_t write,
                 uint8_t value, uint32_t nCyclesLeft) -> uint8_t {
  if (address == 0xCFFF) {
    // Disable expansion ROM at [$C800..$CFFF]
    // . SSC will disable on an access to $CFxx - but ROM only writes to $CFFF,
    // so it doesn't matter
    IO_SELECT = 0;
    IO_SELECT_InternalROM = 0;
    g_uPeripheralRomSlot = 0;

    if (SW_SLOTCXROM) {
      // NB. SW_SLOTCXROM==0 ensures that internal rom stays switched in
      memset(pCxRomPeripheral + FIRMWARE_EXPANSION_SIZE, 0,
             FIRMWARE_EXPANSION_SIZE);
      memset(mem + FIRMWARE_EXPANSION_BEGIN, 0, FIRMWARE_EXPANSION_SIZE);
      g_eExpansionRomType = eExpRomNull;
    }
    // NB. IO_SELECT won't get set, so ROM won't be switched back in...
  }

  uint8_t IO_STROBE = 0;

  if (IS_APPLE2() || SW_SLOTCXROM) {
    if ((address >= 0xC100) && (address <= 0xC7FF)) {
      const uint32_t uSlot = (address >> 8) & 0xF;
      if ((uSlot != 3) && ExpansionRom[uSlot]) {
        IO_SELECT |= 1 << uSlot;
      } else if ((SW_SLOTC3ROM) && ExpansionRom[uSlot]) {
        IO_SELECT |= 1 << uSlot;  // Slot3 & Peripheral ROM
      } else if (!SW_SLOTC3ROM) {
        IO_SELECT_InternalROM = 1;  // Slot3 & Internal ROM
      }
    } else if ((address >= 0xC800) && (address <= 0xCFFF)) {
      IO_STROBE = 1;
    }

    if (IO_SELECT && IO_STROBE) {
      // Enable Peripheral Expansion ROM
      uint32_t uSlot = 1;
      for (; uSlot < NUM_SLOTS; uSlot++) {
        if (IO_SELECT & (1 << uSlot)) {
          assert((IO_SELECT & ~(1 << uSlot)) == 0);
          break;
        }
      }

      if ((uSlot < NUM_SLOTS) && ExpansionRom[uSlot] &&
          (g_uPeripheralRomSlot != uSlot)) {
        memcpy(pCxRomPeripheral + FIRMWARE_EXPANSION_SIZE, ExpansionRom[uSlot],
               FIRMWARE_EXPANSION_SIZE);
        memcpy(mem + FIRMWARE_EXPANSION_BEGIN, ExpansionRom[uSlot],
               FIRMWARE_EXPANSION_SIZE);
        g_eExpansionRomType = eExpRomPeripheral;
        g_uPeripheralRomSlot = uSlot;
      }
    } else if (IO_SELECT_InternalROM && IO_STROBE &&
               (g_eExpansionRomType != eExpRomInternal)) {
      // Enable Internal ROM
      // . Get this for PR#3
      memcpy(mem + FIRMWARE_EXPANSION_BEGIN,
             pCxRomInternal + FIRMWARE_EXPANSION_SIZE, FIRMWARE_EXPANSION_SIZE);
      g_eExpansionRomType = eExpRomInternal;
      g_uPeripheralRomSlot = 0;
    }
  }

  if (!IS_APPLE2() && !SW_SLOTCXROM) {
    // !SW_SLOTC3ROM = Internal ROM: $C300-C3FF
    // !SW_SLOTCXROM = Internal ROM: $C100-CFFF

    if ((address >= 0xC100) &&
        (address <= 0xC7FF)) {  // Don't care about state of SW_SLOTC3ROM
      IO_SELECT_InternalROM = 1;
    } else if ((address >= 0xC800) && (address <= 0xCFFF)) {
      IO_STROBE = 1;
    }

    if (!SW_SLOTCXROM && IO_SELECT_InternalROM && IO_STROBE &&
        (g_eExpansionRomType != eExpRomInternal)) {
      // Enable Internal ROM
      memcpy(mem + FIRMWARE_EXPANSION_BEGIN,
             pCxRomInternal + FIRMWARE_EXPANSION_SIZE, FIRMWARE_EXPANSION_SIZE);
      g_eExpansionRomType = eExpRomInternal;
      g_uPeripheralRomSlot = 0;
    }
  }

  if ((g_eExpansionRomType == eExpRomNull) && (address >= 0xC800)) {
    return IO_Null(programcounter, address, write, value, nCyclesLeft);
  } else {
    return mem[address];
  }
}

auto IOWrite_Cxxx(uint16_t programcounter, uint16_t address, uint8_t write,
                  uint8_t value, uint32_t nCyclesLeft) -> uint8_t {
  (void)value;
  (void)nCyclesLeft;
  (void)programcounter;
  (void)address;
  (void)write;
  return 0;
}

static uint8_t g_bmSlotInit = 0;

static auto InitIoHandlers() -> void {
  g_bmSlotInit = 0;
  uint32_t i = 0;

  for (i = 0; i < 512; i++) {
    IORead[i] = IO_Null;
    IOWrite[i] = IO_Null;
  }

  // $C000..$C07F: 1:1 mapping to existing 16-byte buckets
  for (i = 0; i < 0x80; i++) {
    IORead[i] = IORead_C0xx[i >> 4];
    IOWrite[i] = IOWrite_C0xx[i >> 4];
  }

  // $C1..$CF: Page-based multiplexer
  for (i = 0; i < 16; i++) {
    IORead[NUM_PAGES_64K + i] = IORead_Cxxx;
    IOWrite[NUM_PAGES_64K + i] = IOWrite_Cxxx;
  }

  IO_SELECT = 0;
  IO_SELECT_InternalROM = 0;
  g_eExpansionRomType = eExpRomNull;
  g_uPeripheralRomSlot = 0;

  for (i = 0; i < NUM_SLOTS; i++) {
    ExpansionRom[i] = nullptr;
  }
}

// All slots [0..7] must register their handlers
auto RegisterIoHandler(uint32_t uSlot, iofunction IOReadC0,
                       iofunction IOWriteC0, iofunction IOReadCx,
                       iofunction IOWriteCx, void* lpSlotParameter,
                       uint8_t* pExpansionRom) -> void {
  if (uSlot >= NUM_SLOTS) {
    return;
  }
  g_bmSlotInit |= 1U << uSlot;
  SlotParameters[uSlot] = lpSlotParameter;

  uint16_t index = static_cast<uint16_t>(0x80 + (uSlot << 4));
  for (uint32_t i = 0; i < 16; i++) {
    IORead[index + i] = IOReadC0;
    IOWrite[index + i] = IOWriteC0;
  }

  if (uSlot == 0) {
    return;
  }

  if (IOReadCx == nullptr) {
    IOReadCx = IORead_Cxxx;
  }
  if (IOWriteCx == nullptr) {
    IOWriteCx = IOWrite_Cxxx;
  }

  IORead[NUM_PAGES_64K + uSlot] = IOReadCx;
  IOWrite[NUM_PAGES_64K + uSlot] = IOWriteCx;

  ExpansionRom[uSlot] = pExpansionRom;
}

auto RegisterDirectIoHandler(uint16_t addr, iofunction read, iofunction write,
                             void* instance) -> void {
  if ((addr & 0xFF00) != 0xC000) return;
  uint8_t index = static_cast<uint8_t>(addr & 0xFF);

  if (read) IORead[index] = read;
  if (write) IOWrite[index] = write;

  (void)instance;
}
//===========================================================================

auto GetMemMode() -> uint32_t { return memmode; }

auto SetMemMode(uint32_t uNewMemMode) -> void { memmode = uNewMemMode; }

static auto ResetPaging(bool initialize) -> void {
  lastwriteram = false;
  memmode = MF_HRAM_BANK2 | MF_SLOTCXROM | MF_HRAM_WRITE;
  MemUpdatePaging(initialize, false);
}

auto MemUpdatePaging(bool initialize, bool updatewriteonly) -> void {
  uint8_t* oldshadow[PAGE_MAX]{};
  if (!(initialize || updatewriteonly)) {
    memcpy(oldshadow, memshadow, PAGE_MAX * sizeof(uint8_t*));
  }

  uint32_t loop = 0;
  if (initialize) {
    for (loop = PAGE_ZERO; loop < PAGE_C0; loop++) {
      memwrite[loop] = mem + (loop << 8);
    }
    for (loop = PAGE_C0; loop < PAGE_D0; loop++) {
      memwrite[loop] = nullptr;
    }
  }

  if (!updatewriteonly) {
    for (loop = PAGE_ZERO; loop < PAGE_TWO; loop++) {
      memshadow[loop] = SW_ALTZP ? memaux + (loop << 8) : memmain + (loop << 8);
    }
  }

  for (loop = PAGE_TWO; loop < PAGE_C0; loop++) {
    memshadow[loop] = SW_AUXREAD ? memaux + (loop << 8) : memmain + (loop << 8);
    memwrite[loop] = ((SW_AUXREAD != 0) == (SW_AUXWRITE != 0))
                         ? mem + (loop << 8)
                     : SW_AUXWRITE ? memaux + (loop << 8)
                                   : memmain + (loop << 8);
  }

  if (!updatewriteonly) {
    for (loop = PAGE_C0; loop < PAGE_C8; loop++) {
      const uint32_t uSlotOffset = (loop & 0x0f) * PAGE_SIZE;
      if (loop == PAGE_C3) {
        memshadow[loop] =
            (SW_SLOTC3ROM && SW_SLOTCXROM)
                ? pCxRomPeripheral +
                      uSlotOffset  // C300..C3FF - Slot 3 ROM (all 0x00's)
                : pCxRomInternal + uSlotOffset;  // C300..C3FF - Internal ROM
      } else {
        memshadow[loop] =
            SW_SLOTCXROM
                ? pCxRomPeripheral + uSlotOffset  // C000..C7FF - SSC/Disk][/etc
                : pCxRomInternal + uSlotOffset;   // C000..C7FF - Internal ROM
      }
    }

    for (loop = PAGE_C8; loop < PAGE_D0; loop++) {
      const uint32_t uRomOffset = (loop & 0x0f) * PAGE_SIZE;
      memshadow[loop] =
          pCxRomInternal + uRomOffset;  // C800..CFFF - Internal ROM
    }
  }

  for (loop = PAGE_D0; loop < PAGE_E0; loop++) {
    int bankoffset = (SW_HRAM_BANK2 ? 0 : LC_BANK_SIZE);
    memshadow[loop] =
        SW_HIGHRAM
            ? SW_ALTZP ? memaux + (loop << 8) - bankoffset
                       : memmain + (loop << 8) - bankoffset
            : memrom + (static_cast<size_t>((loop - PAGE_D0) * PAGE_SIZE));

    memwrite[loop] = SW_HRAM_WRITE ? SW_HIGHRAM ? mem + (loop << 8)
                                     : SW_ALTZP
                                         ? memaux + (loop << 8) - bankoffset
                                         : memmain + (loop << 8) - bankoffset
                                   : nullptr;
  }

  for (loop = PAGE_E0; loop < PAGE_MAX; loop++) {
    memshadow[loop] =
        SW_HIGHRAM
            ? SW_ALTZP ? memaux + (loop << 8) : memmain + (loop << 8)
            : memrom + (static_cast<size_t>((loop - PAGE_D0) * PAGE_SIZE));

    memwrite[loop] = SW_HRAM_WRITE ? SW_HIGHRAM ? mem + (loop << 8)
                                     : SW_ALTZP ? memaux + (loop << 8)
                                                : memmain + (loop << 8)
                                   : nullptr;
  }

  if (SW_80STORE) {
    for (loop = PAGE_TXT1_START; loop < PAGE_TXT1_END; loop++) {
      memshadow[loop] = SW_PAGE2 ? memaux + (loop << 8) : memmain + (loop << 8);
      memwrite[loop] = mem + (loop << 8);
    }

    if (SW_HIRES) {
      for (loop = PAGE_HGR1_START; loop < PAGE_HGR1_END; loop++) {
        memshadow[loop] =
            SW_PAGE2 ? memaux + (loop << 8) : memmain + (loop << 8);
        memwrite[loop] = mem + (loop << 8);
      }
    }
  }

  // Move memory back and forth as necessary between the shadow areas and
  // the main ram image to keep both sets of memory consistent with the new
  // paging shadow table
  if (!updatewriteonly) {
    for (loop = PAGE_ZERO; loop < PAGE_MAX; loop++) {
      if (initialize || (oldshadow[loop] != memshadow[loop])) {
        if ((!(initialize)) &&
            ((*(memdirty + loop) & 1) || (loop <= PAGE_ONE))) {
          *(memdirty + loop) &= ~1;
          memcpy(oldshadow[loop], mem + (loop << 8), PAGE_SIZE);
        }
        memcpy(mem + (loop << 8), memshadow[loop], PAGE_SIZE);
      }
    }
  }
}

// All globally accessible functions are below this line

auto MemCheckPaging(uint16_t programcounter, uint16_t address, uint8_t write,
                    uint8_t value, uint32_t nCyclesLeft) -> uint8_t {
  (void)programcounter;
  (void)write;
  (void)value;
  address &= 0xFF;
  bool result = false;
  switch (address) {
    case SS_RDLCRAM:
      result = SW_HRAM_BANK2;
      break;
    case SS_RDRAMRD:
      result = SW_HIGHRAM;
      break;
    case SS_RDRAMWRT:
      result = SW_AUXREAD;
      break;
    case SS_RDCXROM:
      result = SW_AUXWRITE;
      break;
    case SS_RDALTZP:
      result = !SW_SLOTCXROM;
      break;
    case SS_RD80STORE:
      result = SW_ALTZP;
      break;
    case SS_RDSLOTC3ROM:
      result = SW_SLOTC3ROM;
      break;
    case SS_RD80COL:
      result = SW_80STORE;
      break;
    case SS_RDPAGE2:
      result = SW_PAGE2;
      break;
    case SS_RDHIRES:
      result = SW_HIRES;
      break;
    default:
      break;
  }
  return (MemReadFloatingBus(nCyclesLeft) & 0x7F) | (result ? 0x80 : 0x00);
}

auto MemDestroy() -> void {
#ifdef RAMWORKS
  for (uint32_t i = 0; i < MAX_RAMWORKS_PAGES; i++) {
    if (RWpages[i]) {
      free(RWpages[i]);
      RWpages[i] = nullptr;
    }
  }
#endif

  if (memimage) munlock(memimage, MEMORY_64K);

  free(memaux_allocated);
  free(memmain);
  free(memdirty);
  free(memrom);
  free(memimage);
  free(pCxRomInternal);
  free(pCxRomPeripheral);

  memaux = nullptr;
  memaux_allocated = nullptr;
  memmain = nullptr;
  SetMemDirty(nullptr);
  memrom = nullptr;
  memimage = nullptr;

  pCxRomInternal = nullptr;
  pCxRomPeripheral = nullptr;

  SetMem(nullptr);

  memset(memwrite, 0, NUM_PAGES_64K * sizeof(uint8_t*));
  memset(memshadow, 0, NUM_PAGES_64K * sizeof(uint8_t*));
}

auto MemGet80Store() -> bool { return SW_80STORE != 0; }

auto MemCheckSLOTCXROM() -> bool { return SW_SLOTCXROM != 0; }

auto MemGetAuxPtr(uint16_t offset) -> uint8_t* {
  uint8_t* lpMem = (memshadow[(offset >> 8)] == (memaux + (offset & PAGE_MASK)))
                       ? mem + offset
                       : memaux + offset;

#ifdef RAMWORKS
  if (((SW_PAGE2 && SW_80STORE) || VideoGetSW80COL()) &&
      ((((offset & PAGE_MASK) >= TXT1_BEGIN) &&
        ((offset & PAGE_MASK) <= TXT1_END_PAGE)) ||
       (SW_HIRES && ((offset & PAGE_MASK) >= HGR1_BEGIN) &&
        ((offset & PAGE_MASK) <= HGR1_END_PAGE)))) {
    if (RWpages[0] != nullptr) {
      lpMem = (memshadow[(offset >> 8)] == (RWpages[0] + (offset & PAGE_MASK)))
                  ? mem + offset
                  : RWpages[0] + offset;
    }
  }
#endif

  return lpMem;
}

auto MemGetMainPtr(uint16_t offset) -> uint8_t* {
  return (memshadow[(offset >> 8)] == (memmain + (offset & 0xFF00)))
             ? mem + offset
             : memmain + offset;
}

//===========================================================================

auto MemGetBankPtr(const uint32_t nBank) -> uint8_t* {
#ifdef RAMWORKS
  if (nBank > g_uMaxExPages || nBank >= MAX_RAMWORKS_PAGES) {
    return nullptr;
  }

  if (nBank == 0) {
    return memmain;
  }

  return RWpages[nBank - 1];
#else
  return (nBank == 0) ? memmain : (nBank == 1) ? memaux : nullptr;
#endif
}

auto MemGetCxRomPeripheral() -> uint8_t* { return pCxRomPeripheral; }

auto GetMemPtr(uint16_t addr) -> uint8_t* { return mem + addr; }

//===========================================================================

// Post:
// . true:  code memory
// . false: I/O memory or floating bus
auto MemIsAddrCodeMemory(const uint16_t addr) -> bool {
  if (addr < 0xC000 ||
      addr >
          FIRMWARE_EXPANSION_END) {  // Assume all A][ types have at least 48K
    return true;
  }

  if (addr < APPLE_SLOT_BEGIN) {  // [$C000..C0FF]
    return false;
  }

  if (!IS_APPLE2() &&
      SW_SLOTCXROM) {  // [$C100..C7FF] //e or Enhanced //e internal ROM
    return true;
  }

  if (!IS_APPLE2() && !SW_SLOTC3ROM &&
      (addr >> 8) == 0xC3) {  // [$C300..C3FF] //e or Enhanced //e internal ROM
    return true;
  }

  if (addr <= APPLE_SLOT_END)  // [$C100..C7FF]
  {
    const uint32_t uSlot = (addr >> 8) & 0x7;
    return (g_bmSlotInit & (1 << uSlot)) != 0;  // card present in this slot?
  }

  // [$C800..CFFF]
  if (g_eExpansionRomType == eExpRomNull) {
    if (IO_SELECT || IO_SELECT_InternalROM) {
      return true;
    }
    return false;
  }

  return true;
}

auto MemPreInitialize() -> void { InitIoHandlers(); }

auto MemInitialize() -> int  // returns -1 if any error during initialization
{
  MemDestroy();

#ifdef RAMWORKS
  if (g_uMaxExPages > MAX_RAMWORKS_PAGES) {
    g_uMaxExPages = MAX_RAMWORKS_PAGES;
  }
#endif

  const uint32_t CxRomSize = CX_ROM_SIZE;
  const uint32_t Apple2RomSize = APPLE2_ROM_SIZE;
  const uint32_t Apple2eRomSize = Apple2RomSize + CxRomSize;

  memaux_allocated = static_cast<uint8_t*>(malloc(MEMORY_64K));
  memaux = memaux_allocated;
  memmain = static_cast<uint8_t*>(malloc(MEMORY_64K));
  SetMemDirty(static_cast<uint8_t*>(malloc(NUM_PAGES_64K)));
  memrom = static_cast<uint8_t*>(malloc(ROM_BUFFER_SIZE));
  memimage = static_cast<uint8_t*>(malloc(MEMORY_64K));
  pCxRomInternal = static_cast<uint8_t*>(malloc(CxRomSize));
  pCxRomPeripheral = static_cast<uint8_t*>(malloc(CxRomSize));

  if (!memaux || !memdirty || !memimage || !memmain || !memrom ||
      !pCxRomInternal || !pCxRomPeripheral) {
    Logger::Error("Unable to allocate required memory buffers.");
    MemDestroy();
    return -1;
  }

  if (memaux) memset(memaux, 0, MEMORY_64K);
  if (memmain) memset(memmain, 0, MEMORY_64K);
  SetMem(memmain);
  if (memdirty) memset(memdirty, 0, NUM_PAGES_64K);
  if (memrom) memset(memrom, 0, ROM_BUFFER_SIZE);
  if (memimage) memset(memimage, 0, MEMORY_64K);

  if (mlock(memimage, MEMORY_64K) != 0) {
    Logger::Warning("Failed to lock memory image from swapping.");
  }

  if (pCxRomInternal) memset(pCxRomInternal, 0, CxRomSize);
  if (pCxRomPeripheral) memset(pCxRomPeripheral, 0, CxRomSize);

#ifdef RAMWORKS
  RWpages[0] = static_cast<uint8_t*>(malloc(MEMORY_64K));
  if (RWpages[0]) {
    memset(RWpages[0], 0, MEMORY_64K);
    memaux = RWpages[0];
  }
  uint32_t i = 1;
  while (i < g_uMaxExPages && i < MAX_RAMWORKS_PAGES) {
    RWpages[i] = static_cast<uint8_t*>(malloc(MEMORY_64K));
    if (RWpages[i]) {
      memset(RWpages[i], 0, MEMORY_64K);
      i++;
    } else {
      break;
    }
  }
#endif

  MemSetActiveContext(g_active_memory);

#define IDR_APPLE2_ROM "Apple2.rom"
#define IDR_APPLE2_PLUS_ROM "Apple2_Plus.rom"
#define IDR_APPLE2E_ROM "Apple2e.rom"
#define IDR_APPLE2E_ENHANCED_ROM "Apple2e_Enhanced.rom"

  uint32_t ROM_SIZE = 0;
  char* RomFileName = nullptr;
  switch (g_Apple2Type) {
    case A2TYPE_APPLE2:
      RomFileName = Apple2_rom;
      ROM_SIZE = Apple2RomSize;
      break;
    case A2TYPE_APPLE2PLUS:
      RomFileName = Apple2plus_rom;
      ROM_SIZE = Apple2RomSize;
      break;
    case A2TYPE_APPLE2E:
      RomFileName = Apple2e_rom;
      ROM_SIZE = Apple2eRomSize;
      break;
    case A2TYPE_APPLE2EENHANCED:
      RomFileName = Apple2eEnhanced_rom;
      ROM_SIZE = Apple2eRomSize;
      break;
    default:
      break;
  }

  if (RomFileName == nullptr) {
    fprintf(stderr, "Unable to find rom for specified computer type! Sorry\n");
    return -1;
  }

  auto* pData = reinterpret_cast<uint8_t*>(
      RomFileName);  // NB. Don't need to unlock resource

  memset(pCxRomInternal, 0, CxRomSize);
  memset(pCxRomPeripheral, 0, CxRomSize);

  if (ROM_SIZE == Apple2eRomSize) {
    memcpy(pCxRomInternal, pData, CxRomSize);
    pData += CxRomSize;
    ROM_SIZE -= CxRomSize;
  }

  assert(ROM_SIZE == Apple2RomSize);
  memcpy(memrom, pData, Apple2RomSize);  // ROM at $D000...$FFFF

  const uint32_t uSlot = 0;
  RegisterIoHandler(uSlot, MemSetPaging, MemSetPaging, nullptr, nullptr,
                    nullptr, nullptr);

  MemReset();
  return 0;
}

auto MemReset() -> void {
  memset(memshadow, 0, NUM_PAGES_64K * sizeof(uint8_t*));
  memset(memwrite, 0, NUM_PAGES_64K * sizeof(uint8_t*));

  if (memaux) memset(memaux, 0, MEMORY_64K);
  if (memmain) memset(memmain, 0, MEMORY_64K);

  int iByte = 0;

  if (g_eMemoryInitPattern == MIP_FF_FF_00_00) {
    for (iByte = 0x0000; iByte < IO_RANGE_BEGIN;) {
      memmain[iByte++] = 0xFF;
      memmain[iByte++] = 0xFF;
      iByte++;
      iByte++;
    }
  }

  SetMem(memimage);
  ResetPaging(true);

  // Initialize & reset the cpu
  // . Do this after ROM has been copied back to mem[], so that PC is correctly
  // init'ed from 6502's reset vector
  CpuInitialize();
}

// Call by:
// . Soft-reset (Ctrl+Reset)
// . Snapshot_LoadState()
auto MemResetPaging() -> void { ResetPaging(false); }

// Called by Disk][ I/O only
auto MemReturnRandomData(uint8_t highbit) -> uint8_t {
  static const uint8_t RANDOM_DATA_VALUES_COUNT = 16;
  static const uint8_t retval[RANDOM_DATA_VALUES_COUNT] = {
      0x00, 0x2D, 0x2D, 0x30, 0x30, 0x32, 0x32, 0x34,
      0x35, 0x39, 0x43, 0x43, 0x43, 0x60, 0x7F, 0x7F};
  const uint8_t PROBABILITY_2_3_THRESHOLD = 170;
  const uint8_t RANDOM_DATA_BASE_VALUE = 0x20;

  auto r = static_cast<uint8_t>(rand() & 0xFF);
  if (r <= PROBABILITY_2_3_THRESHOLD) {
    return RANDOM_DATA_BASE_VALUE | (highbit ? 0x80 : 0);
  } else {
    return retval[r & (RANDOM_DATA_VALUES_COUNT - 1)] | (highbit ? 0x80 : 0);
  }
}

auto MemReadFloatingBus(const uint32_t uExecutedCycles) -> uint8_t {
  return *(mem + VideoGetScannerAddress(nullptr, uExecutedCycles));
}

auto MemReadFloatingBus(const uint8_t highbit, const uint32_t uExecutedCycles)
    -> uint8_t {
  uint8_t r = *(mem + VideoGetScannerAddress(nullptr, uExecutedCycles));
  return (r & ~0x80) | ((highbit) ? 0x80 : 0);
}

auto MemSetPaging(uint16_t programcounter, uint16_t address, uint8_t write,
                  uint8_t value, uint32_t nCyclesLeft) -> uint8_t {
  address &= 0xFF;
  uint32_t lastmemmode = memmode;

  // Determine the new memory paging mode.
  if ((address >= SS_LC_BEGIN) && (address <= SS_LC_END)) {
    bool writeram = (address & 1);
    memmode &= ~(MF_HRAM_BANK2 | MF_HIGHRAM | MF_HRAM_WRITE);
    {
      lastwriteram =
          true;  // note: because diags.do doesn't set switches twice!
      if (lastwriteram && writeram) {
        memmode |= MF_HRAM_WRITE;
      }
      if (!(address & 8)) {
        memmode |= MF_HRAM_BANK2;
      }
      if (((address & 2) >> 1) == (address & 1)) {
        memmode |= MF_HIGHRAM;
      }
    }
    lastwriteram = writeram;
  } else if (!IS_APPLE2()) {
    switch (address) {
      case SS_80STORE_OFF:
        memmode &= ~MF_80STORE;
        break;
      case SS_80STORE_ON:
        memmode |= MF_80STORE;
        break;
      case SS_AUXREAD_OFF:
        memmode &= ~MF_AUXREAD;
        break;
      case SS_AUXREAD_ON:
        memmode |= MF_AUXREAD;
        break;
      case SS_AUXWRITE_OFF:
        memmode &= ~MF_AUXWRITE;
        break;
      case SS_AUXWRITE_ON:
        memmode |= MF_AUXWRITE;
        break;
      case SS_SLOTCXROM_ON:
        memmode |= MF_SLOTCXROM;
        break;
      case SS_SLOTCXROM_OFF:
        memmode &= ~MF_SLOTCXROM;
        break;
      case SS_ALTZP_OFF:
        memmode &= ~MF_ALTZP;
        break;
      case SS_ALTZP_ON:
        memmode |= MF_ALTZP;
        break;
      case SS_SLOTC3ROM_OFF:
        memmode &= ~MF_SLOTC3ROM;
        break;
      case SS_SLOTC3ROM_ON:
        memmode |= MF_SLOTC3ROM;
        break;
      case SS_PAGE2_OFF:
        memmode &= ~MF_PAGE2;
        break;
      case SS_PAGE2_ON:
        memmode |= MF_PAGE2;
        break;
      case SS_HIRES_OFF:
        memmode &= ~MF_HIRES;
        break;
      case SS_HIRES_ON:
        memmode |= MF_HIRES;
        break;
#ifdef RAMWORKS
      case SS_RW_AUX_PAGE:
      case SS_RW_III_PAGE:
        if ((value < g_uMaxExPages) && (value < MAX_RAMWORKS_PAGES) &&
            RWpages[value]) {
          g_uActiveBank = value;
          memaux = RWpages[value];
          MemUpdatePaging(false, false);
        }
        break;
#endif
      default:
        break;
    }
  }

  // If the emulated program has just update the memory write mode and is
  // about to update the memory read mode, hold off on any processing until it
  // does so.
  if ((address >= 4) && (address <= 5) && (programcounter <= 0xFFFC) &&
      ((read_uint32_le(mem + programcounter) & 0x00FFFEFF) == 0x00C0028D)) {
    modechanging = true;
    return write ? 0 : MemReadFloatingBus(1, nCyclesLeft);
  }
  if ((address >= 0x80) && (address <= 0x8F) && (programcounter <= 0xFFFC) &&
      (((read_uint32_le(mem + programcounter) & 0x00FFFEFF) == 0x00C0048D) ||
       ((read_uint32_le(mem + programcounter) & 0x00FFFEFF) == 0x00C0028D))) {
    modechanging = true;
    return write ? 0 : MemReadFloatingBus(1, nCyclesLeft);
  }

  // If the memory paging mode has changed, update our memory images and write
  // tables.
  if ((lastmemmode != memmode) || modechanging) {
    modechanging = false;

    if ((lastmemmode & MF_SLOTCXROM) != (memmode & MF_SLOTCXROM)) {
      if (SW_SLOTCXROM) {
        // Disable Internal ROM
        // . Similar to $CFFF access
        // . None of the peripheral cards can be driving the bus - so use the
        // null ROM
        memset(pCxRomPeripheral + FIRMWARE_EXPANSION_SIZE, 0,
               FIRMWARE_EXPANSION_SIZE);
        memset(mem + FIRMWARE_EXPANSION_BEGIN, 0, FIRMWARE_EXPANSION_SIZE);
        g_eExpansionRomType = eExpRomNull;
        g_uPeripheralRomSlot = 0;
      } else {
        // Enable Internal ROM
        memcpy(mem + FIRMWARE_EXPANSION_BEGIN,
               pCxRomInternal + FIRMWARE_EXPANSION_SIZE,
               FIRMWARE_EXPANSION_SIZE);
        g_eExpansionRomType = eExpRomInternal;
        g_uPeripheralRomSlot = 0;
      }
    }

    MemUpdatePaging(false, false);
  }

  if ((address <= 1) || ((address >= 0x54) && (address <= 0x57))) {
    return VideoSetMode(programcounter, address, write, value, nCyclesLeft);
  }

  return write ? 0 : MemReadFloatingBus(nCyclesLeft);
}

auto MemGetSlotParameters(uint32_t uSlot) -> void* {
  if (uSlot >= NUM_SLOTS) {
    return nullptr;
  }
  return SlotParameters[uSlot];
}

auto MemGetSnapshot(SS_BaseMemory* pSS) -> uint32_t {
  pSS->mem_mode = memmode;
  pSS->last_write_ram = lastwriteram;

  for (uint32_t dwOffset = 0x0000; dwOffset < MEMORY_64K;
       dwOffset += PAGE_SIZE) {
    memcpy(pSS->mem_main + dwOffset,
           MemGetMainPtr(static_cast<uint16_t>(dwOffset)), PAGE_SIZE);
    memcpy(pSS->mem_aux + dwOffset,
           MemGetAuxPtr(static_cast<uint16_t>(dwOffset)), PAGE_SIZE);
  }

  return 0;
}

auto MemSetSnapshot(SS_BaseMemory* pSS) -> uint32_t {
  memmode = pSS->mem_mode;
  lastwriteram = pSS->last_write_ram;
  memcpy(memmain, pSS->mem_main, mem_main_size);
  memcpy(memaux, pSS->mem_aux, mem_aux_size);
  modechanging = false;
  MemUpdatePaging(true, false);  // Initialize=1, UpdateWriteOnly=0

  return 0;
}

// Modern snake_case aliases
auto mem_get_active_context() -> MemoryInstance_t* {
  return MemGetActiveContext();
}
auto mem_set_active_context(MemoryInstance_t* context) -> void {
  MemSetActiveContext(context);
}
auto register_io_handler(uint32_t slot, iofunction io_read_c0,
                         iofunction io_write_c0, iofunction io_read_cx,
                         iofunction io_write_cx, void* slot_parameter,
                         uint8_t* expansion_rom) -> void {
  RegisterIoHandler(slot, io_read_c0, io_write_c0, io_read_cx, io_write_cx,
                    slot_parameter, expansion_rom);
}
auto register_direct_io_handler(uint16_t addr, iofunction read,
                                iofunction write, void* instance) -> void {
  RegisterDirectIoHandler(addr, read, write, instance);
}
auto mem_destroy() -> void { MemDestroy(); }
auto mem_get_80store() -> bool { return MemGet80Store(); }
auto mem_check_slotcxrom() -> bool { return MemCheckSLOTCXROM(); }
auto mem_get_aux_ptr(uint16_t addr) -> uint8_t* { return MemGetAuxPtr(addr); }
auto mem_get_main_ptr(uint16_t addr) -> uint8_t* { return MemGetMainPtr(addr); }
auto mem_get_cx_rom_peripheral() -> uint8_t* { return MemGetCxRomPeripheral(); }
auto get_mem_ptr(uint16_t addr) -> uint8_t* { return GetMemPtr(addr); }
auto mem_get_bank_ptr(const uint32_t bank) -> uint8_t* {
  return MemGetBankPtr(bank);
}
auto get_mem_mode() -> uint32_t { return GetMemMode(); }
auto set_mem_mode(uint32_t mode) -> void { SetMemMode(mode); }
auto mem_is_addr_code_memory(const uint16_t addr) -> bool {
  return MemIsAddrCodeMemory(addr);
}
auto mem_pre_initialize() -> void { MemPreInitialize(); }
auto mem_initialize() -> int { return MemInitialize(); }
auto mem_read_floating_bus(const uint32_t executed_cycles) -> uint8_t {
  return MemReadFloatingBus(executed_cycles);
}
auto mem_read_floating_bus(const uint8_t highbit,
                           const uint32_t executed_cycles) -> uint8_t {
  return MemReadFloatingBus(highbit, executed_cycles);
}
auto mem_reset() -> void { MemReset(); }
auto mem_reset_paging() -> void { MemResetPaging(); }
auto mem_return_random_data(uint8_t highbit) -> uint8_t {
  return MemReturnRandomData(highbit);
}
auto mem_set_fast_paging(bool enable) -> void { (void)enable; }
auto mem_set_80store(bool enable) -> void { (void)enable; }
auto mem_trim_images() -> void {}
auto mem_get_slot_parameters(uint32_t slot) -> void* {
  return MemGetSlotParameters(slot);
}
auto mem_get_snapshot(SS_BaseMemory* snapshot) -> uint32_t {
  return MemGetSnapshot(snapshot);
}
auto mem_set_snapshot(SS_BaseMemory* snapshot) -> uint32_t {
  return MemSetSnapshot(snapshot);
}
auto io_null(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
             uint32_t cycles) -> uint8_t {
  return IO_Null(pc, addr, write, val, cycles);
}
auto mem_update_paging(bool initialize, bool updatewriteonly) -> void {
  MemUpdatePaging(initialize, updatewriteonly);
}
auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                     uint32_t cycles) -> uint8_t {
  return IOMap_Dispatch(pc, addr, write, val, cycles);
}
auto mem_check_paging(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                      uint32_t cycles) -> uint8_t {
  return MemCheckPaging(pc, addr, write, val, cycles);
}
auto mem_set_paging(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                    uint32_t cycles) -> uint8_t {
  return MemSetPaging(pc, addr, write, val, cycles);
}
auto get_ramworks_active_bank() -> uint32_t { return GetRamWorksActiveBank(); }

auto MemSetFastPaging(bool enable) -> void { mem_set_fast_paging(enable); }
auto MemSet80Store(bool enable) -> void { mem_set_80store(enable); }
auto MemTrimImages() -> void { mem_trim_images(); }

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-no-malloc,
// cppcoreguidelines-owning-memory, cppcoreguidelines-pro-type-reinterpret-cast,
// bugprone-easily-swappable-parameters, bugprone-branch-clone,
// cppcoreguidelines-macro-usage, modernize-use-auto,
// cppcoreguidelines-init-variables,
// cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
