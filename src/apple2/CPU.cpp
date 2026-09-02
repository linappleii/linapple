// SPDX-License-Identifier: GPL-2.0-only

#include <pthread.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "apple2/Apple2Types.h"
#define CPU_CPP_IMPL
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "core/LinAppleCore.h"

// Unavoidable hardware architectural constraints for low-level 6502 CPU core
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// bugprone-easily-swappable-parameters, google-readability-function-size)

enum {
  AF_SIGN = 0x80,
  AF_OVERFLOW = 0x40,
  AF_RESERVED = 0x20,
  AF_BREAK = 0x10,
  AF_DECIMAL = 0x08,
  AF_INTERRUPT = 0x04,
  AF_ZERO = 0x02,
  AF_CARRY = 0x01
};

enum { SHORTOPCODES = 22, BENCHOPCODES = 33 };

static uint8_t benchopcode[BENCHOPCODES] = {
    0x06, 0x16, 0x24, 0x45, 0x48, 0x65, 0x68, 0x76, 0x84, 0x85, 0x86,
    0x91, 0x94, 0xA4, 0xA5, 0xA6, 0xB1, 0xB4, 0xC0, 0xC4, 0xC5, 0xE6,
    0x19, 0x6D, 0x8D, 0x99, 0x9D, 0xAD, 0xB9, 0xBD, 0xDD, 0xED, 0xEE};

static CpuInstance_t g_cpu_context{};
CpuInstance_t* g_active_cpu = &g_cpu_context;

RegsRec_t regs;
uint64_t g_cumulative_cycles = 0;
static uint32_t g_cycles_submitted;
static uint32_t g_cycles_executed;
static std::atomic<uint32_t> g_bm_irq{0};
static std::atomic<uint32_t> g_bm_nmi{0};
static std::atomic<bool> g_nmi_flank{
    false};  // Positive going flank on NMI line

auto cpu_get_registers() -> CpuRegisters_t* { return &regs; }
auto cpu_get_cumulative_cycles() -> uint64_t { return g_cumulative_cycles; }
auto cpu_get_active_context() -> CpuInstance_t* { return g_active_cpu; }
auto cpu_set_active_context(CpuInstance_t* context) -> void {
  if (context == nullptr) {
    return;
  }
  g_active_cpu->cpu_regs = regs;
  g_active_cpu->cumulative_cycles = g_cumulative_cycles;

  g_active_cpu = context;

  regs = g_active_cpu->cpu_regs;
  g_cumulative_cycles = g_active_cpu->cumulative_cycles;
}

static uint32_t g_internal_executed_cycles;

// Interrupt sources assert until the device is commanded to stop
static std::atomic<bool> g_crit_section_valid{false};
pthread_mutex_t g_critical_section = PTHREAD_MUTEX_INITIALIZER;

extern auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write,
                            uint8_t d, uint32_t cycles) -> uint8_t;

static inline auto read_u16_unaligned(const uint8_t* ptr) -> uint16_t {
  uint16_t val = 0;
  std::memcpy(&val, ptr, sizeof(val));
  return val;
}

uint64_t g_cycle_irq_start;
uint64_t g_cycle_irq_end;
uint16_t g_cycle_irq_time;

uint16_t g_idx = 0;
const uint16_t BUFFER_SIZE = 4096;  // 80 secs
uint16_t g_buffer[BUFFER_SIZE] = {};
uint32_t g_mean = 0;
uint32_t g_min = UINT32_MAX_VAL;
uint32_t g_max = 0;

static inline void do_irq_profiling(uint32_t cycles) { (void)cycles; }

static inline void fetch_opcode(uint8_t& opcode, uint32_t executed_cycles) {
  const uint16_t PC = regs.pc;
  g_internal_executed_cycles = executed_cycles;

  opcode = ((PC & IO_REGION_MASK) == IO_REGION_START)
               ? io_map_dispatch(PC, PC, 0, 0, executed_cycles)
               : mem[PC];

  regs.pc++;
}

template <bool is_cmos>
static auto cpu_execute_loop(uint32_t total_cycles) -> uint32_t {
  uint16_t addr = 0;
  uint8_t flagc = (regs.ps & AF_CARRY);
  uint8_t flagn = (regs.ps & AF_SIGN);
  uint8_t flagv = (regs.ps & AF_OVERFLOW);
  uint8_t flagz = (regs.ps & AF_ZERO);
  uint32_t executed_cycles = 0;
  uint16_t base = 0;

  auto set_nz = [&](uint16_t a) {
    flagn = (a & 0x80);
    flagz = !((a) & 0xFF);
  };
  auto set_z = [&](uint16_t a) { flagz = !((a) & 0xFF); };
  auto pack_ps = [&]() {
    regs.ps = (regs.ps & ~(AF_CARRY | AF_SIGN | AF_OVERFLOW | AF_ZERO)) |
              flagc | flagn | (flagv ? AF_OVERFLOW : 0) |
              (flagz ? AF_ZERO : 0) | AF_RESERVED | AF_BREAK;
  };
  auto unpack_ps = [&]() {
    flagc = (regs.ps & AF_CARRY);
    flagn = (regs.ps & AF_SIGN);
    flagv = (regs.ps & AF_OVERFLOW);
    flagz = (regs.ps & AF_ZERO);
  };
  auto push = [&](uint8_t a) {
    *(mem + regs.sp--) = a;
    if (regs.sp < STACK_BEGIN) regs.sp = STACK_END;
  };
  auto pop = [&]() -> uint8_t {
    return *(mem +
             ((regs.sp >= STACK_END) ? (regs.sp = STACK_BEGIN) : ++regs.sp));
  };
  auto read_byte = [&](uint16_t a) -> uint8_t {
    if ((a & IO_REGION_MASK) == IO_REGION_START) {
      return io_map_dispatch(regs.pc, a, 0, 0, executed_cycles);
    }
    return *(mem + a);
  };
  auto write_byte = [&](uint16_t a, uint8_t val) {
    memdirty[a >> 8] = 0xFF;
    uint8_t* page = memwrite[a >> 8];
    if (page) {
      *(page + (a & 0xFF)) = val;
    } else if ((a & IO_REGION_MASK) == IO_REGION_START) {
      io_map_dispatch(regs.pc, a, 1, val, executed_cycles);
    }
  };
  auto check_page_change = [&](uint16_t b, uint16_t a, uint16_t& extra) {
    if ((b ^ a) & 0xFF00) extra = 1;
  };
  auto branch_taken = [&](uint16_t& extra) {
    uint16_t old_pc = regs.pc;
    regs.pc += addr;
    if ((old_pc ^ regs.pc) & 0xFF00) {
      extra = 2;
    } else {
      extra = 1;
    }
  };

  // Addressing modes
  auto addr_imm = [&]() { addr = regs.pc++; };
  auto addr_zpg = [&]() { addr = *(mem + regs.pc++); };
  auto addr_zpgx = [&]() { addr = (*(mem + regs.pc++) + regs.x) & 0xFF; };
  auto addr_zpgy = [&]() { addr = (*(mem + regs.pc++) + regs.y) & 0xFF; };
  auto addr_abs = [&]() {
    addr = read_u16_unaligned(mem + regs.pc);
    regs.pc += 2;
  };
  auto addr_absx = [&](uint16_t& extra) {
    base = read_u16_unaligned(mem + regs.pc);
    addr = base + static_cast<uint16_t>(regs.x);
    regs.pc += 2;
    check_page_change(base, addr, extra);
  };
  auto addr_absy = [&](uint16_t& extra) {
    base = read_u16_unaligned(mem + regs.pc);
    addr = base + static_cast<uint16_t>(regs.y);
    regs.pc += 2;
    check_page_change(base, addr, extra);
  };
  auto addr_iabs_nmos = [&]() {
    base = read_u16_unaligned(mem + regs.pc);
    if ((base & 0xFF) == 0xFF) {
      addr = *(mem + base) +
             (static_cast<uint16_t>(*(mem + (base & 0xFF00))) << 8);
    } else {
      addr = read_u16_unaligned(mem + base);
    }
    regs.pc += 2;
  };
  auto addr_iabs_cmos = [&](uint16_t& extra) {
    base = read_u16_unaligned(mem + regs.pc);
    addr = read_u16_unaligned(mem + base);
    if ((base & 0xFF) == 0xFF) extra = 1;
    regs.pc += 2;
  };
  auto addr_iabsx = [&]() {
    addr = read_u16_unaligned(mem + read_u16_unaligned(mem + regs.pc) +
                              static_cast<uint16_t>(regs.x));
    regs.pc += 2;
  };
  auto addr_indx = [&]() {
    base = (*(mem + regs.pc++) + regs.x) & 0xFF;
    if (base == 0xFF) {
      addr = *(mem + 0xFF) + (static_cast<uint16_t>(*mem) << 8);
    } else {
      addr = read_u16_unaligned(mem + base);
    }
  };
  auto addr_indy = [&](uint16_t& extra) {
    if (*(mem + regs.pc) == 0xFF) {
      base = *(mem + 0xFF) + (static_cast<uint16_t>(*mem) << 8);
    } else {
      base = read_u16_unaligned(mem + *(mem + regs.pc));
    }
    regs.pc++;
    addr = base + static_cast<uint16_t>(regs.y);
    check_page_change(base, addr, extra);
  };
  auto addr_izpg = [&]() {
    base = *(mem + regs.pc++);
    if (base == 0xFF) {
      addr = *(mem + 0xFF) + (static_cast<uint16_t>(*mem) << 8);
    } else {
      addr = read_u16_unaligned(mem + base);
    }
  };
  auto addr_rel = [&]() {
    addr = static_cast<uint16_t>(
        static_cast<int16_t>(static_cast<signed char>(*(mem + regs.pc++))));
  };

  // Standard Opcode Implementations
  auto op_lda = [&]() {
    regs.a = read_byte(addr);
    set_nz(regs.a);
  };
  auto op_ldx = [&]() {
    regs.x = read_byte(addr);
    set_nz(regs.x);
  };
  auto op_ldy = [&]() {
    regs.y = read_byte(addr);
    set_nz(regs.y);
  };
  auto op_sta = [&]() { write_byte(addr, regs.a); };
  auto op_stx = [&]() { write_byte(addr, regs.x); };
  auto op_sty = [&]() { write_byte(addr, regs.y); };
  auto op_stz = [&]() { write_byte(addr, 0); };
  auto op_tax = [&]() {
    regs.x = regs.a;
    set_nz(regs.x);
  };
  auto op_txa = [&]() {
    regs.a = regs.x;
    set_nz(regs.a);
  };
  auto op_tay = [&]() {
    regs.y = regs.a;
    set_nz(regs.y);
  };
  auto op_tya = [&]() {
    regs.a = regs.y;
    set_nz(regs.a);
  };
  auto op_tsx = [&]() {
    regs.x = regs.sp & 0xFF;
    set_nz(regs.x);
  };
  auto op_txs = [&]() { regs.sp = 0x100 | regs.x; };
  auto op_and = [&]() {
    regs.a &= read_byte(addr);
    set_nz(regs.a);
  };
  auto op_ora = [&]() {
    regs.a |= read_byte(addr);
    set_nz(regs.a);
  };
  auto op_eor = [&]() {
    regs.a ^= read_byte(addr);
    set_nz(regs.a);
  };
  auto op_bit = [&]() {
    uint16_t val = read_byte(addr);
    flagz = !(regs.a & val);
    flagn = val & 0x80;
    flagv = val & 0x40;
  };
  auto op_biti = [&]() { flagz = !(regs.a & read_byte(addr)); };
  auto op_cmp = [&]() {
    uint16_t val = read_byte(addr);
    flagc = (regs.a >= val);
    val = regs.a - val;
    set_nz(val);
  };
  auto op_cpx = [&]() {
    uint16_t val = read_byte(addr);
    flagc = (regs.x >= val);
    val = regs.x - val;
    set_nz(val);
  };
  auto op_cpy = [&]() {
    uint16_t val = read_byte(addr);
    flagc = (regs.y >= val);
    val = regs.y - val;
    set_nz(val);
  };
  auto op_asla = [&]() {
    uint16_t val = regs.a << 1;
    flagc = (val > 0xFF);
    set_nz(val);
    regs.a = static_cast<uint8_t>(val);
  };
  auto op_asl = [&]() {
    uint16_t val = read_byte(addr) << 1;
    flagc = (val > 0xFF);
    set_nz(val);
    write_byte(addr, static_cast<uint8_t>(val));
  };
  auto op_lsra = [&]() {
    flagc = (regs.a & 1);
    flagn = 0;
    regs.a >>= 1;
    set_z(regs.a);
  };
  auto op_lsr = [&]() {
    uint16_t val = read_byte(addr);
    flagc = (val & 1);
    flagn = 0;
    val >>= 1;
    set_z(val);
    write_byte(addr, static_cast<uint8_t>(val));
  };
  auto op_rola = [&]() {
    uint16_t val = (static_cast<uint16_t>(regs.a) << 1) | flagc;
    flagc = (val > 0xFF);
    regs.a = val & 0xFF;
    set_nz(regs.a);
  };
  auto op_rol = [&]() {
    uint16_t val = (read_byte(addr) << 1) | flagc;
    flagc = (val > 0xFF);
    set_nz(val);
    write_byte(addr, static_cast<uint8_t>(val));
  };
  auto op_rora = [&]() {
    uint16_t val = (static_cast<uint16_t>(regs.a) >> 1) | (flagc ? 0x80 : 0);
    flagc = (regs.a & 1);
    regs.a = val & 0xFF;
    set_nz(regs.a);
  };
  auto op_ror = [&]() {
    uint16_t temp = read_byte(addr);
    uint16_t val = (temp >> 1) | (flagc ? 0x80 : 0);
    flagc = (temp & 1);
    set_nz(val);
    write_byte(addr, static_cast<uint8_t>(val));
  };
  auto op_ina = [&]() {
    ++regs.a;
    set_nz(regs.a);
  };
  auto op_dea = [&]() {
    --regs.a;
    set_nz(regs.a);
  };
  auto op_inc = [&]() {
    uint16_t val = read_byte(addr) + 1;
    set_nz(val);
    write_byte(addr, static_cast<uint8_t>(val));
  };
  auto op_dec = [&]() {
    uint16_t val = read_byte(addr) - 1;
    set_nz(val);
    write_byte(addr, static_cast<uint8_t>(val));
  };
  auto op_inx = [&]() {
    ++regs.x;
    set_nz(regs.x);
  };
  auto op_dex = [&]() {
    --regs.x;
    set_nz(regs.x);
  };
  auto op_iny = [&]() {
    ++regs.y;
    set_nz(regs.y);
  };
  auto op_dey = [&]() {
    --regs.y;
    set_nz(regs.y);
  };
  auto op_jmp = [&]() { regs.pc = addr; };
  auto op_jsr = [&]() {
    --regs.pc;
    push(regs.pc >> 8);
    push(regs.pc & 0xFF);
    regs.pc = addr;
  };
  auto op_rts = [&]() {
    regs.pc = pop();
    regs.pc |= (static_cast<uint16_t>(pop()) << 8);
    ++regs.pc;
  };
  auto op_rti = [&]() {
    regs.ps = pop() | AF_RESERVED | AF_BREAK;
    unpack_ps();
    regs.pc = pop();
    regs.pc |= (static_cast<uint16_t>(pop()) << 8);
  };
  auto op_brk = [&]() {
    regs.pc++;
    push(regs.pc >> 8);
    push(regs.pc & 0xFF);
    pack_ps();
    push(regs.ps);
    regs.ps |= AF_INTERRUPT;
    regs.pc = read_u16_unaligned(mem + 0xFFFE);
  };
  auto op_hlt = [&]() {
    regs.is_jammed = 1;
    --regs.pc;
  };
  auto op_pha = [&]() { push(regs.a); };
  auto op_php = [&]() {
    pack_ps();
    push(regs.ps);
  };
  auto op_phx = [&]() { push(regs.x); };
  auto op_phy = [&]() { push(regs.y); };
  auto op_pla = [&]() {
    regs.a = pop();
    set_nz(regs.a);
  };
  auto op_plp = [&]() {
    regs.ps = pop() | AF_RESERVED | AF_BREAK;
    unpack_ps();
  };
  auto op_plx = [&]() {
    regs.x = pop();
    set_nz(regs.x);
  };
  auto op_ply = [&]() {
    regs.y = pop();
    set_nz(regs.y);
  };
  auto op_trb = [&]() {
    uint16_t val = read_byte(addr);
    flagz = !(regs.a & val);
    val &= ~regs.a;
    write_byte(addr, static_cast<uint8_t>(val));
  };
  auto op_tsb = [&]() {
    uint16_t val = read_byte(addr);
    flagz = !(regs.a & val);
    val |= regs.a;
    write_byte(addr, static_cast<uint8_t>(val));
  };

  // Arithmetic ADC / SBC
  auto op_adc_nmos = [&]() {
    uint16_t temp = read_byte(addr);
    if (regs.ps & AF_DECIMAL) {
      uint16_t val = regs.a + temp + flagc;
      flagz = !(val & 0xFF);
      flagn = val & 0x80;
      flagv = ((regs.a ^ val) & 0x80) && !((regs.a ^ temp) & 0x80);
      uint16_t low = (regs.a & 0x0F) + (temp & 0x0F) + flagc;
      if (low > 0x09) low += 0x06;
      uint16_t high = (regs.a >> 4) + (temp >> 4) + (low > 0x0F ? 1 : 0);
      if (high > 0x09) high += 0x06;
      flagc = (high > 0x0F);
      regs.a = (high << 4) | (low & 0x0F);
    } else {
      uint16_t val = regs.a + temp + flagc;
      flagc = (val > 0xFF);
      flagv = (((regs.a & 0x80) == (temp & 0x80)) &&
               ((regs.a & 0x80) != (val & 0x80)));
      regs.a = val & 0xFF;
      set_nz(regs.a);
    }
  };

  auto op_adc_cmos = [&](uint16_t& extra) {
    uint16_t temp = read_byte(addr);
    flagv = !((regs.a ^ temp) & 0x80);
    uint16_t val = 0;
    if (regs.ps & AF_DECIMAL) {
      extra++;
      val = (regs.a & 0x0f) + (temp & 0x0f) + flagc;
      if (val >= 0x0A) val = 0x10 | ((val + 6) & 0x0f);
      val += (regs.a & 0xf0) + (temp & 0xf0);
      if (val >= 0xA0) {
        flagc = 1;
        if (val >= 0x180) flagv = 0;
        val += 0x60;
      } else {
        flagc = 0;
        if (val < 0x80) flagv = 0;
      }
    } else {
      val = regs.a + temp + flagc;
      if (val >= 0x100) {
        flagc = 1;
        if (val >= 0x180) flagv = 0;
      } else {
        flagc = 0;
        if (val < 0x80) flagv = 0;
      }
    }
    regs.a = val & 0xFF;
    set_nz(regs.a);
  };

  auto op_sbc_nmos = [&]() {
    uint16_t temp = read_byte(addr);
    if (regs.ps & AF_DECIMAL) {
      uint16_t val = regs.a - temp - !flagc;
      flagn = val & 0x80;
      flagv = ((regs.a ^ val) & 0x80) && ((regs.a ^ temp) & 0x80);
      flagz = !(val & 0xFF);
      uint16_t low = (regs.a & 0x0F) - (temp & 0x0F) - !flagc;
      if (low & 0x10) low -= 0x06;
      uint16_t high = (regs.a >> 4) - (temp >> 4) - ((low & 0x10) >> 4);
      if (high & 0x10) high -= 0x06;
      flagc = !(high & 0x10);
      regs.a = (high << 4) | (low & 0x0F);
    } else {
      uint16_t val = regs.a - temp - !flagc;
      flagc = (val < 0x100);
      flagv = (((regs.a & 0x80) != (temp & 0x80)) &&
               ((regs.a & 0x80) != (val & 0x80)));
      regs.a = val & 0xFF;
      set_nz(regs.a);
    }
  };

  auto op_sbc_cmos = [&](uint16_t& extra) {
    uint16_t temp = read_byte(addr);
    flagv = ((regs.a ^ temp) & 0x80);
    uint16_t val = 0;
    if (regs.ps & AF_DECIMAL) {
      extra++;
      uint16_t temp2 = 0x0F + (regs.a & 0x0F) - (temp & 0x0F) + flagc;
      if (temp2 < 0x10) {
        val = 0;
        temp2 -= 0x06;
      } else {
        val = 0x10;
        temp2 -= 0x10;
      }
      val += 0xF0 + (regs.a & 0xF0) - (temp & 0xF0);
      if (val < 0x100) {
        flagc = 0;
        if (val < 0x80) flagv = 0;
        val -= 0x60;
      } else {
        flagc = 1;
        if (val >= 0x180) flagv = 0;
      }
      val += temp2;
    } else {
      val = 0xff + regs.a - temp + flagc;
      if (val < 0x100) {
        flagc = 0;
        if (val < 0x80) flagv = 0;
      } else {
        flagc = 1;
        if (val >= 0x180) flagv = 0;
      }
    }
    regs.a = val & 0xFF;
    set_nz(regs.a);
  };

  // Unofficial NMOS opcodes
  auto op_alr = [&]() {
    regs.a &= read_byte(addr);
    flagc = (regs.a & 1);
    flagn = 0;
    regs.a >>= 1;
    set_z(regs.a);
  };
  auto op_anc = [&]() {
    regs.a &= read_byte(addr);
    set_nz(regs.a);
    flagc = !!flagn;
  };
  auto op_arr = [&]() {
    uint16_t temp = regs.a & read_byte(addr);
    if (regs.ps & AF_DECIMAL) {
      uint16_t val = temp | (flagc ? 0x100 : 0);
      val >>= 1;
      flagn = (flagc ? 0x80 : 0);
      set_z(val);
      flagv = ((val ^ temp) & 0x40);
      if (((val & 0x0F) + (val & 0x01)) > 0x05) {
        val = (val & 0xF0) | ((val + 0x06) & 0x0F);
      }
      if (((val & 0xF0) + (val & 0x10)) > 0x50) {
        val = (val & 0x0F) | ((val + 0x60) & 0xF0);
        flagc = 1;
      } else {
        flagc = 0;
      }
      regs.a = val & 0xFF;
    } else {
      uint16_t val = temp | (flagc ? 0x100 : 0);
      val >>= 1;
      set_nz(val);
      flagc = !!(val & 0x40);
      flagv = ((val & 0x40) ^ ((val & 0x20) << 1));
      regs.a = val & 0xFF;
    }
  };
  auto op_aso = [&]() {
    uint16_t val = read_byte(addr) << 1;
    flagc = (val > 0xFF);
    write_byte(addr, static_cast<uint8_t>(val));
    regs.a |= val;
    set_nz(regs.a);
  };
  auto op_axa = [&]() {
    uint16_t val = regs.a & regs.x & (((base >> 8) + 1) & 0xFF);
    addr = (addr & 0x00FF) | (static_cast<uint16_t>(val) << 8);
    write_byte(addr, static_cast<uint8_t>(val));
  };
  auto op_axs = [&]() { write_byte(addr, regs.a & regs.x); };
  auto op_dcm = [&]() {
    uint16_t val = read_byte(addr) - 1;
    write_byte(addr, static_cast<uint8_t>(val));
    flagc = (regs.a >= val);
    val = regs.a - val;
    set_nz(val);
  };
  auto op_ins = [&]() {
    uint16_t val = read_byte(addr) + 1;
    write_byte(addr, static_cast<uint8_t>(val));
    uint16_t temp = val;
    if (regs.ps & AF_DECIMAL) {
      val = regs.a - temp - !flagc;
      flagn = val & 0x80;
      flagv = ((regs.a ^ val) & 0x80) && ((regs.a ^ temp) & 0x80);
      flagz = !(val & 0xFF);
      uint16_t low = (regs.a & 0x0F) - (temp & 0x0F) - !flagc;
      if (low & 0x10) low -= 0x06;
      uint16_t high = (regs.a >> 4) - (temp >> 4) - ((low & 0x10) >> 4);
      if (high & 0x10) high -= 0x06;
      flagc = !(high & 0x10);
      regs.a = (high << 4) | (low & 0x0F);
    } else {
      val = regs.a - temp - !flagc;
      flagc = (val < 0x100);
      flagv = (((regs.a & 0x80) != (temp & 0x80)) &&
               ((regs.a & 0x80) != (val & 0x80)));
      regs.a = val & 0xFF;
      set_nz(regs.a);
    }
  };
  auto op_las = [&]() {
    uint16_t val = static_cast<uint8_t>(read_byte(addr) & regs.sp);
    regs.a = regs.x = static_cast<uint8_t>(val);
    regs.sp = val | 0x100;
    set_nz(val);
  };
  auto op_lax = [&]() {
    regs.a = regs.x = read_byte(addr);
    set_nz(regs.a);
  };
  auto op_lse = [&]() {
    uint16_t val = read_byte(addr);
    flagc = (val & 1);
    val >>= 1;
    write_byte(addr, static_cast<uint8_t>(val));
    regs.a ^= val;
    set_nz(regs.a);
  };
  auto op_oal = [&]() {
    regs.a |= 0xEE;
    regs.a &= read_byte(addr);
    regs.x = regs.a;
    set_nz(regs.a);
  };
  auto op_rla = [&]() {
    uint16_t val = (read_byte(addr) << 1) | flagc;
    flagc = (val > 0xFF);
    write_byte(addr, static_cast<uint8_t>(val));
    regs.a &= val;
    set_nz(regs.a);
  };
  auto op_rra = [&]() {
    uint16_t temp = read_byte(addr);
    uint16_t val = (temp >> 1) | (flagc ? 0x80 : 0);
    flagc = (temp & 1);
    write_byte(addr, static_cast<uint8_t>(val));
    temp = val;
    if (regs.ps & AF_DECIMAL) {
      val = regs.a + temp + flagc;
      flagz = !(val & 0xFF);
      flagn = val & 0x80;
      flagv = ((regs.a ^ val) & 0x80) && !((regs.a ^ temp) & 0x80);
      uint16_t low = (regs.a & 0x0F) + (temp & 0x0F) + flagc;
      if (low > 0x09) low += 0x06;
      uint16_t high = (regs.a >> 4) + (temp >> 4) + (low > 0x0F ? 1 : 0);
      if (high > 0x09) high += 0x06;
      flagc = (high > 0x0F);
      regs.a = (high << 4) | (low & 0x0F);
    } else {
      val = regs.a + temp + flagc;
      flagc = (val > 0xFF);
      flagv = (((regs.a & 0x80) == (temp & 0x80)) &&
               ((regs.a & 0x80) != (val & 0x80)));
      regs.a = val & 0xFF;
      set_nz(regs.a);
    }
  };
  auto op_sax = [&]() {
    uint16_t temp = regs.a & regs.x;
    uint16_t val = read_byte(addr);
    flagc = (temp >= val);
    regs.x = temp - val;
    set_nz(regs.x);
  };
  auto op_say = [&]() {
    uint16_t val = regs.y & (((base >> 8) + 1) & 0xFF);
    addr = (addr & 0x00FF) | (static_cast<uint16_t>(val) << 8);
    write_byte(addr, static_cast<uint8_t>(val));
  };
  auto op_tas = [&]() {
    uint16_t val = regs.a & regs.x;
    regs.sp = 0x100 | val;
    val &= (((base >> 8) + 1) & 0xFF);
    addr = (addr & 0x00FF) | (static_cast<uint16_t>(val) << 8);
    write_byte(addr, static_cast<uint8_t>(val));
  };
  auto op_xaa = [&]() {
    regs.a = regs.x;
    regs.a &= read_byte(addr);
    set_nz(regs.a);
  };
  auto op_xas = [&]() {
    uint16_t val = regs.x & (((base >> 8) + 1) & 0xFF);
    addr = (addr & 0x00FF) | (static_cast<uint16_t>(val) << 8);
    write_byte(addr, static_cast<uint8_t>(val));
  };

  auto check_nmi = [&]() {
#ifdef ENABLE_NMI_SUPPORT
    if (g_nmi_flank) {
      g_nmi_flank = false;
      g_cycle_irq_start = g_cumulative_cycles + executed_cycles;
      push(regs.pc >> 8);
      push(regs.pc & 0xFF);
      pack_ps();
      push(regs.ps & ~AF_BREAK);
      regs.ps = (regs.ps | AF_INTERRUPT) & ~AF_DECIMAL;
      regs.pc = read_u16_unaligned(mem + NMI_VECTOR_ADDR);
      executed_cycles += 7;
    }
#endif
  };

  auto check_irq = [&]() {
    if (g_bm_irq && !(regs.ps & AF_INTERRUPT)) {
      g_cycle_irq_start = g_cumulative_cycles + executed_cycles;
      push(regs.pc >> 8);
      push(regs.pc & 0xFF);
      pack_ps();
      push(regs.ps & ~AF_BREAK);
      regs.ps = (regs.ps | AF_INTERRUPT) & ~AF_DECIMAL;
      regs.pc = read_u16_unaligned(mem + IRQ_VECTOR_ADDR);
      executed_cycles += 7;
    }
  };

  do {
    uint16_t extra_cycles = 0;
    uint8_t opcode = 0;

    fetch_opcode(opcode, executed_cycles);

    switch (opcode) {
      case 0x00:
        op_brk();
        executed_cycles += 7 + extra_cycles;
        break;
      case 0x01:
        addr_indx();
        op_ora();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x02:
        if (is_cmos) {
          addr_imm();
          executed_cycles += 2 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x03:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indx();
          op_aso();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0x04:
        if (is_cmos) {
          addr_zpg();
          op_tsb();
          executed_cycles += 5 + extra_cycles;
        } else {
          addr_zpg();
          executed_cycles += 3 + extra_cycles;
        }
        break;
      case 0x05:
        addr_zpg();
        op_ora();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x06:
        addr_zpg();
        op_asl();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0x07:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpg();
          op_aso();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0x08:
        op_php();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x09:
        addr_imm();
        op_ora();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x0A:
        op_asla();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x0B:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          op_anc();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x0C:
        if (is_cmos) {
          addr_abs();
          op_tsb();
          executed_cycles += 6 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x0D:
        addr_abs();
        op_ora();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x0E:
        addr_abs();
        op_asl();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x0F:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_abs();
          op_aso();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x10:
        addr_rel();
        if (!flagn) branch_taken(extra_cycles);
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x11:
        addr_indy(extra_cycles);
        op_ora();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0x12:
        if (is_cmos) {
          addr_izpg();
          op_ora();
          executed_cycles += 5 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x13:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indy(extra_cycles);
          op_aso();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0x14:
        if (is_cmos) {
          addr_zpg();
          op_trb();
          executed_cycles += 5 + extra_cycles;
        } else {
          addr_zpgx();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x15:
        addr_zpgx();
        op_ora();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x16:
        addr_zpgx();
        op_asl();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x17:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpgx();
          op_aso();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x18:
        flagc = 0;
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x19:
        addr_absy(extra_cycles);
        op_ora();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x1A:
        if (is_cmos) {
          op_ina();
          executed_cycles += 2 + extra_cycles;
        } else {
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x1B:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_aso();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0x1C:
        if (is_cmos) {
          addr_abs();
          op_trb();
          executed_cycles += 6 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x1D:
        addr_absx(extra_cycles);
        op_ora();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x1E:
        addr_absx(extra_cycles);
        op_asl();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x1F:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          op_aso();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0x20:
        addr_abs();
        op_jsr();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x21:
        addr_indx();
        op_and();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x22:
        if (is_cmos) {
          addr_imm();
          executed_cycles += 2 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x23:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indx();
          op_rla();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0x24:
        addr_zpg();
        op_bit();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x25:
        addr_zpg();
        op_and();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x26:
        addr_zpg();
        op_rol();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0x27:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpg();
          op_rla();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0x28:
        op_plp();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x29:
        addr_imm();
        op_and();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x2A:
        op_rola();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x2B:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          op_anc();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x2C:
        addr_abs();
        op_bit();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x2D:
        addr_abs();
        op_and();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x2E:
        addr_abs();
        op_rol();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x2F:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_abs();
          op_rla();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x30:
        addr_rel();
        if (flagn) branch_taken(extra_cycles);
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x31:
        addr_indy(extra_cycles);
        op_and();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0x32:
        if (is_cmos) {
          addr_izpg();
          op_and();
          executed_cycles += 5 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x33:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indy(extra_cycles);
          op_rla();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0x34:
        if (is_cmos) {
          addr_zpgx();
          op_bit();
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_zpgx();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x35:
        addr_zpgx();
        op_and();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x36:
        addr_zpgx();
        op_rol();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x37:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpgx();
          op_rla();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x38:
        flagc = 1;
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x39:
        addr_absy(extra_cycles);
        op_and();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x3A:
        if (is_cmos) {
          op_dea();
          executed_cycles += 2 + extra_cycles;
        } else {
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x3B:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_rla();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0x3C:
        if (is_cmos) {
          addr_absx(extra_cycles);
          op_bit();
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x3D:
        addr_absx(extra_cycles);
        op_and();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x3E:
        addr_absx(extra_cycles);
        op_rol();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x3F:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          op_rla();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0x40:
        op_rti();
        do_irq_profiling(executed_cycles);
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x41:
        addr_indx();
        op_eor();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x42:
        if (is_cmos) {
          addr_imm();
          executed_cycles += 2 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x43:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indx();
          op_lse();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0x44:
        addr_zpg();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x45:
        addr_zpg();
        op_eor();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x46:
        addr_zpg();
        op_lsr();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0x47:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpg();
          op_lse();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0x48:
        op_pha();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x49:
        addr_imm();
        op_eor();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x4A:
        op_lsra();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x4B:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          op_alr();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x4C:
        addr_abs();
        op_jmp();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x4D:
        addr_abs();
        op_eor();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x4E:
        addr_abs();
        op_lsr();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x4F:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_abs();
          op_lse();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x50:
        addr_rel();
        if (!flagv) branch_taken(extra_cycles);
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x51:
        addr_indy(extra_cycles);
        op_eor();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0x52:
        if (is_cmos) {
          addr_izpg();
          op_eor();
          executed_cycles += 5 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x53:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indy(extra_cycles);
          op_lse();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0x54:
        addr_zpgx();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x55:
        addr_zpgx();
        op_eor();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x56:
        addr_zpgx();
        op_lsr();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x57:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpgx();
          op_lse();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x58:
        regs.ps &= ~AF_INTERRUPT;
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x59:
        addr_absy(extra_cycles);
        op_eor();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x5A:
        if (is_cmos) {
          op_phy();
          executed_cycles += 3 + extra_cycles;
        } else {
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x5B:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_lse();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0x5C:
        if (is_cmos) {
          addr_absx(extra_cycles);
          executed_cycles += 8 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x5D:
        addr_absx(extra_cycles);
        op_eor();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x5E:
        addr_absx(extra_cycles);
        op_lsr();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x5F:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          op_lse();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0x60:
        op_rts();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x61:
        if (is_cmos) {
          addr_indx();
          op_adc_cmos(extra_cycles);
          executed_cycles += 6 + extra_cycles;
        } else {
          addr_indx();
          op_adc_nmos();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x62:
        if (is_cmos) {
          addr_imm();
          executed_cycles += 2 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x63:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indx();
          op_rra();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0x64:
        if (is_cmos) {
          addr_zpg();
          op_stz();
          executed_cycles += 3 + extra_cycles;
        } else {
          addr_zpg();
          executed_cycles += 3 + extra_cycles;
        }
        break;
      case 0x65:
        if (is_cmos) {
          addr_zpg();
          op_adc_cmos(extra_cycles);
          executed_cycles += 3 + extra_cycles;
        } else {
          addr_zpg();
          op_adc_nmos();
          executed_cycles += 3 + extra_cycles;
        }
        break;
      case 0x66:
        addr_zpg();
        op_ror();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0x67:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpg();
          op_rra();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0x68:
        op_pla();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x69:
        if (is_cmos) {
          addr_imm();
          op_adc_cmos(extra_cycles);
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          op_adc_nmos();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x6A:
        op_rora();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x6B:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          op_arr();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x6C:
        if (is_cmos) {
          addr_iabs_cmos(extra_cycles);
          op_jmp();
          executed_cycles += 6 + extra_cycles;
        } else {
          addr_iabs_nmos();
          op_jmp();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x6D:
        if (is_cmos) {
          addr_abs();
          op_adc_cmos(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_abs();
          op_adc_nmos();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x6E:
        addr_abs();
        op_ror();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x6F:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_abs();
          op_rra();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x70:
        addr_rel();
        if (flagv) branch_taken(extra_cycles);
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x71:
        if (is_cmos) {
          addr_indy(extra_cycles);
          op_adc_cmos(extra_cycles);
          executed_cycles += 5 + extra_cycles;
        } else {
          addr_indy(extra_cycles);
          op_adc_nmos();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0x72:
        if (is_cmos) {
          addr_izpg();
          op_adc_cmos(extra_cycles);
          executed_cycles += 5 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x73:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indy(extra_cycles);
          op_rra();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0x74:
        if (is_cmos) {
          addr_zpgx();
          op_stz();
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_zpgx();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x75:
        if (is_cmos) {
          addr_zpgx();
          op_adc_cmos(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_zpgx();
          op_adc_nmos();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x76:
        addr_zpgx();
        op_ror();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x77:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpgx();
          op_rra();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x78:
        regs.ps |= AF_INTERRUPT;
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x79:
        if (is_cmos) {
          addr_absy(extra_cycles);
          op_adc_cmos(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_adc_nmos();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x7A:
        if (is_cmos) {
          op_ply();
          executed_cycles += 4 + extra_cycles;
        } else {
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x7B:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_rra();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0x7C:
        if (is_cmos) {
          addr_iabsx();
          op_jmp();
          executed_cycles += 6 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x7D:
        if (is_cmos) {
          addr_absx(extra_cycles);
          op_adc_cmos(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          op_adc_nmos();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x7E:
        addr_absx(extra_cycles);
        op_ror();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x7F:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          op_rra();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0x80:
        if (is_cmos) {
          addr_rel();
          branch_taken(extra_cycles);
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x81:
        addr_indx();
        op_sta();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x82:
        addr_imm();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x83:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indx();
          op_axs();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x84:
        addr_zpg();
        op_sty();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x85:
        addr_zpg();
        op_sta();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x86:
        addr_zpg();
        op_stx();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0x87:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpg();
          op_axs();
          executed_cycles += 3 + extra_cycles;
        }
        break;
      case 0x88:
        op_dey();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x89:
        if (is_cmos) {
          addr_imm();
          op_biti();
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x8A:
        op_txa();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x8B:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          op_xaa();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x8C:
        addr_abs();
        op_sty();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x8D:
        addr_abs();
        op_sta();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x8E:
        addr_abs();
        op_stx();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x8F:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_abs();
          op_axs();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x90:
        addr_rel();
        if (!flagc) branch_taken(extra_cycles);
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x91:
        addr_indy(extra_cycles);
        op_sta();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0x92:
        if (is_cmos) {
          addr_izpg();
          op_sta();
          executed_cycles += 5 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0x93:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indy(extra_cycles);
          op_axa();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0x94:
        addr_zpgx();
        op_sty();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x95:
        addr_zpgx();
        op_sta();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x96:
        addr_zpgy();
        op_stx();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0x97:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpgy();
          op_axs();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0x98:
        op_tya();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x99:
        addr_absy(extra_cycles);
        op_sta();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0x9A:
        op_txs();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0x9B:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_tas();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0x9C:
        if (is_cmos) {
          addr_abs();
          op_stz();
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          op_say();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0x9D:
        addr_absx(extra_cycles);
        op_sta();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0x9E:
        if (is_cmos) {
          addr_absx(extra_cycles);
          op_stz();
          executed_cycles += 5 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_xas();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0x9F:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_axa();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0xA0:
        addr_imm();
        op_ldy();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xA1:
        addr_indx();
        op_lda();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0xA2:
        addr_imm();
        op_ldx();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xA3:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indx();
          op_lax();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0xA4:
        addr_zpg();
        op_ldy();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0xA5:
        addr_zpg();
        op_lda();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0xA6:
        addr_zpg();
        op_ldx();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0xA7:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpg();
          op_lax();
          executed_cycles += 3 + extra_cycles;
        }
        break;
      case 0xA8:
        op_tay();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xA9:
        addr_imm();
        op_lda();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xAA:
        op_tax();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xAB:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          op_oal();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0xAC:
        addr_abs();
        op_ldy();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xAD:
        addr_abs();
        op_lda();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xAE:
        addr_abs();
        op_ldx();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xAF:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_abs();
          op_lax();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0xB0:
        addr_rel();
        if (flagc) branch_taken(extra_cycles);
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xB1:
        addr_indy(extra_cycles);
        op_lda();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0xB2:
        if (is_cmos) {
          addr_izpg();
          op_lda();
          executed_cycles += 5 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0xB3:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indy(extra_cycles);
          op_lax();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0xB4:
        addr_zpgx();
        op_ldy();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xB5:
        addr_zpgx();
        op_lda();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xB6:
        addr_zpgy();
        op_ldx();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xB7:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpgy();
          op_lax();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0xB8:
        flagv = 0;
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xB9:
        addr_absy(extra_cycles);
        op_lda();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xBA:
        op_tsx();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xBB:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_las();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0xBC:
        addr_absx(extra_cycles);
        op_ldy();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xBD:
        addr_absx(extra_cycles);
        op_lda();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xBE:
        addr_absy(extra_cycles);
        op_ldx();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xBF:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_lax();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0xC0:
        addr_imm();
        op_cpy();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xC1:
        addr_indx();
        op_cmp();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0xC2:
        addr_imm();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xC3:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indx();
          op_dcm();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0xC4:
        addr_zpg();
        op_cpy();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0xC5:
        addr_zpg();
        op_cmp();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0xC6:
        addr_zpg();
        op_dec();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0xC7:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpg();
          op_dcm();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0xC8:
        op_iny();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xC9:
        addr_imm();
        op_cmp();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xCA:
        op_dex();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xCB:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          op_sax();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0xCC:
        addr_abs();
        op_cpy();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xCD:
        addr_abs();
        op_cmp();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xCE:
        addr_abs();
        op_dec();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0xCF:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_abs();
          op_dcm();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0xD0:
        addr_rel();
        if (!flagz) branch_taken(extra_cycles);
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xD1:
        addr_indy(extra_cycles);
        op_cmp();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0xD2:
        if (is_cmos) {
          addr_izpg();
          op_cmp();
          executed_cycles += 5 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0xD3:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indy(extra_cycles);
          op_dcm();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0xD4:
        addr_zpgx();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xD5:
        addr_zpgx();
        op_cmp();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xD6:
        addr_zpgx();
        op_dec();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0xD7:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpgx();
          op_dcm();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0xD8:
        regs.ps &= ~AF_DECIMAL;
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xD9:
        addr_absy(extra_cycles);
        op_cmp();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xDA:
        if (is_cmos) {
          op_phx();
          executed_cycles += 3 + extra_cycles;
        } else {
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0xDB:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_dcm();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0xDC:
        addr_absx(extra_cycles);
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xDD:
        addr_absx(extra_cycles);
        op_cmp();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xDE:
        addr_absx(extra_cycles);
        op_dec();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0xDF:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          op_dcm();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0xE0:
        addr_imm();
        op_cpx();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xE1:
        if (is_cmos) {
          addr_indx();
          op_sbc_cmos(extra_cycles);
          executed_cycles += 6 + extra_cycles;
        } else {
          addr_indx();
          op_sbc_nmos();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0xE2:
        addr_imm();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xE3:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indx();
          op_ins();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0xE4:
        addr_zpg();
        op_cpx();
        executed_cycles += 3 + extra_cycles;
        break;
      case 0xE5:
        if (is_cmos) {
          addr_zpg();
          op_sbc_cmos(extra_cycles);
          executed_cycles += 3 + extra_cycles;
        } else {
          addr_zpg();
          op_sbc_nmos();
          executed_cycles += 3 + extra_cycles;
        }
        break;
      case 0xE6:
        addr_zpg();
        op_inc();
        executed_cycles += 5 + extra_cycles;
        break;
      case 0xE7:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpg();
          op_ins();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0xE8:
        op_inx();
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xE9:
        if (is_cmos) {
          addr_imm();
          op_sbc_cmos(extra_cycles);
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          op_sbc_nmos();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0xEA:
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xEB:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_imm();
          op_sbc_nmos();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0xEC:
        addr_abs();
        op_cpx();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xED:
        if (is_cmos) {
          addr_abs();
          op_sbc_cmos(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_abs();
          op_sbc_nmos();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0xEE:
        addr_abs();
        op_inc();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0xEF:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_abs();
          op_ins();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0xF0:
        addr_rel();
        if (flagz) branch_taken(extra_cycles);
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xF1:
        if (is_cmos) {
          addr_indy(extra_cycles);
          op_sbc_cmos(extra_cycles);
          executed_cycles += 5 + extra_cycles;
        } else {
          addr_indy(extra_cycles);
          op_sbc_nmos();
          executed_cycles += 5 + extra_cycles;
        }
        break;
      case 0xF2:
        if (is_cmos) {
          addr_izpg();
          op_sbc_cmos(extra_cycles);
          executed_cycles += 5 + extra_cycles;
        } else {
          op_hlt();
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0xF3:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_indy(extra_cycles);
          op_ins();
          executed_cycles += 8 + extra_cycles;
        }
        break;
      case 0xF4:
        addr_zpgx();
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xF5:
        if (is_cmos) {
          addr_zpgx();
          op_sbc_cmos(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_zpgx();
          op_sbc_nmos();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0xF6:
        addr_zpgx();
        op_inc();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0xF7:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_zpgx();
          op_ins();
          executed_cycles += 6 + extra_cycles;
        }
        break;
      case 0xF8:
        regs.ps |= AF_DECIMAL;
        executed_cycles += 2 + extra_cycles;
        break;
      case 0xF9:
        if (is_cmos) {
          addr_absy(extra_cycles);
          op_sbc_cmos(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_sbc_nmos();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0xFA:
        if (is_cmos) {
          op_plx();
          executed_cycles += 4 + extra_cycles;
        } else {
          executed_cycles += 2 + extra_cycles;
        }
        break;
      case 0xFB:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absy(extra_cycles);
          op_ins();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      case 0xFC:
        addr_absx(extra_cycles);
        executed_cycles += 4 + extra_cycles;
        break;
      case 0xFD:
        if (is_cmos) {
          addr_absx(extra_cycles);
          op_sbc_cmos(extra_cycles);
          executed_cycles += 4 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          op_sbc_nmos();
          executed_cycles += 4 + extra_cycles;
        }
        break;
      case 0xFE:
        addr_absx(extra_cycles);
        op_inc();
        executed_cycles += 6 + extra_cycles;
        break;
      case 0xFF:
        if (is_cmos) {
          executed_cycles += 2 + extra_cycles;
        } else {
          addr_absx(extra_cycles);
          op_ins();
          executed_cycles += 7 + extra_cycles;
        }
        break;
      default:
        break;
    }

    pack_ps();
    check_nmi();
    check_irq();
  } while (executed_cycles < total_cycles);

  return executed_cycles;
}

static auto internal_cpu_execute(uint32_t total_cycles) -> uint32_t {
  if (IS_APPLE2() || (g_apple2_type == A2TYPE_APPLE2E)) {
    return cpu_execute_loop<false>(
        total_cycles);  // Apple ][, ][+, //e (NMOS 6502)
  } else {
    return cpu_execute_loop<true>(
        total_cycles);  // Enhanced Apple //e (CMOS 65C02)
  }
}

// Modern API implementation

auto cpu_destroy() -> void {
  if (g_crit_section_valid) {
    g_crit_section_valid = false;
  }
}

auto cpu_calc_cycles(uint32_t executed_cycles) -> void {
  uint32_t cycles = executed_cycles - g_cycles_executed;
#ifdef UPDATE_ALL_PER_CYCLE
  assert((int32_t)cycles >= 0);
#endif
  g_cycles_executed += cycles;
  g_cumulative_cycles += cycles;
}

#ifdef UPDATE_ALL_PER_CYCLE
auto cpu_get_cycles_this_frame(uint32_t) -> uint32_t {
  cpu_calc_cycles(g_internal_executed_cycles);
  return g_cycles_this_frame + g_cycles_executed;
}
#else
auto cpu_get_cycles_this_frame(uint32_t executed_cycles) -> uint32_t {
  cpu_calc_cycles(executed_cycles);
  return g_cycles_this_frame + g_cycles_executed;
}
#endif

auto cpu_execute(uint32_t total_cycles) -> uint32_t {
  uint32_t executed_cycles = 0;

  g_cycles_submitted = total_cycles;
  g_cycles_executed = 0;

  if (total_cycles == 0) {  // Do single step
    executed_cycles = internal_cpu_execute(0);
  } else {  // Do multi-opcode emulation
    executed_cycles = internal_cpu_execute(total_cycles);
  }

  uint32_t remaining_cycles = executed_cycles - g_cycles_executed;
  g_cumulative_cycles += remaining_cycles;

  return executed_cycles;
}

auto cpu_initialize() -> void {
  cpu_destroy();
  regs.a = regs.x = regs.y = regs.ps = 0xFF;
  regs.sp = 0x01FF;
  cpu_reset();

  g_crit_section_valid = true;
  cpu_irq_reset();
  cpu_nmi_reset();
}

auto cpu_setup_benchmark() -> void {
  regs.a = 0;
  regs.x = 0;
  regs.y = 0;
  regs.pc = 0x300;
  regs.sp = 0x1FF;

  {
    uint16_t addr = 0x300;
    uint8_t opcode = 0;
    do {
      *(mem + addr++) = benchopcode[opcode];
      *(mem + addr++) = benchopcode[opcode];

      if (opcode >= SHORTOPCODES) {
        *(mem + addr++) = 0;
      }

      if ((++opcode >= BENCHOPCODES) || ((addr & 0x0F) >= 0x0B)) {
        uint8_t jump_low = (opcode >= BENCHOPCODES)
                               ? 0x00
                               : static_cast<uint8_t>(((addr >> 4) + 1) << 4);
        *(mem + addr++) = 0x4C;
        *(mem + addr++) = jump_low;
        *(mem + addr++) = 0x03;
        while (addr & 0x0F) {
          ++addr;
        }
      }
    } while (opcode < BENCHOPCODES);
  }
}

auto cpu_irq_reset() -> void {
  assert(g_crit_section_valid);
  if (g_crit_section_valid) {
    pthread_mutex_lock(&g_critical_section);
  }
  g_bm_irq = 0;
  if (g_crit_section_valid) {
    pthread_mutex_unlock(&g_critical_section);
  }
}

auto cpu_irq_assert(IrqSrc_t device) -> void {
  assert(g_crit_section_valid);
  if (g_crit_section_valid) {
    pthread_mutex_lock(&g_critical_section);
  }
  g_bm_irq |= 1 << device;
  if (g_crit_section_valid) {
    pthread_mutex_unlock(&g_critical_section);
  }
}

auto cpu_irq_deassert(IrqSrc_t device) -> void {
  assert(g_crit_section_valid);
  if (g_crit_section_valid) {
    pthread_mutex_lock(&g_critical_section);
  }
  g_bm_irq &= ~(1 << device);
  if (g_crit_section_valid) {
    pthread_mutex_unlock(&g_critical_section);
  }
}

auto cpu_nmi_reset() -> void {
  assert(g_crit_section_valid);
  if (g_crit_section_valid) {
    pthread_mutex_lock(&g_critical_section);
  }
  g_bm_nmi = 0;
  g_nmi_flank = false;
  if (g_crit_section_valid) {
    pthread_mutex_unlock(&g_critical_section);
  }
}

auto cpu_nmi_assert(IrqSrc_t device) -> void {
  assert(g_crit_section_valid);
  if (g_crit_section_valid) {
    pthread_mutex_lock(&g_critical_section);
  }
  if (g_bm_nmi == 0) {  // NMI line is just becoming active
    g_nmi_flank = true;
  }
  g_bm_nmi |= 1 << device;
  if (g_crit_section_valid) {
    pthread_mutex_unlock(&g_critical_section);
  }
}

auto cpu_nmi_deassert(IrqSrc_t device) -> void {
  assert(g_crit_section_valid);
  if (g_crit_section_valid) {
    pthread_mutex_lock(&g_critical_section);
  }
  g_bm_nmi &= ~(1 << device);
  if (g_crit_section_valid) {
    pthread_mutex_unlock(&g_critical_section);
  }
}

auto cpu_reset() -> void {
  regs.ps = (regs.ps | AF_INTERRUPT) & ~AF_DECIMAL;
  regs.pc = *reinterpret_cast<uint16_t*>(mem + 0xFFFC);
  regs.sp = 0x0100 | ((regs.sp - 3) & 0xFF);

  regs.is_jammed = 0;
}

auto cpu_get_snapshot(SsCpu6502_t* snapshot) -> uint32_t {
  if (!snapshot) {
    return 1;
  }
  g_active_cpu->cpu_regs = regs;
  g_active_cpu->cumulative_cycles = g_cumulative_cycles;

  snapshot->a = regs.a;
  snapshot->x = regs.x;
  snapshot->y = regs.y;
  snapshot->p = regs.ps | AF_RESERVED | AF_BREAK;
  snapshot->s = static_cast<uint8_t>(regs.sp & 0xff);
  snapshot->pc = regs.pc;
  snapshot->cumulative_cycles = g_cumulative_cycles;

  return 0;
}

auto cpu_set_snapshot(SsCpu6502_t* snapshot) -> uint32_t {
  if (!snapshot) {
    return 1;
  }
  regs.a = snapshot->a;
  regs.x = snapshot->x;
  regs.y = snapshot->y;
  regs.ps = snapshot->p | AF_RESERVED | AF_BREAK;
  regs.sp = static_cast<uint16_t>(snapshot->s) | 0x100;
  regs.pc = snapshot->pc;
  cpu_irq_reset();
  cpu_nmi_reset();
  g_cumulative_cycles = snapshot->cumulative_cycles;

  g_active_cpu->cpu_regs = regs;
  g_active_cpu->cumulative_cycles = g_cumulative_cycles;

  return 0;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// bugprone-easily-swappable-parameters, google-readability-function-size)

auto cpu_step() -> void { cpu_execute(0); }
