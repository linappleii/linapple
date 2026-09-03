// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/Memory.h"

#include <sys/mman.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "EmbeddedRoms.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "core/Log.h"
#include "core/Util_Endian.h"

// Unavoidable hardware architectural constraints for Apple II memory management
// unit and page table multiplexer
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-no-malloc,
// cppcoreguidelines-owning-memory, cppcoreguidelines-pro-type-reinterpret-cast,
// bugprone-easily-swappable-parameters, bugprone-branch-clone,
// cppcoreguidelines-macro-usage, modernize-use-auto,
// cppcoreguidelines-init-variables,
// cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
static inline auto sw_80store(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_80STORE) != 0;
}
static inline auto sw_altzp(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_ALTZP) != 0;
}
static inline auto sw_auxread(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_AUXREAD) != 0;
}
static inline auto sw_auxwrite(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_AUXWRITE) != 0;
}
static inline auto sw_hram_bank2(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_HRAM_BANK2) != 0;
}
static inline auto sw_highram(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_HIGHRAM) != 0;
}
static inline auto sw_hires(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_HIRES) != 0;
}
static inline auto sw_page2(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_PAGE2) != 0;
}
static inline auto sw_slotc3rom(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_SLOTC3ROM) != 0;
}
static inline auto sw_slotcxrom(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_SLOTCXROM) != 0;
}
static inline auto sw_hram_write(const MemoryInstance_t* ctx) -> bool {
  return (ctx->mem_mode & MF_HRAM_WRITE) != 0;
}

static MemoryInstance_t g_default_memory_context;
static MemoryInstance_t* g_active_memory = &g_default_memory_context;

iofunction* IORead = g_default_memory_context.io_read;
iofunction* IOWrite = g_default_memory_context.io_write;
uint8_t** memwrite = g_default_memory_context.memwrite;
uint8_t* mem = nullptr;
uint8_t* memdirty = nullptr;
MemoryInitPattern_e g_memory_init_pattern = MIP_FF_FF_00_00;

static auto set_mem(uint8_t* val) -> void {
  mem = val;
  if (g_active_memory) g_active_memory->mem = val;
}
static auto set_mem_dirty(uint8_t* val) -> void {
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

auto get_ramworks_active_bank() -> uint32_t {
  return g_active_memory->active_bank;
}

auto io_annunciator(uint16_t programcounter, uint16_t address, uint8_t write,
                    uint8_t value, uint32_t cycles) -> uint8_t;

auto mem_update_paging(bool initialize, bool updatewriteonly) -> void;

static auto io_read_c00x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
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

static auto io_write_c00x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                          uint32_t cycles_left) -> uint8_t {
  if ((addr & ADDR_NIBBLE_MASK) <= LAST_MEM_SOFT_SWITCH_OFFSET) {
    return mem_set_paging(pc, addr, write, d, cycles_left);
  } else {
    return video_set_mode(pc, addr, write, d, cycles_left);
  }
}

static auto io_read_c01x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
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

static auto io_write_c01x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                          uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  return mem_read_floating_bus(cycles_left);
}

static auto io_read_c02x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  return mem_read_floating_bus(cycles_left);
}

static auto io_write_c02x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                          uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  (void)cycles_left;
  return 0;
}

static auto io_read_c03x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  return mem_read_floating_bus(cycles_left);
}

static auto io_write_c03x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                          uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  return mem_read_floating_bus(cycles_left);
}

static auto io_read_c04x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  return mem_read_floating_bus(cycles_left);
}

static auto io_write_c04x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                          uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)d;
  (void)cycles_left;
  return 0;
}

static auto io_read_c05x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
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
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN0_ON& ADDR_NIBBLE_MASK:
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN1_OFF& ADDR_NIBBLE_MASK:
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN1_ON& ADDR_NIBBLE_MASK:
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN2_OFF& ADDR_NIBBLE_MASK:
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN2_ON& ADDR_NIBBLE_MASK:
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN3_OFF& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_AN3_ON& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    default:
      break;
  }

  return 0;
}

static auto io_write_c05x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
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
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN0_ON& ADDR_NIBBLE_MASK:
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN1_OFF& ADDR_NIBBLE_MASK:
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN1_ON& ADDR_NIBBLE_MASK:
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN2_OFF& ADDR_NIBBLE_MASK:
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN2_ON& ADDR_NIBBLE_MASK:
      return io_annunciator(pc, addr, write, d, cycles_left);
    case SS_AN3_OFF& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    case SS_AN3_ON& ADDR_NIBBLE_MASK:
      return video_set_mode(pc, addr, write, d, cycles_left);
    default:
      break;
  }

  return 0;
}

static auto io_read_c06x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  return io_null(pc, addr, write, d, cycles_left);
}

static auto io_write_c06x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                          uint32_t cycles_left) -> uint8_t {
  return io_null(pc, addr, write, d, cycles_left);
}

static auto io_read_c07x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                         uint32_t cycles_left) -> uint8_t {
  if ((addr & 0xF) == 0xF) {
    return video_check_mode(pc, addr, write, d, cycles_left);
  }
  return io_null(pc, addr, write, d, cycles_left);
}

static auto io_write_c07x(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
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
    io_read_c00x,                // Keyboard
    io_read_c01x,                // Memory/Video
    io_read_c02x,                // Cassette
    io_read_c03x,                // Speaker
    io_read_c04x, io_read_c05x,  // Video
    io_read_c06x,                // Joystick
    io_read_c07x,                // Joystick/Video
};

static iofunction IOWrite_C0xx[8] = {
    io_write_c00x,                 // Memory/Video
    io_write_c01x,                 // Keyboard
    io_write_c02x,                 // Cassette
    io_write_c03x,                 // Speaker
    io_write_c04x, io_write_c05x,  // Video/Memory
    io_write_c06x, io_write_c07x,  // Joystick/Ramworks
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

auto io_annunciator(uint16_t programcounter, uint16_t address, uint8_t write,
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

auto io_read_cxxx(uint16_t programcounter, uint16_t address, uint8_t write,
                  uint8_t value, uint32_t cycles_left) -> uint8_t {
  if (address == 0xCFFF) {
    // Disable expansion ROM at [$C800..$CFFF]
    // . SSC will disable on an access to $CFxx - but ROM only writes to $CFFF,
    // so it doesn't matter
    g_active_memory->io_select = 0;
    g_active_memory->io_select_internal_rom = 0;
    g_active_memory->peripheral_rom_slot = 0;

    if (sw_slotcxrom(g_active_memory)) {
      // NB. sw_slotcxrom(g_active_memory)==0 ensures that internal rom stays
      // switched in
      memset(g_active_memory->cx_rom_peripheral + FIRMWARE_EXPANSION_SIZE, 0,
             FIRMWARE_EXPANSION_SIZE);
      memset(mem + FIRMWARE_EXPANSION_BEGIN, 0, FIRMWARE_EXPANSION_SIZE);
      g_active_memory->expansion_rom_type = eExpRomNull;
    }
    // NB. g_active_memory->io_select won't get set, so ROM won't be switched
    // back in...
  }

  uint8_t IO_STROBE = 0;

  if (IS_APPLE2() || sw_slotcxrom(g_active_memory)) {
    if ((address >= 0xC100) && (address <= 0xC7FF)) {
      const uint32_t slot = (address >> 8) & 0xF;
      if (slot < NUM_SLOTS) {
        if ((slot != 3) && g_active_memory->expansion_rom[slot]) {
          g_active_memory->io_select |= 1 << slot;
        } else if ((sw_slotc3rom(g_active_memory)) &&
                   g_active_memory->expansion_rom[slot]) {
          g_active_memory->io_select |= 1 << slot;  // Slot3 & Peripheral ROM
        } else if (!sw_slotc3rom(g_active_memory)) {
          g_active_memory->io_select_internal_rom = 1;  // Slot3 & Internal ROM
        }
      }
    } else if ((address >= 0xC800) && (address <= 0xCFFF)) {
      IO_STROBE = 1;
    }

    if (g_active_memory->io_select && IO_STROBE) {
      // Enable Peripheral Expansion ROM
      uint32_t slot = 1;
      for (; slot < NUM_SLOTS; slot++) {
        if (g_active_memory->io_select & (1 << slot)) {
          break;
        }
      }

      if ((slot < NUM_SLOTS) && g_active_memory->expansion_rom[slot] &&
          (g_active_memory->peripheral_rom_slot != slot)) {
        if (g_active_memory->cx_rom_peripheral != nullptr) {
          memcpy(g_active_memory->cx_rom_peripheral + FIRMWARE_EXPANSION_SIZE,
                 g_active_memory->expansion_rom[slot], FIRMWARE_EXPANSION_SIZE);
        }
        if (mem != nullptr) {
          memcpy(mem + FIRMWARE_EXPANSION_BEGIN,
                 g_active_memory->expansion_rom[slot], FIRMWARE_EXPANSION_SIZE);
        }
        g_active_memory->expansion_rom_type = eExpRomPeripheral;
        g_active_memory->peripheral_rom_slot = slot;
      }
    } else if (g_active_memory->io_select_internal_rom && IO_STROBE &&
               (g_active_memory->expansion_rom_type != eExpRomInternal)) {
      // Enable Internal ROM
      // . Get this for PR#3
      if (g_active_memory->cx_rom_internal != nullptr && mem != nullptr) {
        memcpy(mem + FIRMWARE_EXPANSION_BEGIN,
               g_active_memory->cx_rom_internal + FIRMWARE_EXPANSION_SIZE,
               FIRMWARE_EXPANSION_SIZE);
      }
      g_active_memory->expansion_rom_type = eExpRomInternal;
      g_active_memory->peripheral_rom_slot = 0;
    }
  }

  if (!IS_APPLE2() && !sw_slotcxrom(g_active_memory)) {
    // !sw_slotc3rom(g_active_memory) = Internal ROM: $C300-C3FF
    // !sw_slotcxrom(g_active_memory) = Internal ROM: $C100-CFFF

    if ((address >= 0xC100) &&
        (address <=
         0xC7FF)) {  // Don't care about state of sw_slotc3rom(g_active_memory)
      g_active_memory->io_select_internal_rom = 1;
    } else if ((address >= 0xC800) && (address <= 0xCFFF)) {
      IO_STROBE = 1;
    }

    if (!sw_slotcxrom(g_active_memory) &&
        g_active_memory->io_select_internal_rom && IO_STROBE &&
        (g_active_memory->expansion_rom_type != eExpRomInternal)) {
      // Enable Internal ROM
      if (g_active_memory->cx_rom_internal != nullptr && mem != nullptr) {
        memcpy(mem + FIRMWARE_EXPANSION_BEGIN,
               g_active_memory->cx_rom_internal + FIRMWARE_EXPANSION_SIZE,
               FIRMWARE_EXPANSION_SIZE);
      }
      g_active_memory->expansion_rom_type = eExpRomInternal;
      g_active_memory->peripheral_rom_slot = 0;
    }
  }

  if ((g_active_memory->expansion_rom_type == eExpRomNull) &&
      (address >= 0xC800)) {
    return io_null(programcounter, address, write, value, cycles_left);
  } else {
    return mem ? mem[address] : mem_read_floating_bus(cycles_left);
  }
}

auto io_write_cxxx(uint16_t programcounter, uint16_t address, uint8_t write,
                   uint8_t value, uint32_t cycles_left) -> uint8_t {
  (void)value;
  (void)cycles_left;
  (void)programcounter;
  (void)address;
  (void)write;
  return 0;
}

static uint8_t g_bm_slot_init = 0;

static auto init_io_handlers() -> void {
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
    IORead[NUM_PAGES_64K + i] = io_read_cxxx;
    IOWrite[NUM_PAGES_64K + i] = io_write_cxxx;
  }

  g_active_memory->io_select = 0;
  g_active_memory->io_select_internal_rom = 0;
  g_active_memory->expansion_rom_type = eExpRomNull;
  g_active_memory->peripheral_rom_slot = 0;

  for (i = 0; i < NUM_SLOTS; i++) {
    g_active_memory->expansion_rom[i] = nullptr;
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
  g_active_memory->slot_parameters[slot] = slot_parameter;

  uint16_t index = static_cast<uint16_t>(0x80 + (slot << 4));
  for (uint32_t i = 0; i < 16; i++) {
    IORead[index + i] = IOReadC0;
    IOWrite[index + i] = IOWriteC0;
  }

  if (slot == 0) {
    return;
  }

  if (IOReadCx == nullptr) {
    IOReadCx = io_read_cxxx;
  }
  if (IOWriteCx == nullptr) {
    IOWriteCx = io_write_cxxx;
  }

  IORead[NUM_PAGES_64K + slot] = IOReadCx;
  IOWrite[NUM_PAGES_64K + slot] = IOWriteCx;

  g_active_memory->expansion_rom[slot] = expansion_rom;
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

auto get_mem_mode() -> uint32_t { return g_active_memory->mem_mode; }

auto set_mem_mode(uint32_t new_mem_mode) -> void {
  g_active_memory->mem_mode = new_mem_mode;
}

static auto reset_paging(bool initialize) -> void {
  g_active_memory->last_write_ram = false;
  g_active_memory->mem_mode = MF_HRAM_BANK2 | MF_SLOTCXROM | MF_HRAM_WRITE;
  mem_update_paging(initialize, false);
}

auto mem_update_paging(bool initialize, bool updatewriteonly) -> void {
  uint8_t* oldshadow[PAGE_MAX]{};
  if (!(initialize || updatewriteonly)) {
    memcpy(oldshadow, g_active_memory->memshadow, PAGE_MAX * sizeof(uint8_t*));
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
      g_active_memory->memshadow[loop] =
          sw_altzp(g_active_memory) ? g_active_memory->memaux + (loop << 8)
                                    : g_active_memory->memmain + (loop << 8);
    }
  }

  for (loop = PAGE_TWO; loop < PAGE_C0; loop++) {
    g_active_memory->memshadow[loop] =
        sw_auxread(g_active_memory) ? g_active_memory->memaux + (loop << 8)
                                    : g_active_memory->memmain + (loop << 8);
    memwrite[loop] = ((sw_auxread(g_active_memory) != 0) ==
                      (sw_auxwrite(g_active_memory) != 0))
                         ? mem + (loop << 8)
                     : sw_auxwrite(g_active_memory)
                         ? g_active_memory->memaux + (loop << 8)
                         : g_active_memory->memmain + (loop << 8);
  }

  if (!updatewriteonly) {
    for (loop = PAGE_C0; loop < PAGE_C8; loop++) {
      const uint32_t slot_offset = (loop & 0x0f) * PAGE_SIZE;
      uint8_t* base = nullptr;
      if (loop == PAGE_C3) {
        base = (sw_slotc3rom(g_active_memory) && sw_slotcxrom(g_active_memory))
                   ? g_active_memory->cx_rom_peripheral
                   : g_active_memory->cx_rom_internal;
      } else {
        base = sw_slotcxrom(g_active_memory)
                   ? g_active_memory->cx_rom_peripheral
                   : g_active_memory->cx_rom_internal;
      }
      g_active_memory->memshadow[loop] =
          base ? (base + slot_offset) : (mem + (loop << 8));
    }

    for (loop = PAGE_C8; loop < PAGE_D0; loop++) {
      const uint32_t rom_offset = (loop & 0x0f) * PAGE_SIZE;
      g_active_memory->memshadow[loop] =
          g_active_memory->cx_rom_internal
              ? (g_active_memory->cx_rom_internal + rom_offset)
              : (mem + (loop << 8));
    }
  }

  for (loop = PAGE_D0; loop < PAGE_E0; loop++) {
    int bankoffset = (sw_hram_bank2(g_active_memory) ? 0 : LC_BANK_SIZE);
    g_active_memory->memshadow[loop] =
        sw_highram(g_active_memory)
            ? sw_altzp(g_active_memory)
                  ? (g_active_memory->memaux
                         ? g_active_memory->memaux + (loop << 8) - bankoffset
                         : mem + (loop << 8))
                  : (g_active_memory->memmain
                         ? g_active_memory->memmain + (loop << 8) - bankoffset
                         : mem + (loop << 8))
            : (g_active_memory->memrom
                   ? g_active_memory->memrom +
                         (static_cast<size_t>((loop - PAGE_D0) * PAGE_SIZE))
                   : (mem + (loop << 8)));

    memwrite[loop] =
        sw_hram_write(g_active_memory)
            ? sw_highram(g_active_memory) ? mem + (loop << 8)
              : sw_altzp(g_active_memory)
                  ? (g_active_memory->memaux
                         ? g_active_memory->memaux + (loop << 8) - bankoffset
                         : mem + (loop << 8))
                  : (g_active_memory->memmain
                         ? g_active_memory->memmain + (loop << 8) - bankoffset
                         : mem + (loop << 8))
            : nullptr;
  }

  for (loop = PAGE_E0; loop < PAGE_MAX; loop++) {
    g_active_memory->memshadow[loop] =
        sw_highram(g_active_memory)
            ? sw_altzp(g_active_memory)
                  ? (g_active_memory->memaux
                         ? g_active_memory->memaux + (loop << 8)
                         : mem + (loop << 8))
                  : (g_active_memory->memmain
                         ? g_active_memory->memmain + (loop << 8)
                         : mem + (loop << 8))
            : (g_active_memory->memrom
                   ? g_active_memory->memrom +
                         (static_cast<size_t>((loop - PAGE_D0) * PAGE_SIZE))
                   : (mem + (loop << 8)));

    memwrite[loop] = sw_hram_write(g_active_memory)
                         ? sw_highram(g_active_memory) ? mem + (loop << 8)
                           : sw_altzp(g_active_memory)
                               ? (g_active_memory->memaux
                                      ? g_active_memory->memaux + (loop << 8)
                                      : mem + (loop << 8))
                               : (g_active_memory->memmain
                                      ? g_active_memory->memmain + (loop << 8)
                                      : mem + (loop << 8))
                         : nullptr;
  }

  if (sw_80store(g_active_memory)) {
    for (loop = PAGE_TXT1_START; loop < PAGE_TXT1_END; loop++) {
      g_active_memory->memshadow[loop] =
          sw_page2(g_active_memory) ? g_active_memory->memaux + (loop << 8)
                                    : g_active_memory->memmain + (loop << 8);
      memwrite[loop] = mem + (loop << 8);
    }

    if (sw_hires(g_active_memory)) {
      for (loop = PAGE_HGR1_START; loop < PAGE_HGR1_END; loop++) {
        g_active_memory->memshadow[loop] =
            sw_page2(g_active_memory) ? g_active_memory->memaux + (loop << 8)
                                      : g_active_memory->memmain + (loop << 8);
        memwrite[loop] = mem + (loop << 8);
      }
    }
  }

  // Move memory back and forth as necessary between the shadow areas and
  // the main ram image to keep both sets of memory consistent with the new
  // paging shadow table
  if (!updatewriteonly) {
    for (loop = PAGE_ZERO; loop < PAGE_MAX; loop++) {
      if (initialize || (oldshadow[loop] != g_active_memory->memshadow[loop])) {
        if ((!(initialize)) &&
            ((*(memdirty + loop) & 1) || (loop <= PAGE_ONE))) {
          *(memdirty + loop) &= ~1;
          memcpy(oldshadow[loop], mem + (loop << 8), PAGE_SIZE);
        }
        memcpy(mem + (loop << 8), g_active_memory->memshadow[loop], PAGE_SIZE);
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
      result = sw_hram_bank2(g_active_memory);
      break;
    case SS_RDRAMRD:
      result = sw_highram(g_active_memory);
      break;
    case SS_RDRAMWRT:
      result = sw_auxread(g_active_memory);
      break;
    case SS_RDCXROM:
      result = sw_auxwrite(g_active_memory);
      break;
    case SS_RDALTZP:
      result = !sw_slotcxrom(g_active_memory);
      break;
    case SS_RD80STORE:
      result = sw_altzp(g_active_memory);
      break;
    case SS_RDSLOTC3ROM:
      result = sw_slotc3rom(g_active_memory);
      break;
    case SS_RD80COL:
      result = sw_80store(g_active_memory);
      break;
    case SS_RDPAGE2:
      result = sw_page2(g_active_memory);
      break;
    case SS_RDHIRES:
      result = sw_hires(g_active_memory);
      break;
    default:
      break;
  }
  return (mem_read_floating_bus(cycles_left) & 0x7F) | (result ? 0x80 : 0x00);
}

auto mem_destroy() -> void {
#ifdef RAMWORKS
  for (uint32_t i = 0; i < MAX_RAMWORKS_PAGES; i++) {
    if (g_active_memory->rw_pages[i]) {
      free(g_active_memory->rw_pages[i]);
      g_active_memory->rw_pages[i] = nullptr;
    }
  }
#endif

  if (g_active_memory->memimage) munlock(g_active_memory->memimage, MEMORY_64K);

  free(g_active_memory->memaux_allocated);
  free(g_active_memory->memmain);
  free(memdirty);
  free(g_active_memory->memrom);
  free(g_active_memory->memimage);
  free(g_active_memory->cx_rom_internal);
  free(g_active_memory->cx_rom_peripheral);

  g_active_memory->memaux = nullptr;
  g_active_memory->memaux_allocated = nullptr;
  g_active_memory->memmain = nullptr;
  set_mem_dirty(nullptr);
  g_active_memory->memrom = nullptr;
  g_active_memory->memimage = nullptr;

  g_active_memory->cx_rom_internal = nullptr;
  g_active_memory->cx_rom_peripheral = nullptr;

  set_mem(nullptr);

  memset(memwrite, 0, NUM_PAGES_64K * sizeof(uint8_t*));
  memset(g_active_memory->memshadow, 0, NUM_PAGES_64K * sizeof(uint8_t*));
}

auto mem_get_80store() -> bool { return sw_80store(g_active_memory) != 0; }

auto mem_check_slotcxrom() -> bool {
  return sw_slotcxrom(g_active_memory) != 0;
}

auto mem_get_aux_ptr(uint16_t offset) -> uint8_t* {
  uint8_t* result = (g_active_memory->memshadow[(offset >> 8)] ==
                     (g_active_memory->memaux + (offset & PAGE_MASK)))
                        ? mem + offset
                        : g_active_memory->memaux + offset;

#ifdef RAMWORKS
  if (((sw_page2(g_active_memory) && sw_80store(g_active_memory)) ||
       video_get_sw_80col()) &&
      ((((offset & PAGE_MASK) >= TXT1_BEGIN) &&
        ((offset & PAGE_MASK) <= TXT1_END_PAGE)) ||
       (sw_hires(g_active_memory) && ((offset & PAGE_MASK) >= HGR1_BEGIN) &&
        ((offset & PAGE_MASK) <= HGR1_END_PAGE)))) {
    if (g_active_memory->rw_pages[0] != nullptr) {
      result = (g_active_memory->memshadow[(offset >> 8)] ==
                (g_active_memory->rw_pages[0] + (offset & PAGE_MASK)))
                   ? mem + offset
                   : g_active_memory->rw_pages[0] + offset;
    }
  }
#endif

  return result;
}

auto mem_get_main_ptr(uint16_t offset) -> uint8_t* {
  return (g_active_memory->memshadow[(offset >> 8)] ==
          (g_active_memory->memmain + (offset & 0xFF00)))
             ? mem + offset
             : g_active_memory->memmain + offset;
}

//===========================================================================

auto mem_get_bank_ptr(const uint32_t bank) -> uint8_t* {
#ifdef RAMWORKS
  if (bank > g_max_ex_pages || bank >= MAX_RAMWORKS_PAGES) {
    return nullptr;
  }

  if (bank == 0) {
    return g_active_memory->memmain;
  }

  return g_active_memory->rw_pages[bank - 1];
#else
  return (bank == 0)   ? g_active_memory->memmain
         : (bank == 1) ? g_active_memory->memaux
                       : nullptr;
#endif
}

auto mem_get_cx_rom_peripheral() -> uint8_t* {
  return g_active_memory->cx_rom_peripheral;
}

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
      sw_slotcxrom(
          g_active_memory)) {  // [$C100..C7FF] //e or Enhanced //e internal ROM
    return true;
  }

  if (!IS_APPLE2() && !sw_slotc3rom(g_active_memory) &&
      (addr >> 8) == 0xC3) {  // [$C300..C3FF] //e or Enhanced //e internal ROM
    return true;
  }

  if (addr <= APPLE_SLOT_END)  // [$C100..C7FF]
  {
    const uint32_t slot = (addr >> 8) & 0x7;
    return (g_bm_slot_init & (1 << slot)) != 0;  // card present in this slot?
  }

  // [$C800..CFFF]
  if (g_active_memory->expansion_rom_type == eExpRomNull) {
    if (g_active_memory->io_select || g_active_memory->io_select_internal_rom) {
      return true;
    }
    return false;
  }

  return true;
}

auto mem_pre_initialize() -> void { init_io_handlers(); }

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

  g_active_memory->memaux_allocated = static_cast<uint8_t*>(malloc(MEMORY_64K));
  g_active_memory->memaux = g_active_memory->memaux_allocated;
  g_active_memory->memmain = static_cast<uint8_t*>(malloc(MEMORY_64K));
  set_mem_dirty(static_cast<uint8_t*>(malloc(NUM_PAGES_64K)));
  g_active_memory->memrom = static_cast<uint8_t*>(malloc(ROM_BUFFER_SIZE));
  g_active_memory->memimage = static_cast<uint8_t*>(malloc(MEMORY_64K));
  g_active_memory->cx_rom_internal = static_cast<uint8_t*>(malloc(CxRomSize));
  g_active_memory->cx_rom_peripheral = static_cast<uint8_t*>(malloc(CxRomSize));

  if (!g_active_memory->memaux || !memdirty || !g_active_memory->memimage ||
      !g_active_memory->memmain || !g_active_memory->memrom ||
      !g_active_memory->cx_rom_internal ||
      !g_active_memory->cx_rom_peripheral) {
    Logger::error("Unable to allocate required memory buffers.");
    mem_destroy();
    return -1;
  }

  if (g_active_memory->memaux) memset(g_active_memory->memaux, 0, MEMORY_64K);
  if (g_active_memory->memmain) memset(g_active_memory->memmain, 0, MEMORY_64K);
  set_mem(g_active_memory->memmain);
  if (memdirty) memset(memdirty, 0, NUM_PAGES_64K);
  if (g_active_memory->memrom)
    memset(g_active_memory->memrom, 0, ROM_BUFFER_SIZE);
  if (g_active_memory->memimage)
    memset(g_active_memory->memimage, 0, MEMORY_64K);

  if (mlock(g_active_memory->memimage, MEMORY_64K) != 0) {
    Logger::warning("Failed to lock memory image from swapping.");
  }

  if (g_active_memory->cx_rom_internal)
    memset(g_active_memory->cx_rom_internal, 0, CxRomSize);
  if (g_active_memory->cx_rom_peripheral)
    memset(g_active_memory->cx_rom_peripheral, 0, CxRomSize);

#ifdef RAMWORKS
  g_active_memory->rw_pages[0] = static_cast<uint8_t*>(malloc(MEMORY_64K));
  if (g_active_memory->rw_pages[0]) {
    memset(g_active_memory->rw_pages[0], 0, MEMORY_64K);
    g_active_memory->memaux = g_active_memory->rw_pages[0];
  }
  uint32_t i = 1;
  while (i < g_max_ex_pages && i < MAX_RAMWORKS_PAGES) {
    g_active_memory->rw_pages[i] = static_cast<uint8_t*>(malloc(MEMORY_64K));
    if (g_active_memory->rw_pages[i]) {
      memset(g_active_memory->rw_pages[i], 0, MEMORY_64K);
      i++;
    } else {
      break;
    }
  }
#endif
  mem_set_active_context(g_active_memory);

  uint32_t ROM_SIZE = 0;
  const uint8_t* rom_data = nullptr;
  switch (g_apple2_type) {
#if ENABLE_ROM_APPLE2
    case A2TYPE_APPLE2:
      rom_data = g_rom_apple2;
      ROM_SIZE = Apple2RomSize;
      break;
#endif
#if ENABLE_ROM_APPLE2PLUS
    case A2TYPE_APPLE2PLUS:
      rom_data = g_rom_apple2_plus;
      ROM_SIZE = Apple2RomSize;
      break;
#endif
#if ENABLE_ROM_APPLE2_JPLUS
    case A2TYPE_APPLE2JPLUS:
      rom_data = g_rom_apple2_jplus;
      ROM_SIZE = Apple2RomSize;
      break;
#endif
#if ENABLE_ROM_APPLE2E
    case A2TYPE_APPLE2E:
      rom_data = g_rom_apple2e;
      ROM_SIZE = Apple2eRomSize;
      break;
#endif
#if ENABLE_ROM_APPLE2ENHANCED
    case A2TYPE_APPLE2EENHANCED:
      rom_data = g_rom_apple2e_enhanced;
      ROM_SIZE = Apple2eRomSize;
      break;
#endif
    default:
      break;
  }

  if (rom_data == nullptr) {
    fprintf(stderr, "Unable to find rom for specified computer type! Sorry\n");
    return -1;
  }

  const uint8_t* data = rom_data;

  memset(g_active_memory->cx_rom_internal, 0, CxRomSize);
  memset(g_active_memory->cx_rom_peripheral, 0, CxRomSize);

  if (ROM_SIZE == Apple2eRomSize) {
    memcpy(g_active_memory->cx_rom_internal, data, CxRomSize);
    data += CxRomSize;
    ROM_SIZE -= CxRomSize;
  }

  assert(ROM_SIZE == Apple2RomSize);
  memcpy(g_active_memory->memrom, data, Apple2RomSize);  // ROM at $D000...$FFFF

  const uint32_t slot = 0;
  register_io_handler(slot, mem_set_paging, mem_set_paging, nullptr, nullptr,
                      nullptr, nullptr);

  mem_reset();
  return 0;
}

auto mem_reset() -> void {
  memset(g_active_memory->memshadow, 0, NUM_PAGES_64K * sizeof(uint8_t*));
  memset(memwrite, 0, NUM_PAGES_64K * sizeof(uint8_t*));

  if (g_active_memory->memaux) memset(g_active_memory->memaux, 0, MEMORY_64K);
  if (g_active_memory->memmain) memset(g_active_memory->memmain, 0, MEMORY_64K);

  int byte = 0;

  if (g_memory_init_pattern == MIP_FF_FF_00_00) {
    for (byte = 0x0000; byte < IO_RANGE_BEGIN;) {
      g_active_memory->memmain[byte++] = 0xFF;
      g_active_memory->memmain[byte++] = 0xFF;
      byte++;
      byte++;
    }
  }

  set_mem(g_active_memory->memimage);
  reset_paging(true);

  // Initialize & reset the cpu
  // . Do this after ROM has been copied back to mem[], so that PC is correctly
  // init'ed from 6502's reset vector
  cpu_initialize();
}

// Call by:
// . Soft-reset (Ctrl+Reset)
// . Snapshot_LoadState()
auto mem_reset_paging() -> void { reset_paging(false); }

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
  uint32_t lastmemmode = g_active_memory->mem_mode;

  // Determine the new memory paging mode.
  if ((address >= SS_LC_BEGIN) && (address <= SS_LC_END)) {
    bool writeram = (address & 1);
    g_active_memory->mem_mode &= ~(MF_HRAM_BANK2 | MF_HIGHRAM | MF_HRAM_WRITE);
    {
      g_active_memory->last_write_ram =
          true;  // note: because diags.do doesn't set switches twice!
      if (g_active_memory->last_write_ram && writeram) {
        g_active_memory->mem_mode |= MF_HRAM_WRITE;
      }
      if (!(address & 8)) {
        g_active_memory->mem_mode |= MF_HRAM_BANK2;
      }
      if (((address & 2) >> 1) == (address & 1)) {
        g_active_memory->mem_mode |= MF_HIGHRAM;
      }
    }
    g_active_memory->last_write_ram = writeram;
  } else if (!IS_APPLE2()) {
    switch (address) {
      case SS_80STORE_OFF:
        g_active_memory->mem_mode &= ~MF_80STORE;
        break;
      case SS_80STORE_ON:
        g_active_memory->mem_mode |= MF_80STORE;
        break;
      case SS_AUXREAD_OFF:
        g_active_memory->mem_mode &= ~MF_AUXREAD;
        break;
      case SS_AUXREAD_ON:
        g_active_memory->mem_mode |= MF_AUXREAD;
        break;
      case SS_AUXWRITE_OFF:
        g_active_memory->mem_mode &= ~MF_AUXWRITE;
        break;
      case SS_AUXWRITE_ON:
        g_active_memory->mem_mode |= MF_AUXWRITE;
        break;
      case SS_SLOTCXROM_ON:
        g_active_memory->mem_mode |= MF_SLOTCXROM;
        break;
      case SS_SLOTCXROM_OFF:
        g_active_memory->mem_mode &= ~MF_SLOTCXROM;
        break;
      case SS_ALTZP_OFF:
        g_active_memory->mem_mode &= ~MF_ALTZP;
        break;
      case SS_ALTZP_ON:
        g_active_memory->mem_mode |= MF_ALTZP;
        break;
      case SS_SLOTC3ROM_OFF:
        g_active_memory->mem_mode &= ~MF_SLOTC3ROM;
        break;
      case SS_SLOTC3ROM_ON:
        g_active_memory->mem_mode |= MF_SLOTC3ROM;
        break;
      case SS_PAGE2_OFF:
        g_active_memory->mem_mode &= ~MF_PAGE2;
        break;
      case SS_PAGE2_ON:
        g_active_memory->mem_mode |= MF_PAGE2;
        break;
      case SS_HIRES_OFF:
        g_active_memory->mem_mode &= ~MF_HIRES;
        break;
      case SS_HIRES_ON:
        g_active_memory->mem_mode |= MF_HIRES;
        break;
#ifdef RAMWORKS
      case SS_RW_AUX_PAGE:
      case SS_RW_III_PAGE:
        if ((value < g_max_ex_pages) && (value < MAX_RAMWORKS_PAGES) &&
            g_active_memory->rw_pages[value]) {
          g_active_memory->active_bank = value;
          g_active_memory->memaux = g_active_memory->rw_pages[value];
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
      ((read_u32_le(mem + programcounter) & 0x00FFFEFF) == 0x00C0028D)) {
    g_active_memory->mode_changing = true;
    return write ? 0 : mem_read_floating_bus(1, cycles_left);
  }
  if ((address >= 0x80) && (address <= 0x8F) && (programcounter <= 0xFFFC) &&
      (((read_u32_le(mem + programcounter) & 0x00FFFEFF) == 0x00C0048D) ||
       ((read_u32_le(mem + programcounter) & 0x00FFFEFF) == 0x00C0028D))) {
    g_active_memory->mode_changing = true;
    return write ? 0 : mem_read_floating_bus(1, cycles_left);
  }

  // If the memory paging mode has changed, update our memory images and write
  // tables.
  if ((lastmemmode != g_active_memory->mem_mode) ||
      g_active_memory->mode_changing) {
    g_active_memory->mode_changing = false;

    if ((lastmemmode & MF_SLOTCXROM) !=
        (g_active_memory->mem_mode & MF_SLOTCXROM)) {
      if (sw_slotcxrom(g_active_memory)) {
        // Disable Internal ROM
        // . Similar to $CFFF access
        // . None of the peripheral cards can be driving the bus - so use the
        // null ROM
        memset(g_active_memory->cx_rom_peripheral + FIRMWARE_EXPANSION_SIZE, 0,
               FIRMWARE_EXPANSION_SIZE);
        memset(mem + FIRMWARE_EXPANSION_BEGIN, 0, FIRMWARE_EXPANSION_SIZE);
        g_active_memory->expansion_rom_type = eExpRomNull;
        g_active_memory->peripheral_rom_slot = 0;
      } else {
        // Enable Internal ROM
        if (g_active_memory->cx_rom_internal != nullptr) {
          memcpy(mem + FIRMWARE_EXPANSION_BEGIN,
                 g_active_memory->cx_rom_internal + FIRMWARE_EXPANSION_SIZE,
                 FIRMWARE_EXPANSION_SIZE);
        }
        g_active_memory->expansion_rom_type = eExpRomInternal;
        g_active_memory->peripheral_rom_slot = 0;
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
  return g_active_memory->slot_parameters[slot];
}

auto mem_get_snapshot(SS_BaseMemory* ss) -> uint32_t {
  ss->mem_mode = g_active_memory->mem_mode;
  ss->last_write_ram = g_active_memory->last_write_ram ? 1 : 0;

  for (uint32_t offset = 0x0000; offset < MEMORY_64K; offset += PAGE_SIZE) {
    memcpy(ss->mem_main + offset,
           mem_get_main_ptr(static_cast<uint16_t>(offset)), PAGE_SIZE);
    memcpy(ss->mem_aux + offset, mem_get_aux_ptr(static_cast<uint16_t>(offset)),
           PAGE_SIZE);
  }

  return 0;
}

auto mem_set_snapshot(SS_BaseMemory* ss) -> uint32_t {
  g_active_memory->mem_mode = ss->mem_mode;
  g_active_memory->last_write_ram = (ss->last_write_ram != 0);
  memcpy(g_active_memory->memmain, ss->mem_main, mem_main_size);
  memcpy(g_active_memory->memaux, ss->mem_aux, mem_aux_size);
  g_active_memory->mode_changing = false;
  mem_update_paging(true, false);  // Initialize=1, UpdateWriteOnly=0

  return 0;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-no-malloc,
// cppcoreguidelines-owning-memory, cppcoreguidelines-pro-type-reinterpret-cast,
// bugprone-easily-swappable-parameters, bugprone-branch-clone,
// cppcoreguidelines-macro-usage, modernize-use-auto,
// cppcoreguidelines-init-variables,
// cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
