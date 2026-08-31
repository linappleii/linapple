// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/Memory.h"

#include <sys/mman.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "core/Log.h"
#include "core/Resource.h"

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
MemoryInitPattern_e g_memory_init_pattern = MIP_FF_FF_00_00;

static auto SetMem(uint8_t* val) -> void {
  mem = val;
  if (g_active_memory) g_active_memory->mem = val;
}
static auto SetMemDirty(uint8_t* val) -> void {
  memdirty = val;
  if (g_active_memory) g_active_memory->memdirty = val;
}

MemoryInstance_t::~MemoryInstance_t() {
  if (this->memimage != nullptr) {
    munlock(this->memimage, MEMORY_64K);
  }
  free(this->memaux_allocated);
  this->memaux_allocated = nullptr;
  free(this->memmain);
  this->memmain = nullptr;
  free(this->memdirty);
  this->memdirty = nullptr;
  free(this->memrom);
  this->memrom = nullptr;
  free(this->memimage);
  this->memimage = nullptr;
  free(this->cx_rom_internal);
  this->cx_rom_internal = nullptr;
  free(this->cx_rom_peripheral);
  this->cx_rom_peripheral = nullptr;

#ifdef RAMWORKS
  for (uint32_t i = 0; i < MAX_RAMWORKS_PAGES; ++i) {
    if (this->rw_pages[i] != nullptr) {
      free(this->rw_pages[i]);
      this->rw_pages[i] = nullptr;
    }
  }
#endif
}

#define memaux (g_active_memory->memaux)
#define memaux_allocated (g_active_memory->memaux_allocated)
#define memmain (g_active_memory->memmain)
#define memrom (g_active_memory->memrom)
#define memimage (g_active_memory->memimage)
#define cx_rom_internal (g_active_memory->cx_rom_internal)
#define cx_rom_peripheral (g_active_memory->cx_rom_peripheral)
#define memshadow (g_active_memory->memshadow)
#define SlotParameters (g_active_memory->slot_parameters)
#define lastwriteram (g_active_memory->last_write_ram)
#define memmode (g_active_memory->mem_mode)
#define modechanging (g_active_memory->mode_changing)
#define g_active_bank (g_active_memory->active_bank)
#define g_expansion_rom_type (g_active_memory->expansion_rom_type)
#define g_peripheral_rom_slot (g_active_memory->peripheral_rom_slot)
#define IO_SELECT (g_active_memory->io_select)
#define IO_SELECT_InternalROM (g_active_memory->io_select_internal_rom)
#define ExpansionRom (g_active_memory->expansion_rom)
#ifdef RAMWORKS
#define RWpages (g_active_memory->rw_pages)
#endif

auto mem_get_active_context() -> MemoryInstance_t* { return g_active_memory; }

auto mem_set_active_context(MemoryInstance_t* context) -> void {
  if (!context) return;
  g_active_memory = context;
  IORead = context->io_read;
  IOWrite = context->io_write;
  memwrite = context->memwrite;
  mem = context->mem;
  memdirty = context->memdirty;
}

auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                     uint32_t cycles) -> uint8_t {
  if (addr < IO_RANGE_BEGIN || addr > IO_RANGE_END) {
    return io_null(pc, addr, write, d, cycles);
  }
  if ((addr & PAGE_MASK) == IO_RANGE_BEGIN) {
    uint8_t index = static_cast<uint8_t>(addr & 0xFF);
    if (write) {
      if (IOWrite[index] != nullptr) {
        return IOWrite[index](pc, addr, write, d, cycles);
      }
    } else {
      if (IORead[index] != nullptr) {
        return IORead[index](pc, addr, write, d, cycles);
      }
    }
  } else {
    uint8_t page = static_cast<uint8_t>((addr >> 8) & ADDR_NIBBLE_MASK);
    if (write) {
      if (IOWrite[NUM_PAGES_64K + page] != nullptr) {
        return IOWrite[NUM_PAGES_64K + page](pc, addr, write, d, cycles);
      }
    } else {
      if (IORead[NUM_PAGES_64K + page] != nullptr) {
        return IORead[NUM_PAGES_64K + page](pc, addr, write, d, cycles);
      }
    }
  }
  return io_null(pc, addr, write, d, cycles);
}

#ifdef RAMWORKS
uint32_t g_max_ex_pages = 1;
#endif

auto get_ramworks_active_bank() -> uint32_t { return g_active_bank; }

auto IO_Annunciator(uint16_t programcounter, uint16_t address, uint8_t write,
                    uint8_t value, uint32_t cycles) -> uint8_t;

auto mem_update_paging(bool initialize, bool updatewriteonly) -> void;

static auto IORead_C00x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                        uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  // $C000-$C00F are owned by the keyboard peripheral once initialized.
  // RegisterDirectIO overwrites these dispatch slots before any CPU execution.
  return mem_read_floating_bus(cycles_left);
}

static const uint8_t LAST_MEM_SOFT_SWITCH_OFFSET = 0x0B;

static auto IOWrite_C00x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  if ((addr & ADDR_NIBBLE_MASK) <= LAST_MEM_SOFT_SWITCH_OFFSET) {
    return mem_set_paging(pc, addr, write, d, cycles_left);
  } else {
    return video_set_mode(pc, addr, write, d, cycles_left);
  }
}

static auto IORead_C01x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                        uint32_t cycles_left) -> uint8_t {
  switch (addr & ADDR_NIBBLE_MASK) {
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
    case 0x8:
      return mem_check_paging(pc, addr, write, d, cycles_left);
    case 0x9:
      return video_check_vbl(pc, addr, write, d, cycles_left);
    case 0xA:
    case 0xB:
      return video_check_mode(pc, addr, write, d, cycles_left);
    case 0xC:
    case 0xD:
      return mem_check_paging(pc, addr, write, d, cycles_left);
    case 0xE:
    case 0xF:
      return video_check_mode(pc, addr, write, d, cycles_left);
    default:
      break;
  }
  return 0;
}

static auto IOWrite_C01x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  return mem_read_floating_bus(cycles_left);
}

static auto IORead_C02x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                        uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  return mem_read_floating_bus(cycles_left);
}

static auto IOWrite_C02x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  (void)cycles_left;
  return 0;
}

static auto IORead_C03x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                        uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  return mem_read_floating_bus(cycles_left);
}

static auto IOWrite_C03x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  return mem_read_floating_bus(cycles_left);
}

static auto IORead_C04x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                        uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  return mem_read_floating_bus(cycles_left);
}

static auto IOWrite_C04x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  (void)cycles_left;
  return 0;
}

static auto IORead_C05x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                        uint32_t cycles_left) -> uint8_t {
  switch (addr & ADDR_NIBBLE_MASK) {
    case SS_TEXT_OFF& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_TEXT_ON& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_MIXED_OFF& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_MIXED_ON& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_PAGE2_OFF& ADDR_NIBBLE_MASK:
      return mem_set_paging(pc, addr, write, d, cycles_left);
    case SS_PAGE2_ON& ADDR_NIBBLE_MASK:
      return mem_set_paging(pc, addr, write, d, cycles_left);
    case SS_HIRES_OFF& ADDR_NIBBLE_MASK:
      return mem_set_paging(pc, addr, write, d, cycles_left);
    case SS_HIRES_ON& ADDR_NIBBLE_MASK:
      return mem_set_paging(pc, addr, write, d, cycles_left);
    case SS_AN0_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN0_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN1_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN1_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN2_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN2_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN3_OFF& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_AN3_ON& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    default:
      break;
  }

  return 0;
}

static auto IOWrite_C05x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  switch (addr & ADDR_NIBBLE_MASK) {
    case SS_TEXT_OFF& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_TEXT_ON& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_MIXED_OFF& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_MIXED_ON& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_PAGE2_OFF& ADDR_NIBBLE_MASK:
      return mem_set_paging(pc, addr, write, d, cycles_left);
    case SS_PAGE2_ON& ADDR_NIBBLE_MASK:
      return mem_set_paging(pc, addr, write, d, cycles_left);
    case SS_HIRES_OFF& ADDR_NIBBLE_MASK:
      return mem_set_paging(pc, addr, write, d, cycles_left);
    case SS_HIRES_ON& ADDR_NIBBLE_MASK:
      return mem_set_paging(pc, addr, write, d, cycles_left);
    case SS_AN0_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN0_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN1_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN1_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN2_OFF& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN2_ON& ADDR_NIBBLE_MASK:
      return IO_Annunciator(pc, addr, write, d, cycles_left);
    case SS_AN3_OFF& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_AN3_ON& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    default:
      break;
  }

  return 0;
}

static auto IORead_C06x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                        uint32_t cycles_left) -> uint8_t {
  return io_null(pc, addr, write, d, cycles_left);
}

static auto IOWrite_C06x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  return io_null(pc, addr, write, d, cycles_left);
}

static auto IORead_C07x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                        uint32_t cycles_left) -> uint8_t {
  if ((addr & 0xF) == 0xF) {
    return video_check_mode(pc, addr, write, d, cycles_left);
  }
  return io_null(pc, addr, write, d, cycles_left);
}

static auto IOWrite_C07x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  switch (addr & 0xf) {
    case 0x0:
      return io_null(pc, addr, write, d, cycles_left);
#ifdef RAMWORKS
    case 0x1:
      return mem_set_paging(pc, addr, write, d, cycles_left);
    case 0x2:
      return io_null(pc, addr, write, d, cycles_left);
    case 0x3:
      return mem_set_paging(pc, addr, write, d, cycles_left);
#else
    case 0x1:
      return io_null(pc, addr, write, d, cycles_left);
    case 0x2:
      return io_null(pc, addr, write, d, cycles_left);
    case 0x3:
      return io_null(pc, addr, write, d, cycles_left);
#endif
    case 0x4:
      return io_null(pc, addr, write, d, cycles_left);
    case 0x5:
      return io_null(pc, addr, write, d, cycles_left);
    case 0x6:
      return io_null(pc, addr, write, d, cycles_left);
    case 0x7:
      return io_null(pc, addr, write, d, cycles_left);
    case 0x8:
      return io_null(pc, addr, write, d, cycles_left);
    case 0x9:
      return io_null(pc, addr, write, d, cycles_left);
    case 0xA:
      return io_null(pc, addr, write, d, cycles_left);
    case 0xB:
      return io_null(pc, addr, write, d, cycles_left);
    case 0xC:
      return io_null(pc, addr, write, d, cycles_left);
    case 0xD:
      return io_null(pc, addr, write, d, cycles_left);
    case 0xE:
      return io_null(pc, addr, write, d, cycles_left);
    case 0xF:
      return video_check_mode(pc, addr, write, d, cycles_left);
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

auto io_null(uint16_t programcounter, uint16_t address, uint8_t write,
             uint8_t value, uint32_t cycles_left) -> uint8_t {
  (void)value;
  (void)programcounter;
  (void)address;
  if (!write) {
    return mem_read_floating_bus(cycles_left);
  }
  return 0;
}

auto IO_Annunciator(uint16_t programcounter, uint16_t address, uint8_t write,
                    uint8_t value, uint32_t cycles_left) -> uint8_t {
  (void)value;
  (void)cycles_left;
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
                 uint8_t value, uint32_t cycles_left) -> uint8_t {
  if (address == 0xCFFF) {
    // Disable expansion ROM at [$C800..$CFFF]
    // . SSC will disable on an access to $CFxx - but ROM only writes to $CFFF,
    // so it doesn't matter
    IO_SELECT = 0;
    IO_SELECT_InternalROM = 0;
    g_peripheral_rom_slot = 0;

    if (SW_SLOTCXROM) {
      // NB. SW_SLOTCXROM==0 ensures that internal rom stays switched in
      memset(cx_rom_peripheral + FIRMWARE_EXPANSION_SIZE, 0,
             FIRMWARE_EXPANSION_SIZE);
      memset(mem + FIRMWARE_EXPANSION_BEGIN, 0, FIRMWARE_EXPANSION_SIZE);
      g_expansion_rom_type = eExpRomNull;
    }
    // NB. IO_SELECT won't get set, so ROM won't be switched back in...
  }

  uint8_t IO_STROBE = 0;

  if (IS_APPLE2() || SW_SLOTCXROM) {
    if ((address >= 0xC100) && (address <= 0xC7FF)) {
      const uint32_t slot = (address >> 8) & 0xF;
      if (slot < NUM_SLOTS) {
        if ((slot != 3) && ExpansionRom[slot]) {
          IO_SELECT |= 1 << slot;
        } else if ((SW_SLOTC3ROM) && ExpansionRom[slot]) {
          IO_SELECT |= 1 << slot;  // Slot3 & Peripheral ROM
        } else if (!SW_SLOTC3ROM) {
          IO_SELECT_InternalROM = 1;  // Slot3 & Internal ROM
        }
      }
    } else if ((address >= 0xC800) && (address <= 0xCFFF)) {
      IO_STROBE = 1;
    }

    if (IO_SELECT && IO_STROBE) {
      // Enable Peripheral Expansion ROM
      uint32_t slot = 1;
      for (; slot < NUM_SLOTS; slot++) {
        if (IO_SELECT & (1 << slot)) {
          break;
        }
      }

      if ((slot < NUM_SLOTS) && ExpansionRom[slot] &&
          (g_peripheral_rom_slot != slot)) {
        if (cx_rom_peripheral != nullptr) {
          memcpy(cx_rom_peripheral + FIRMWARE_EXPANSION_SIZE,
                 ExpansionRom[slot], FIRMWARE_EXPANSION_SIZE);
        }
        if (mem != nullptr) {
          memcpy(mem + FIRMWARE_EXPANSION_BEGIN, ExpansionRom[slot],
                 FIRMWARE_EXPANSION_SIZE);
        }
        g_expansion_rom_type = eExpRomPeripheral;
        g_peripheral_rom_slot = slot;
      }
    } else if (IO_SELECT_InternalROM && IO_STROBE &&
               (g_expansion_rom_type != eExpRomInternal)) {
      // Enable Internal ROM
      // . Get this for PR#3
      if (cx_rom_internal != nullptr && mem != nullptr) {
        memcpy(mem + FIRMWARE_EXPANSION_BEGIN,
               cx_rom_internal + FIRMWARE_EXPANSION_SIZE,
               FIRMWARE_EXPANSION_SIZE);
      }
      g_expansion_rom_type = eExpRomInternal;
      g_peripheral_rom_slot = 0;
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
        (g_expansion_rom_type != eExpRomInternal)) {
      // Enable Internal ROM
      if (cx_rom_internal != nullptr && mem != nullptr) {
        memcpy(mem + FIRMWARE_EXPANSION_BEGIN,
               cx_rom_internal + FIRMWARE_EXPANSION_SIZE,
               FIRMWARE_EXPANSION_SIZE);
      }
      g_expansion_rom_type = eExpRomInternal;
      g_peripheral_rom_slot = 0;
    }
  }

  if ((g_expansion_rom_type == eExpRomNull) && (address >= 0xC800)) {
    return io_null(programcounter, address, write, value, cycles_left);
  } else {
    return mem ? mem[address] : mem_read_floating_bus(cycles_left);
  }
}

auto IOWrite_Cxxx(uint16_t programcounter, uint16_t address, uint8_t write,
                  uint8_t value, uint32_t cycles_left) -> uint8_t {
  (void)value;
  (void)cycles_left;
  (void)programcounter;
  (void)address;
  (void)write;
  return 0;
}

static uint8_t g_bm_slot_init = 0;

static auto InitIoHandlers() -> void {
  g_bm_slot_init = 0;
  uint32_t i = 0;

  for (i = 0; i < 512; i++) {
    IORead[i] = io_null;
    IOWrite[i] = io_null;
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
  g_expansion_rom_type = eExpRomNull;
  g_peripheral_rom_slot = 0;

  for (i = 0; i < NUM_SLOTS; i++) {
    ExpansionRom[i] = nullptr;
  }
}

// All slots [0..7] must register their handlers
auto register_io_handler(uint32_t slot, iofunction IOReadC0,
                         iofunction IOWriteC0, iofunction IOReadCx,
                         iofunction IOWriteCx, void* slot_parameter,
                         uint8_t* expansion_rom) -> void {
  if (slot >= NUM_SLOTS) {
    return;
  }
  g_bm_slot_init |= 1U << slot;
  SlotParameters[slot] = slot_parameter;

  uint16_t index = static_cast<uint16_t>(0x80 + (slot << 4));
  for (uint32_t i = 0; i < 16; i++) {
    IORead[index + i] = IOReadC0;
    IOWrite[index + i] = IOWriteC0;
  }

  if (slot == 0) {
    return;
  }

  if (IOReadCx == nullptr) {
    IOReadCx = IORead_Cxxx;
  }
  if (IOWriteCx == nullptr) {
    IOWriteCx = IOWrite_Cxxx;
  }

  IORead[NUM_PAGES_64K + slot] = IOReadCx;
  IOWrite[NUM_PAGES_64K + slot] = IOWriteCx;

  ExpansionRom[slot] = expansion_rom;
}

auto register_direct_io_handler(uint16_t addr, iofunction read,
                                iofunction write, void* instance) -> void {
  if ((addr & 0xFF00) != 0xC000) return;
  uint8_t index = static_cast<uint8_t>(addr & 0xFF);

  if (read) IORead[index] = read;
  if (write) IOWrite[index] = write;

  (void)instance;
}
//===========================================================================

auto get_mem_mode() -> uint32_t { return memmode; }

auto set_mem_mode(uint32_t new_mem_mode) -> void { memmode = new_mem_mode; }

static auto ResetPaging(bool initialize) -> void {
  lastwriteram = false;
  memmode = MF_HRAM_BANK2 | MF_SLOTCXROM | MF_HRAM_WRITE;
  mem_update_paging(initialize, false);
}

auto mem_update_paging(bool initialize, bool updatewriteonly) -> void {
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
      const uint32_t slot_offset = (loop & 0x0f) * PAGE_SIZE;
      uint8_t* base = nullptr;
      if (loop == PAGE_C3) {
        base = (SW_SLOTC3ROM && SW_SLOTCXROM) ? cx_rom_peripheral
                                              : cx_rom_internal;
      } else {
        base = SW_SLOTCXROM ? cx_rom_peripheral : cx_rom_internal;
      }
      memshadow[loop] = base ? (base + slot_offset) : (mem + (loop << 8));
    }

    for (loop = PAGE_C8; loop < PAGE_D0; loop++) {
      const uint32_t rom_offset = (loop & 0x0f) * PAGE_SIZE;
      memshadow[loop] = cx_rom_internal ? (cx_rom_internal + rom_offset)
                                        : (mem + (loop << 8));
    }
  }

  for (loop = PAGE_D0; loop < PAGE_E0; loop++) {
    int bankoffset = (SW_HRAM_BANK2 ? 0 : LC_BANK_SIZE);
    memshadow[loop] =
        SW_HIGHRAM
            ? SW_ALTZP ? (memaux ? memaux + (loop << 8) - bankoffset
                                 : mem + (loop << 8))
                       : (memmain ? memmain + (loop << 8) - bankoffset
                                  : mem + (loop << 8))
            : (memrom ? memrom +
                            (static_cast<size_t>((loop - PAGE_D0) * PAGE_SIZE))
                      : (mem + (loop << 8)));

    memwrite[loop] = SW_HRAM_WRITE
                         ? SW_HIGHRAM ? mem + (loop << 8)
                           : SW_ALTZP
                               ? (memaux ? memaux + (loop << 8) - bankoffset
                                         : mem + (loop << 8))
                               : (memmain ? memmain + (loop << 8) - bankoffset
                                          : mem + (loop << 8))
                         : nullptr;
  }

  for (loop = PAGE_E0; loop < PAGE_MAX; loop++) {
    memshadow[loop] =
        SW_HIGHRAM
            ? SW_ALTZP ? (memaux ? memaux + (loop << 8) : mem + (loop << 8))
                       : (memmain ? memmain + (loop << 8) : mem + (loop << 8))
            : (memrom ? memrom +
                            (static_cast<size_t>((loop - PAGE_D0) * PAGE_SIZE))
                      : (mem + (loop << 8)));

    memwrite[loop] =
        SW_HRAM_WRITE
            ? SW_HIGHRAM ? mem + (loop << 8)
              : SW_ALTZP ? (memaux ? memaux + (loop << 8) : mem + (loop << 8))
                         : (memmain ? memmain + (loop << 8) : mem + (loop << 8))
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

auto mem_check_paging(uint16_t programcounter, uint16_t address, uint8_t write,
                      uint8_t value, uint32_t cycles_left) -> uint8_t {
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
  return (mem_read_floating_bus(cycles_left) & 0x7F) | (result ? 0x80 : 0x00);
}

auto mem_destroy() -> void {
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
  free(cx_rom_internal);
  free(cx_rom_peripheral);

  memaux = nullptr;
  memaux_allocated = nullptr;
  memmain = nullptr;
  SetMemDirty(nullptr);
  memrom = nullptr;
  memimage = nullptr;

  cx_rom_internal = nullptr;
  cx_rom_peripheral = nullptr;

  SetMem(nullptr);

  memset(memwrite, 0, NUM_PAGES_64K * sizeof(uint8_t*));
  memset(memshadow, 0, NUM_PAGES_64K * sizeof(uint8_t*));
}

auto mem_get_80store() -> bool { return SW_80STORE != 0; }

auto mem_check_slotcxrom() -> bool { return SW_SLOTCXROM != 0; }

auto mem_get_aux_ptr(uint16_t offset) -> uint8_t* {
  uint8_t* result =
      (memshadow[(offset >> 8)] == (memaux + (offset & PAGE_MASK)))
          ? mem + offset
          : memaux + offset;

#ifdef RAMWORKS
  if (((SW_PAGE2 && SW_80STORE) || video_get_sw_80col()) &&
      ((((offset & PAGE_MASK) >= TXT1_BEGIN) &&
        ((offset & PAGE_MASK) <= TXT1_END_PAGE)) ||
       (SW_HIRES && ((offset & PAGE_MASK) >= HGR1_BEGIN) &&
        ((offset & PAGE_MASK) <= HGR1_END_PAGE)))) {
    if (RWpages[0] != nullptr) {
      result = (memshadow[(offset >> 8)] == (RWpages[0] + (offset & PAGE_MASK)))
                   ? mem + offset
                   : RWpages[0] + offset;
    }
  }
#endif

  return result;
}

auto mem_get_main_ptr(uint16_t offset) -> uint8_t* {
  return (memshadow[(offset >> 8)] == (memmain + (offset & 0xFF00)))
             ? mem + offset
             : memmain + offset;
}

//===========================================================================

auto mem_get_bank_ptr(const uint32_t bank) -> uint8_t* {
#ifdef RAMWORKS
  if (bank > g_max_ex_pages || bank >= MAX_RAMWORKS_PAGES) {
    return nullptr;
  }

  if (bank == 0) {
    return memmain;
  }

  return RWpages[bank - 1];
#else
  return (bank == 0) ? memmain : (bank == 1) ? memaux : nullptr;
#endif
}

auto mem_get_cx_rom_peripheral() -> uint8_t* { return cx_rom_peripheral; }

auto get_mem_ptr(uint16_t addr) -> uint8_t* { return mem + addr; }

//===========================================================================

// Post:
// . true:  code memory
// . false: I/O memory or floating bus
auto mem_is_addr_code_memory(const uint16_t addr) -> bool {
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
    const uint32_t slot = (addr >> 8) & 0x7;
    return (g_bm_slot_init & (1 << slot)) != 0;  // card present in this slot?
  }

  // [$C800..CFFF]
  if (g_expansion_rom_type == eExpRomNull) {
    if (IO_SELECT || IO_SELECT_InternalROM) {
      return true;
    }
    return false;
  }

  return true;
}

auto mem_pre_initialize() -> void { InitIoHandlers(); }

auto mem_initialize() -> int  // returns -1 if any error during initialization
{
  mem_destroy();

#ifdef RAMWORKS
  if (g_max_ex_pages > MAX_RAMWORKS_PAGES) {
    g_max_ex_pages = MAX_RAMWORKS_PAGES;
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
  cx_rom_internal = static_cast<uint8_t*>(malloc(CxRomSize));
  cx_rom_peripheral = static_cast<uint8_t*>(malloc(CxRomSize));

  if (!memaux || !memdirty || !memimage || !memmain || !memrom ||
      !cx_rom_internal || !cx_rom_peripheral) {
    Logger::error("Unable to allocate required memory buffers.");
    mem_destroy();
    return -1;
  }

  if (memaux) memset(memaux, 0, MEMORY_64K);
  if (memmain) memset(memmain, 0, MEMORY_64K);
  SetMem(memmain);
  if (memdirty) memset(memdirty, 0, NUM_PAGES_64K);
  if (memrom) memset(memrom, 0, ROM_BUFFER_SIZE);
  if (memimage) memset(memimage, 0, MEMORY_64K);

  if (mlock(memimage, MEMORY_64K) != 0) {
    Logger::warning("Failed to lock memory image from swapping.");
  }

  if (cx_rom_internal) memset(cx_rom_internal, 0, CxRomSize);
  if (cx_rom_peripheral) memset(cx_rom_peripheral, 0, CxRomSize);

#ifdef RAMWORKS
  RWpages[0] = static_cast<uint8_t*>(malloc(MEMORY_64K));
  if (RWpages[0]) {
    memset(RWpages[0], 0, MEMORY_64K);
    memaux = RWpages[0];
  }
  uint32_t i = 1;
  while (i < g_max_ex_pages && i < MAX_RAMWORKS_PAGES) {
    RWpages[i] = static_cast<uint8_t*>(malloc(MEMORY_64K));
    if (RWpages[i]) {
      memset(RWpages[i], 0, MEMORY_64K);
      i++;
    } else {
      break;
    }
  }
#endif

  mem_set_active_context(g_active_memory);

#define IDR_APPLE2_ROM "Apple2.rom"
#define IDR_APPLE2_PLUS_ROM "Apple2_Plus.rom"
#define IDR_APPLE2E_ROM "Apple2e.rom"
#define IDR_APPLE2E_ENHANCED_ROM "Apple2e_Enhanced.rom"

  uint32_t ROM_SIZE = 0;
  const char* RomFileName = nullptr;
  switch (g_apple2_type) {
    case A2TYPE_APPLE2:
      RomFileName = apple2_rom;
      ROM_SIZE = Apple2RomSize;
      break;
    case A2TYPE_APPLE2PLUS:
      RomFileName = apple2_plus_rom;
      ROM_SIZE = Apple2RomSize;
      break;
    case A2TYPE_APPLE2E:
      RomFileName = apple2e_rom;
      ROM_SIZE = Apple2eRomSize;
      break;
    case A2TYPE_APPLE2EENHANCED:
      RomFileName = apple2e_enhanced_rom;
      ROM_SIZE = Apple2eRomSize;
      break;
    default:
      break;
  }

  if (RomFileName == nullptr) {
    fprintf(stderr, "Unable to find rom for specified computer type! Sorry\n");
    return -1;
  }

  auto* data = reinterpret_cast<uint8_t*>(
      const_cast<char*>(RomFileName));  // NB. Don't need to unlock resource

  memset(cx_rom_internal, 0, CxRomSize);
  memset(cx_rom_peripheral, 0, CxRomSize);

  if (ROM_SIZE == Apple2eRomSize) {
    memcpy(cx_rom_internal, data, CxRomSize);
    data += CxRomSize;
    ROM_SIZE -= CxRomSize;
  }

  assert(ROM_SIZE == Apple2RomSize);
  memcpy(memrom, data, Apple2RomSize);  // ROM at $D000...$FFFF

  const uint32_t slot = 0;
  register_io_handler(slot, mem_set_paging, mem_set_paging, nullptr, nullptr,
                      nullptr, nullptr);

  mem_reset();
  return 0;
}

auto mem_reset() -> void {
  memset(memshadow, 0, NUM_PAGES_64K * sizeof(uint8_t*));
  memset(memwrite, 0, NUM_PAGES_64K * sizeof(uint8_t*));

  if (memaux) memset(memaux, 0, MEMORY_64K);
  if (memmain) memset(memmain, 0, MEMORY_64K);

  int byte = 0;

  if (g_memory_init_pattern == MIP_FF_FF_00_00) {
    for (byte = 0x0000; byte < IO_RANGE_BEGIN;) {
      memmain[byte++] = 0xFF;
      memmain[byte++] = 0xFF;
      byte++;
      byte++;
    }
  }

  SetMem(memimage);
  ResetPaging(true);

  // Initialize & reset the cpu
  // . Do this after ROM has been copied back to mem[], so that PC is correctly
  // init'ed from 6502's reset vector
  cpu_initialize();
}

// Call by:
// . Soft-reset (Ctrl+Reset)
// . Snapshot_LoadState()
auto mem_reset_paging() -> void { ResetPaging(false); }

// Called by Disk][ I/O only
auto mem_return_random_data(uint8_t highbit) -> uint8_t {
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

auto mem_read_floating_bus(const uint32_t executed_cycles) -> uint8_t {
  if (mem == nullptr) {
    return 0xFF;
  }
  return *(mem + video_get_scanner_address(nullptr, executed_cycles));
}

auto mem_read_floating_bus(const uint8_t highbit,
                           const uint32_t executed_cycles) -> uint8_t {
  uint8_t r = (mem != nullptr)
                  ? *(mem + video_get_scanner_address(nullptr, executed_cycles))
                  : 0xFF;
  return (r & ~0x80) | ((highbit) ? 0x80 : 0);
}

auto mem_set_paging(uint16_t programcounter, uint16_t address, uint8_t write,
                    uint8_t value, uint32_t cycles_left) -> uint8_t {
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
        if ((value < g_max_ex_pages) && (value < MAX_RAMWORKS_PAGES) &&
            RWpages[value]) {
          g_active_bank = value;
          memaux = RWpages[value];
          mem_update_paging(false, false);
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
    return write ? 0 : mem_read_floating_bus(1, cycles_left);
  }
  if ((address >= 0x80) && (address <= 0x8F) && (programcounter <= 0xFFFC) &&
      (((read_uint32_le(mem + programcounter) & 0x00FFFEFF) == 0x00C0048D) ||
       ((read_uint32_le(mem + programcounter) & 0x00FFFEFF) == 0x00C0028D))) {
    modechanging = true;
    return write ? 0 : mem_read_floating_bus(1, cycles_left);
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
        memset(cx_rom_peripheral + FIRMWARE_EXPANSION_SIZE, 0,
               FIRMWARE_EXPANSION_SIZE);
        memset(mem + FIRMWARE_EXPANSION_BEGIN, 0, FIRMWARE_EXPANSION_SIZE);
        g_expansion_rom_type = eExpRomNull;
        g_peripheral_rom_slot = 0;
      } else {
        // Enable Internal ROM
        if (cx_rom_internal != nullptr) {
          memcpy(mem + FIRMWARE_EXPANSION_BEGIN,
                 cx_rom_internal + FIRMWARE_EXPANSION_SIZE,
                 FIRMWARE_EXPANSION_SIZE);
        }
        g_expansion_rom_type = eExpRomInternal;
        g_peripheral_rom_slot = 0;
      }
    }

    mem_update_paging(false, false);
  }

  if ((address <= 1) || ((address >= 0x54) && (address <= 0x57))) {
    return video_set_mode(programcounter, address, write, value, cycles_left);
  }

  return write ? 0 : mem_read_floating_bus(cycles_left);
}

auto mem_get_slot_parameters(uint32_t slot) -> void* {
  if (slot >= NUM_SLOTS) {
    return nullptr;
  }
  return SlotParameters[slot];
}

auto mem_get_snapshot(SS_BaseMemory* ss) -> uint32_t {
  ss->mem_mode = memmode;
  ss->last_write_ram = lastwriteram ? 1 : 0;

  for (uint32_t offset = 0x0000; offset < MEMORY_64K; offset += PAGE_SIZE) {
    memcpy(ss->mem_main + offset,
           mem_get_main_ptr(static_cast<uint16_t>(offset)), PAGE_SIZE);
    memcpy(ss->mem_aux + offset, mem_get_aux_ptr(static_cast<uint16_t>(offset)),
           PAGE_SIZE);
  }

  return 0;
}

auto mem_set_snapshot(SS_BaseMemory* ss) -> uint32_t {
  memmode = ss->mem_mode;
  lastwriteram = (ss->last_write_ram != 0);
  memcpy(memmain, ss->mem_main, mem_main_size);
  memcpy(memaux, ss->mem_aux, mem_aux_size);
  modechanging = false;
  mem_update_paging(true, false);  // Initialize=1, UpdateWriteOnly=0

  return 0;
}

// NOLINTEND
