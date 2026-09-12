// SPDX-License-Identifier: GPL-2.0-only

#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>

#include "apple2/Apple2Types.h"
#define CPU_CPP_IMPL
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral_Types.h"
#include "core/Util_Endian.h"

// Unavoidable hardware architectural constraints for low-level 6502 CPU core
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-pro-bounds-pointer-arithmetic, bugprone-easily-swappable-parameters, google-readability-function-size)

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
static std::mutex g_interrupt_mutex;

auto cpu_get_registers() -> CpuRegisters_t* { return &regs; }
auto cpu_get_cumulative_cycles() -> uint64_t { return g_cumulative_cycles; }
auto cpu_get_active_context() -> CpuInstance_t* { return g_active_cpu; }
auto cpu_set_active_context(CpuInstance_t* context) -> void {
  if (context == nullptr) {
    return;
  }
  g_active_cpu->cpu_regs = regs;
  g_active_cpu->cumulative_cycles = g_cumulative_cycles;
  g_active_cpu->cycles_submitted = g_cycles_submitted;
  g_active_cpu->cycles_executed = g_cycles_executed;

  {
    std::lock_guard<std::mutex> lock(g_interrupt_mutex);
    g_active_cpu->bm_irq = g_bm_irq.load();
    g_active_cpu->bm_nmi = g_bm_nmi.load();
    g_active_cpu->nmi_flank = g_nmi_flank.load();

    g_active_cpu = context;

    regs = g_active_cpu->cpu_regs;
    g_cumulative_cycles = g_active_cpu->cumulative_cycles;
    g_cycles_submitted = g_active_cpu->cycles_submitted;
    g_cycles_executed = g_active_cpu->cycles_executed;
    g_bm_irq.store(g_active_cpu->bm_irq);
    g_bm_nmi.store(g_active_cpu->bm_nmi);
    g_nmi_flank.store(g_active_cpu->nmi_flank);
  }
}

static uint32_t g_internal_executed_cycles;

extern auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write,
                            uint8_t d, uint32_t cycles) -> uint8_t;

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

struct CpuLoopContext_t;
struct OpcodeDesc_t {
  void (*handler)(CpuLoopContext_t& ctx);
  uint8_t base_cycles;
};

struct CpuLoopContext_t {
  uint16_t addr = 0;
  uint16_t base = 0;
  uint16_t extra_cycles = 0;
  uint8_t flagc = 0;
  uint8_t flagn = 0;
  uint8_t flagv = 0;
  uint8_t flagz = 0;
  uint32_t executed_cycles = 0;

  inline auto set_nz(uint16_t a) -> void {
    flagn = (a & 0x80);
    flagz = !((a) & 0xFF);
  }

  inline auto set_z(uint16_t a) -> void { flagz = !((a) & 0xFF); }

  inline auto pack_ps() -> void {
    regs.ps = (regs.ps & ~(AF_CARRY | AF_SIGN | AF_OVERFLOW | AF_ZERO)) |
              flagc | flagn | (flagv ? AF_OVERFLOW : 0) |
              (flagz ? AF_ZERO : 0) | AF_RESERVED | AF_BREAK;
  }

  inline auto unpack_ps() -> void {
    flagc = (regs.ps & AF_CARRY);
    flagn = (regs.ps & AF_SIGN);
    flagv = (regs.ps & AF_OVERFLOW);
    flagz = (regs.ps & AF_ZERO);
  }

  inline auto push(uint8_t a) -> void {
    *(mem + regs.sp--) = a;
    if (regs.sp < STACK_BEGIN) regs.sp = STACK_END;
  }

  inline auto pop() -> uint8_t {
    return *(mem +
             ((regs.sp >= STACK_END) ? (regs.sp = STACK_BEGIN) : ++regs.sp));
  }

  inline auto read_byte(uint16_t a) -> uint8_t {
    if ((a & IO_REGION_MASK) == IO_REGION_START) {
      return io_map_dispatch(regs.pc, a, 0, 0, executed_cycles);
    }
    return *(mem + a);
  }

  inline auto write_byte(uint16_t a, uint8_t val) -> void {
    memdirty[a >> 8] = 0xFF;
    uint8_t* page = memwrite[a >> 8];
    if (page) {
      *(page + (a & 0xFF)) = val;
    } else if ((a & IO_REGION_MASK) == IO_REGION_START) {
      io_map_dispatch(regs.pc, a, 1, val, executed_cycles);
    }
  }

  inline auto check_page_change(uint16_t b, uint16_t a) -> void {
    if ((b ^ a) & 0xFF00) extra_cycles = 1;
  }

  inline auto branch_taken() -> void {
    uint16_t old_pc = regs.pc;
    regs.pc += addr;
    if ((old_pc ^ regs.pc) & 0xFF00) {
      extra_cycles = 2;
    } else {
      extra_cycles = 1;
    }
  }

  // Addressing modes
  inline auto addr_imm() -> void { addr = regs.pc++; }
  inline auto addr_zpg() -> void { addr = *(mem + regs.pc++); }
  inline auto addr_zpgx() -> void {
    addr = (*(mem + regs.pc++) + regs.x) & 0xFF;
  }
  inline auto addr_zpgy() -> void {
    addr = (*(mem + regs.pc++) + regs.y) & 0xFF;
  }
  inline auto addr_abs() -> void {
    addr = read_u16_unaligned(mem + regs.pc);
    regs.pc += 2;
  }
  inline auto addr_absx() -> void {
    base = read_u16_unaligned(mem + regs.pc);
    addr = base + static_cast<uint16_t>(regs.x);
    regs.pc += 2;
    check_page_change(base, addr);
  }
  inline auto addr_absy() -> void {
    base = read_u16_unaligned(mem + regs.pc);
    addr = base + static_cast<uint16_t>(regs.y);
    regs.pc += 2;
    check_page_change(base, addr);
  }
  inline auto addr_iabs_nmos() -> void {
    base = read_u16_unaligned(mem + regs.pc);
    if ((base & 0xFF) == 0xFF) {
      addr = *(mem + base) +
             (static_cast<uint16_t>(*(mem + (base & 0xFF00))) << 8);
    } else {
      addr = read_u16_unaligned(mem + base);
    }
    regs.pc += 2;
  }
  inline auto addr_iabs_cmos() -> void {
    base = read_u16_unaligned(mem + regs.pc);
    addr = read_u16_unaligned(mem + base);
    if ((base & 0xFF) == 0xFF) extra_cycles = 1;
    regs.pc += 2;
  }
  inline auto addr_iabsx() -> void {
    addr = read_u16_unaligned(mem + read_u16_unaligned(mem + regs.pc) +
                              static_cast<uint16_t>(regs.x));
    regs.pc += 2;
  }
  inline auto addr_indx() -> void {
    base = (*(mem + regs.pc++) + regs.x) & 0xFF;
    if (base == 0xFF) {
      addr = *(mem + 0xFF) + (static_cast<uint16_t>(*mem) << 8);
    } else {
      addr = read_u16_unaligned(mem + base);
    }
  }
  inline auto addr_indy() -> void {
    if (*(mem + regs.pc) == 0xFF) {
      base = *(mem + 0xFF) + (static_cast<uint16_t>(*mem) << 8);
    } else {
      base = read_u16_unaligned(mem + *(mem + regs.pc));
    }
    regs.pc++;
    addr = base + static_cast<uint16_t>(regs.y);
    check_page_change(base, addr);
  }
  inline auto addr_izpg() -> void {
    base = *(mem + regs.pc++);
    if (base == 0xFF) {
      addr = *(mem + 0xFF) + (static_cast<uint16_t>(*mem) << 8);
    } else {
      addr = read_u16_unaligned(mem + base);
    }
  }
  inline auto addr_rel() -> void {
    addr = static_cast<uint16_t>(
        static_cast<int16_t>(static_cast<signed char>(*(mem + regs.pc++))));
  }

  // Standard Opcode Implementations
  inline auto op_lda() -> void {
    regs.a = read_byte(addr);
    set_nz(regs.a);
  }
  inline auto op_ldx() -> void {
    regs.x = read_byte(addr);
    set_nz(regs.x);
  }
  inline auto op_ldy() -> void {
    regs.y = read_byte(addr);
    set_nz(regs.y);
  }
  inline auto op_sta() -> void { write_byte(addr, regs.a); }
  inline auto op_stx() -> void { write_byte(addr, regs.x); }
  inline auto op_sty() -> void { write_byte(addr, regs.y); }
  inline auto op_stz() -> void { write_byte(addr, 0); }
  inline auto op_tax() -> void {
    regs.x = regs.a;
    set_nz(regs.x);
  }
  inline auto op_txa() -> void {
    regs.a = regs.x;
    set_nz(regs.a);
  }
  inline auto op_tay() -> void {
    regs.y = regs.a;
    set_nz(regs.y);
  }
  inline auto op_tya() -> void {
    regs.a = regs.y;
    set_nz(regs.a);
  }
  inline auto op_tsx() -> void {
    regs.x = regs.sp & 0xFF;
    set_nz(regs.x);
  }
  inline auto op_txs() -> void { regs.sp = 0x100 | regs.x; }
  inline auto op_and() -> void {
    regs.a &= read_byte(addr);
    set_nz(regs.a);
  }
  inline auto op_ora() -> void {
    regs.a |= read_byte(addr);
    set_nz(regs.a);
  }
  inline auto op_eor() -> void {
    regs.a ^= read_byte(addr);
    set_nz(regs.a);
  }
  inline auto op_bit() -> void {
    uint16_t val = read_byte(addr);
    flagz = !(regs.a & val);
    flagn = val & 0x80;
    flagv = val & 0x40;
  }
  inline auto op_biti() -> void { flagz = !(regs.a & read_byte(addr)); }
  inline auto op_cmp() -> void {
    uint16_t val = read_byte(addr);
    flagc = (regs.a >= val);
    val = regs.a - val;
    set_nz(val);
  }
  inline auto op_cpx() -> void {
    uint16_t val = read_byte(addr);
    flagc = (regs.x >= val);
    val = regs.x - val;
    set_nz(val);
  }
  inline auto op_cpy() -> void {
    uint16_t val = read_byte(addr);
    flagc = (regs.y >= val);
    val = regs.y - val;
    set_nz(val);
  }
  inline auto op_asla() -> void {
    uint16_t val = regs.a << 1;
    flagc = (val > 0xFF);
    set_nz(val);
    regs.a = static_cast<uint8_t>(val);
  }
  inline auto op_asl() -> void {
    uint16_t val = read_byte(addr) << 1;
    flagc = (val > 0xFF);
    set_nz(val);
    write_byte(addr, static_cast<uint8_t>(val));
  }
  inline auto op_lsra() -> void {
    flagc = (regs.a & 1);
    flagn = 0;
    regs.a >>= 1;
    set_z(regs.a);
  }
  inline auto op_lsr() -> void {
    uint16_t val = read_byte(addr);
    flagc = (val & 1);
    flagn = 0;
    val >>= 1;
    set_z(val);
    write_byte(addr, static_cast<uint8_t>(val));
  }
  inline auto op_rola() -> void {
    uint16_t val = (static_cast<uint16_t>(regs.a) << 1) | flagc;
    flagc = (val > 0xFF);
    regs.a = val & 0xFF;
    set_nz(regs.a);
  }
  inline auto op_rol() -> void {
    uint16_t val = (read_byte(addr) << 1) | flagc;
    flagc = (val > 0xFF);
    set_nz(val);
    write_byte(addr, static_cast<uint8_t>(val));
  }
  inline auto op_rora() -> void {
    uint16_t val = (static_cast<uint16_t>(regs.a) >> 1) | (flagc ? 0x80 : 0);
    flagc = (regs.a & 1);
    regs.a = val & 0xFF;
    set_nz(regs.a);
  }
  inline auto op_ror() -> void {
    uint16_t temp = read_byte(addr);
    uint16_t val = (temp >> 1) | (flagc ? 0x80 : 0);
    flagc = (temp & 1);
    set_nz(val);
    write_byte(addr, static_cast<uint8_t>(val));
  }
  inline auto op_ina() -> void {
    ++regs.a;
    set_nz(regs.a);
  }
  inline auto op_dea() -> void {
    --regs.a;
    set_nz(regs.a);
  }
  inline auto op_inc() -> void {
    uint16_t val = read_byte(addr) + 1;
    set_nz(val);
    write_byte(addr, static_cast<uint8_t>(val));
  }
  inline auto op_dec() -> void {
    uint16_t val = read_byte(addr) - 1;
    set_nz(val);
    write_byte(addr, static_cast<uint8_t>(val));
  }
  inline auto op_inx() -> void {
    ++regs.x;
    set_nz(regs.x);
  }
  inline auto op_dex() -> void {
    --regs.x;
    set_nz(regs.x);
  }
  inline auto op_iny() -> void {
    ++regs.y;
    set_nz(regs.y);
  }
  inline auto op_dey() -> void {
    --regs.y;
    set_nz(regs.y);
  }
  inline auto op_jmp() -> void { regs.pc = addr; }
  inline auto op_jsr() -> void {
    --regs.pc;
    push(regs.pc >> 8);
    push(regs.pc & 0xFF);
    regs.pc = addr;
  }
  inline auto op_rts() -> void {
    regs.pc = pop();
    regs.pc |= (static_cast<uint16_t>(pop()) << 8);
    ++regs.pc;
  }
  inline auto op_rti() -> void {
    regs.ps = pop() | AF_RESERVED | AF_BREAK;
    unpack_ps();
    regs.pc = pop();
    regs.pc |= (static_cast<uint16_t>(pop()) << 8);
  }
  template <bool cmos>
  inline auto op_brk() -> void {
    regs.pc++;
    push(regs.pc >> 8);
    push(regs.pc & 0xFF);
    pack_ps();
    push(regs.ps);
    regs.ps |= AF_INTERRUPT;
    if (cmos) {
      regs.ps &= ~AF_DECIMAL;
    }
    regs.pc = read_u16_unaligned(mem + 0xFFFE);
  }
  inline auto op_hlt() -> void {
    regs.is_jammed = 1;
    --regs.pc;
  }
  inline auto op_pha() -> void { push(regs.a); }
  inline auto op_php() -> void {
    pack_ps();
    push(regs.ps);
  }
  inline auto op_phx() -> void { push(regs.x); }
  inline auto op_phy() -> void { push(regs.y); }
  inline auto op_pla() -> void {
    regs.a = pop();
    set_nz(regs.a);
  }
  inline auto op_plp() -> void {
    regs.ps = pop() | AF_RESERVED | AF_BREAK;
    unpack_ps();
  }
  inline auto op_plx() -> void {
    regs.x = pop();
    set_nz(regs.x);
  }
  inline auto op_ply() -> void {
    regs.y = pop();
    set_nz(regs.y);
  }
  inline auto op_trb() -> void {
    uint16_t val = read_byte(addr);
    flagz = !(regs.a & val);
    val &= ~regs.a;
    write_byte(addr, static_cast<uint8_t>(val));
  }
  inline auto op_tsb() -> void {
    uint16_t val = read_byte(addr);
    flagz = !(regs.a & val);
    val |= regs.a;
    write_byte(addr, static_cast<uint8_t>(val));
  }

  // Arithmetic ADC / SBC
  inline auto op_adc_nmos() -> void {
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
  }

  inline auto op_adc_cmos() -> void {
    uint16_t temp = read_byte(addr);
    flagv = !((regs.a ^ temp) & 0x80);
    uint16_t val = 0;
    if (regs.ps & AF_DECIMAL) {
      extra_cycles++;
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
  }

  inline auto op_sbc_nmos() -> void {
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
  }

  inline auto op_sbc_cmos() -> void {
    uint16_t temp = read_byte(addr);
    flagv = ((regs.a ^ temp) & 0x80);
    uint16_t val = 0;
    if (regs.ps & AF_DECIMAL) {
      extra_cycles++;
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
  }

  // Unofficial NMOS opcodes
  inline auto op_alr() -> void {
    regs.a &= read_byte(addr);
    flagc = (regs.a & 1);
    flagn = 0;
    regs.a >>= 1;
    set_z(regs.a);
  }
  inline auto op_anc() -> void {
    regs.a &= read_byte(addr);
    set_nz(regs.a);
    flagc = !!flagn;
  }
  inline auto op_arr() -> void {
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
  }
  inline auto op_aso() -> void {
    uint16_t val = read_byte(addr) << 1;
    flagc = (val > 0xFF);
    write_byte(addr, static_cast<uint8_t>(val));
    regs.a |= val;
    set_nz(regs.a);
  }
  inline auto op_axa() -> void {
    uint16_t val = regs.a & regs.x & (((base >> 8) + 1) & 0xFF);
    addr = (addr & 0x00FF) | (static_cast<uint16_t>(val) << 8);
    write_byte(addr, static_cast<uint8_t>(val));
  }
  inline auto op_axs() -> void { write_byte(addr, regs.a & regs.x); }
  inline auto op_dcm() -> void {
    uint16_t val = read_byte(addr) - 1;
    write_byte(addr, static_cast<uint8_t>(val));
    flagc = (regs.a >= val);
    val = regs.a - val;
    set_nz(val);
  }
  inline auto op_ins() -> void {
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
  }
  inline auto op_las() -> void {
    uint16_t val = static_cast<uint8_t>(read_byte(addr) & regs.sp);
    regs.a = regs.x = static_cast<uint8_t>(val);
    regs.sp = val | 0x100;
    set_nz(val);
  }
  inline auto op_lax() -> void {
    regs.a = regs.x = read_byte(addr);
    set_nz(regs.a);
  }
  inline auto op_lse() -> void {
    uint16_t val = read_byte(addr);
    flagc = (val & 1);
    val >>= 1;
    write_byte(addr, static_cast<uint8_t>(val));
    regs.a ^= val;
    set_nz(regs.a);
  }
  inline auto op_oal() -> void {
    regs.a |= 0xEE;
    regs.a &= read_byte(addr);
    regs.x = regs.a;
    set_nz(regs.a);
  }
  inline auto op_rla() -> void {
    uint16_t val = (read_byte(addr) << 1) | flagc;
    flagc = (val > 0xFF);
    write_byte(addr, static_cast<uint8_t>(val));
    regs.a &= val;
    set_nz(regs.a);
  }
  inline auto op_rra() -> void {
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
  }
  inline auto op_sax() -> void {
    uint16_t temp = regs.a & regs.x;
    uint16_t val = read_byte(addr);
    flagc = (temp >= val);
    regs.x = temp - val;
    set_nz(regs.x);
  }
  inline auto op_say() -> void {
    uint16_t val = regs.y & (((base >> 8) + 1) & 0xFF);
    addr = (addr & 0x00FF) | (static_cast<uint16_t>(val) << 8);
    write_byte(addr, static_cast<uint8_t>(val));
  }
  inline auto op_tas() -> void {
    uint16_t val = regs.a & regs.x;
    regs.sp = 0x100 | val;
    val &= (((base >> 8) + 1) & 0xFF);
    addr = (addr & 0x00FF) | (static_cast<uint16_t>(val) << 8);
    write_byte(addr, static_cast<uint8_t>(val));
  }
  inline auto op_xaa() -> void {
    regs.a = regs.x;
    regs.a &= read_byte(addr);
    set_nz(regs.a);
  }
  inline auto op_xas() -> void {
    uint16_t val = regs.x & (((base >> 8) + 1) & 0xFF);
    addr = (addr & 0x00FF) | (static_cast<uint16_t>(val) << 8);
    write_byte(addr, static_cast<uint8_t>(val));
  }

  inline auto op_clc() -> void { flagc = 0; }
  inline auto op_sec() -> void { flagc = 1; }
  inline auto op_clv() -> void { flagv = 0; }
  inline auto op_cli() -> void { regs.ps &= ~AF_INTERRUPT; }
  inline auto op_sei() -> void { regs.ps |= AF_INTERRUPT; }
  inline auto op_cld() -> void { regs.ps &= ~AF_DECIMAL; }
  inline auto op_sed() -> void { regs.ps |= AF_DECIMAL; }

  template <bool cmos>
  inline auto check_nmi() -> void {
#ifdef ENABLE_NMI_SUPPORT
    if (g_nmi_flank) {
      g_nmi_flank = false;
      g_cycle_irq_start = g_cumulative_cycles + executed_cycles;
      push(regs.pc >> 8);
      push(regs.pc & 0xFF);
      pack_ps();
      push(regs.ps & ~AF_BREAK);
      regs.ps |= AF_INTERRUPT;
      if (cmos) {
        regs.ps &= ~AF_DECIMAL;
      }
      regs.pc = read_u16_unaligned(mem + NMI_VECTOR_ADDR);
      executed_cycles += 7;
    }
#endif
  }

  template <bool cmos>
  inline auto check_irq() -> void {
    if (g_bm_irq && !(regs.ps & AF_INTERRUPT)) {
      g_cycle_irq_start = g_cumulative_cycles + executed_cycles;
      push(regs.pc >> 8);
      push(regs.pc & 0xFF);
      pack_ps();
      push(regs.ps & ~AF_BREAK);
      regs.ps |= AF_INTERRUPT;
      if (cmos) {
        regs.ps &= ~AF_DECIMAL;
      }
      regs.pc = read_u16_unaligned(mem + IRQ_VECTOR_ADDR);
      executed_cycles += 7;
    }
  }
};

static auto op_nop(CpuLoopContext_t&) -> void {}

static const OpcodeDesc_t s_opcodes_nmos[256] = {
    /* 0x00 */ {[](CpuLoopContext_t& c) { c.op_brk<false>(); }, 7},  // BRK
                                                                     /* 0x01 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_ora();
     },
     6},                                                      // ORA
    /* 0x02 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0x03 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_aso();
     },
     8},                                                        // aso
    /* 0x04 */ {[](CpuLoopContext_t& c) { c.addr_zpg(); }, 3},  // nop
                                                                /* 0x05 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_ora();
     },
     3},  // ORA
          /* 0x06 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_asl();
     },
     5},  // ASL
          /* 0x07 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_aso();
     },
     5},                                                      // aso
    /* 0x08 */ {[](CpuLoopContext_t& c) { c.op_php(); }, 3},  // PHP
                                                              /* 0x09 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_ora();
     },
     2},                                                       // ORA
    /* 0x0A */ {[](CpuLoopContext_t& c) { c.op_asla(); }, 2},  // ASL
                                                               /* 0x0B */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_anc();
     },
     2},                                                         // anc
    /* 0x0C */ {[](CpuLoopContext_t& c) { c.addr_absx(); }, 4},  // nop
                                                                 /* 0x0D */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_ora();
     },
     4},  // ORA
          /* 0x0E */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_asl();
     },
     6},  // ASL
          /* 0x0F */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_aso();
     },
     6},  // aso
          /* 0x10 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (!c.flagn) c.branch_taken();
     },
     2},  // BPL
          /* 0x11 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_ora();
     },
     5},                                                      // ORA
    /* 0x12 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0x13 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_aso();
     },
     8},                                                         // aso
    /* 0x14 */ {[](CpuLoopContext_t& c) { c.addr_zpgx(); }, 4},  // nop
                                                                 /* 0x15 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_ora();
     },
     4},  // ORA
          /* 0x16 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_asl();
     },
     6},  // ASL
          /* 0x17 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_aso();
     },
     6},                                                      // aso
    /* 0x18 */ {[](CpuLoopContext_t& c) { c.op_clc(); }, 2},  // CLC
                                                              /* 0x19 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_ora();
     },
     4},                     // ORA
    /* 0x1A */ {op_nop, 2},  // nop
                             /* 0x1B */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_aso();
     },
     7},                                                         // aso
    /* 0x1C */ {[](CpuLoopContext_t& c) { c.addr_absx(); }, 4},  // nop
                                                                 /* 0x1D */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_ora();
     },
     4},  // ORA
          /* 0x1E */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_asl();
     },
     6},  // ASL
          /* 0x1F */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_aso();
     },
     7},  // aso
          /* 0x20 */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_jsr();
     },
     6},  // JSR
          /* 0x21 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_and();
     },
     6},                                                      // AND
    /* 0x22 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0x23 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_rla();
     },
     8},  // rla
          /* 0x24 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_bit();
     },
     3},  // BIT
          /* 0x25 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_and();
     },
     3},  // AND
          /* 0x26 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_rol();
     },
     5},  // ROL
          /* 0x27 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_rla();
     },
     5},                                                      // rla
    /* 0x28 */ {[](CpuLoopContext_t& c) { c.op_plp(); }, 4},  // PLP
                                                              /* 0x29 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_and();
     },
     2},                                                       // AND
    /* 0x2A */ {[](CpuLoopContext_t& c) { c.op_rola(); }, 2},  // ROL
                                                               /* 0x2B */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_anc();
     },
     2},  // anc
          /* 0x2C */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_bit();
     },
     4},  // BIT
          /* 0x2D */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_and();
     },
     2},  // AND
          /* 0x2E */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_rol();
     },
     6},  // ROL
          /* 0x2F */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_rla();
     },
     6},  // rla
          /* 0x30 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (c.flagn) c.branch_taken();
     },
     2},  // BMI
          /* 0x31 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_and();
     },
     5},                                                      // AND
    /* 0x32 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0x33 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_rla();
     },
     8},                                                         // rla
    /* 0x34 */ {[](CpuLoopContext_t& c) { c.addr_zpgx(); }, 4},  // nop
                                                                 /* 0x35 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_and();
     },
     4},  // AND
          /* 0x36 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_rol();
     },
     6},  // ROL
          /* 0x37 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_rla();
     },
     6},                                                      // rla
    /* 0x38 */ {[](CpuLoopContext_t& c) { c.op_sec(); }, 2},  // SEC
                                                              /* 0x39 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_and();
     },
     4},                     // AND
    /* 0x3A */ {op_nop, 2},  // nop
                             /* 0x3B */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_rla();
     },
     7},                                                         // rla
    /* 0x3C */ {[](CpuLoopContext_t& c) { c.addr_absx(); }, 4},  // nop
                                                                 /* 0x3D */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_and();
     },
     4},  // AND
          /* 0x3E */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_rol();
     },
     6},  // ROL
          /* 0x3F */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_rla();
     },
     7},  // rla
          /* 0x40 */
    {[](CpuLoopContext_t& c) {
       c.op_rti();
       do_irq_profiling(c.executed_cycles);
     },
     6},  // RTI
          /* 0x41 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_eor();
     },
     6},                                                      // EOR
    /* 0x42 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0x43 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_lse();
     },
     8},                                                        // lse
    /* 0x44 */ {[](CpuLoopContext_t& c) { c.addr_zpg(); }, 3},  // nop
                                                                /* 0x45 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_eor();
     },
     3},  // EOR
          /* 0x46 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_lsr();
     },
     5},  // LSR
          /* 0x47 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_lse();
     },
     5},                                                      // lse
    /* 0x48 */ {[](CpuLoopContext_t& c) { c.op_pha(); }, 3},  // PHA
                                                              /* 0x49 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_eor();
     },
     2},                                                       // EOR
    /* 0x4A */ {[](CpuLoopContext_t& c) { c.op_lsra(); }, 2},  // LSR
                                                               /* 0x4B */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_alr();
     },
     2},  // alr
          /* 0x4C */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_jmp();
     },
     3},  // JMP
          /* 0x4D */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_eor();
     },
     4},  // EOR
          /* 0x4E */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_lsr();
     },
     6},  // LSR
          /* 0x4F */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_lse();
     },
     6},  // lse
          /* 0x50 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (!c.flagv) c.branch_taken();
     },
     2},  // BVC
          /* 0x51 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_eor();
     },
     5},                                                      // EOR
    /* 0x52 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0x53 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_lse();
     },
     8},                                                         // lse
    /* 0x54 */ {[](CpuLoopContext_t& c) { c.addr_zpgx(); }, 4},  // nop
                                                                 /* 0x55 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_eor();
     },
     4},  // EOR
          /* 0x56 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_lsr();
     },
     6},  // LSR
          /* 0x57 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_lse();
     },
     6},                                                      // lse
    /* 0x58 */ {[](CpuLoopContext_t& c) { c.op_cli(); }, 2},  // CLI
                                                              /* 0x59 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_eor();
     },
     4},                     // EOR
    /* 0x5A */ {op_nop, 2},  // nop
                             /* 0x5B */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_lse();
     },
     7},                                                         // lse
    /* 0x5C */ {[](CpuLoopContext_t& c) { c.addr_absx(); }, 4},  // nop
                                                                 /* 0x5D */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_eor();
     },
     4},  // EOR
          /* 0x5E */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_lsr();
     },
     6},  // LSR
          /* 0x5F */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_lse();
     },
     7},                                                      // lse
    /* 0x60 */ {[](CpuLoopContext_t& c) { c.op_rts(); }, 6},  // RTS
                                                              /* 0x61 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_adc_nmos();
     },
     6},                                                      // ADC
    /* 0x62 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0x63 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_rra();
     },
     8},                                                        // rra
    /* 0x64 */ {[](CpuLoopContext_t& c) { c.addr_zpg(); }, 3},  // nop
                                                                /* 0x65 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_adc_nmos();
     },
     3},  // ADC
          /* 0x66 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_ror();
     },
     5},  // ROR
          /* 0x67 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_rra();
     },
     5},                                                      // rra
    /* 0x68 */ {[](CpuLoopContext_t& c) { c.op_pla(); }, 4},  // PLA
                                                              /* 0x69 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_adc_nmos();
     },
     2},                                                       // ADC
    /* 0x6A */ {[](CpuLoopContext_t& c) { c.op_rora(); }, 2},  // ROR
                                                               /* 0x6B */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_arr();
     },
     2},  // arr
          /* 0x6C */
    {[](CpuLoopContext_t& c) {
       c.addr_iabs_nmos();
       c.op_jmp();
     },
     6},  // JMP
          /* 0x6D */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_adc_nmos();
     },
     4},  // ADC
          /* 0x6E */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_ror();
     },
     6},  // ROR
          /* 0x6F */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_rra();
     },
     6},  // rra
          /* 0x70 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (c.flagv) c.branch_taken();
     },
     2},  // BVS
          /* 0x71 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_adc_nmos();
     },
     5},                                                      // ADC
    /* 0x72 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0x73 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_rra();
     },
     8},                                                         // rra
    /* 0x74 */ {[](CpuLoopContext_t& c) { c.addr_zpgx(); }, 4},  // nop
                                                                 /* 0x75 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_adc_nmos();
     },
     4},  // ADC
          /* 0x76 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_ror();
     },
     6},  // ROR
          /* 0x77 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_rra();
     },
     6},                                                      // rra
    /* 0x78 */ {[](CpuLoopContext_t& c) { c.op_sei(); }, 2},  // SEI
                                                              /* 0x79 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_adc_nmos();
     },
     4},                     // ADC
    /* 0x7A */ {op_nop, 2},  // nop
                             /* 0x7B */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_rra();
     },
     7},                                                         // rra
    /* 0x7C */ {[](CpuLoopContext_t& c) { c.addr_absx(); }, 4},  // nop
                                                                 /* 0x7D */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_adc_nmos();
     },
     4},  // ADC
          /* 0x7E */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_ror();
     },
     6},  // ROR
          /* 0x7F */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_rra();
     },
     7},                                                        // rra
    /* 0x80 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
                                                                /* 0x81 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_sta();
     },
     6},                                                        // STA
    /* 0x82 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
                                                                /* 0x83 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_axs();
     },
     6},  // axs
          /* 0x84 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_sty();
     },
     3},  // STY
          /* 0x85 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_sta();
     },
     3},  // STA
          /* 0x86 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_stx();
     },
     3},  // STX
          /* 0x87 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_axs();
     },
     3},                                                        // axs
    /* 0x88 */ {[](CpuLoopContext_t& c) { c.op_dey(); }, 2},    // DEY
    /* 0x89 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
    /* 0x8A */ {[](CpuLoopContext_t& c) { c.op_txa(); }, 2},    // TXA
                                                                /* 0x8B */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_xaa();
     },
     2},  // xaa
          /* 0x8C */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_sty();
     },
     4},  // STY
          /* 0x8D */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_sta();
     },
     4},  // STA
          /* 0x8E */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_stx();
     },
     4},  // STX
          /* 0x8F */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_axs();
     },
     4},  // axs
          /* 0x90 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (!c.flagc) c.branch_taken();
     },
     2},  // BCC
          /* 0x91 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_sta();
     },
     6},                                                      // STA
    /* 0x92 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0x93 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_axa();
     },
     6},  // axa
          /* 0x94 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_sty();
     },
     4},  // STY
          /* 0x95 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_sta();
     },
     4},  // STA
          /* 0x96 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgy();
       c.op_stx();
     },
     4},  // STX
          /* 0x97 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgy();
       c.op_axs();
     },
     4},                                                      // axs
    /* 0x98 */ {[](CpuLoopContext_t& c) { c.op_tya(); }, 2},  // TYA
                                                              /* 0x99 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_sta();
     },
     5},                                                      // STA
    /* 0x9A */ {[](CpuLoopContext_t& c) { c.op_txs(); }, 2},  // TXS
                                                              /* 0x9B */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_tas();
     },
     5},  // tas
          /* 0x9C */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_say();
     },
     5},  // say
          /* 0x9D */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_sta();
     },
     5},  // STA
          /* 0x9E */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_xas();
     },
     5},  // xas
          /* 0x9F */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_axa();
     },
     5},  // axa
          /* 0xA0 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_ldy();
     },
     2},  // LDY
          /* 0xA1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_lda();
     },
     6},  // LDA
          /* 0xA2 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_ldx();
     },
     2},  // LDX
          /* 0xA3 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_lax();
     },
     6},  // lax
          /* 0xA4 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_ldy();
     },
     3},  // LDY
          /* 0xA5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_lda();
     },
     3},  // LDA
          /* 0xA6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_ldx();
     },
     3},  // LDX
          /* 0xA7 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_lax();
     },
     3},                                                      // lax
    /* 0xA8 */ {[](CpuLoopContext_t& c) { c.op_tay(); }, 2},  // TAY
                                                              /* 0xA9 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_lda();
     },
     2},                                                      // LDA
    /* 0xAA */ {[](CpuLoopContext_t& c) { c.op_tax(); }, 2},  // TAX
                                                              /* 0xAB */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_oal();
     },
     2},  // oal
          /* 0xAC */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_ldy();
     },
     4},  // LDY
          /* 0xAD */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_lda();
     },
     4},  // LDA
          /* 0xAE */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_ldx();
     },
     4},  // LDX
          /* 0xAF */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_lax();
     },
     4},  // lax
          /* 0xB0 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (c.flagc) c.branch_taken();
     },
     2},  // BCS
          /* 0xB1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_lda();
     },
     5},                                                      // LDA
    /* 0xB2 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0xB3 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_lax();
     },
     5},  // lax
          /* 0xB4 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_ldy();
     },
     4},  // LDY
          /* 0xB5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_lda();
     },
     4},  // LDA
          /* 0xB6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgy();
       c.op_ldx();
     },
     4},  // LDX
          /* 0xB7 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgy();
       c.op_lax();
     },
     4},                                                      // lax
    /* 0xB8 */ {[](CpuLoopContext_t& c) { c.op_clv(); }, 2},  // CLV
                                                              /* 0xB9 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_lda();
     },
     4},                                                      // LDA
    /* 0xBA */ {[](CpuLoopContext_t& c) { c.op_tsx(); }, 2},  // TSX
                                                              /* 0xBB */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_las();
     },
     4},  // las
          /* 0xBC */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_ldy();
     },
     4},  // LDY
          /* 0xBD */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_lda();
     },
     4},  // LDA
          /* 0xBE */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_ldx();
     },
     4},  // LDX
          /* 0xBF */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_lax();
     },
     4},  // lax
          /* 0xC0 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_cpy();
     },
     2},  // CPY
          /* 0xC1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_cmp();
     },
     6},                                                        // CMP
    /* 0xC2 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
                                                                /* 0xC3 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_dcm();
     },
     8},  // dcm
          /* 0xC4 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_cpy();
     },
     3},  // CPY
          /* 0xC5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_cmp();
     },
     3},  // CMP
          /* 0xC6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_dec();
     },
     5},  // DEC
          /* 0xC7 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_dcm();
     },
     5},                                                      // dcm
    /* 0xC8 */ {[](CpuLoopContext_t& c) { c.op_iny(); }, 2},  // INY
                                                              /* 0xC9 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_cmp();
     },
     2},                                                      // CMP
    /* 0xCA */ {[](CpuLoopContext_t& c) { c.op_dex(); }, 2},  // DEX
                                                              /* 0xCB */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_sax();
     },
     2},  // sax
          /* 0xCC */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_cpy();
     },
     4},  // CPY
          /* 0xCD */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_cmp();
     },
     4},  // CMP
          /* 0xCE */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_dec();
     },
     5},  // DEC
          /* 0xCF */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_dcm();
     },
     6},  // dcm
          /* 0xD0 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (!c.flagz) c.branch_taken();
     },
     2},  // BNE
          /* 0xD1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_cmp();
     },
     5},                                                      // CMP
    /* 0xD2 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0xD3 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_dcm();
     },
     8},                                                         // dcm
    /* 0xD4 */ {[](CpuLoopContext_t& c) { c.addr_zpgx(); }, 4},  // nop
                                                                 /* 0xD5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_cmp();
     },
     4},  // CMP
          /* 0xD6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_dec();
     },
     6},  // DEC
          /* 0xD7 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_dcm();
     },
     6},                                                      // dcm
    /* 0xD8 */ {[](CpuLoopContext_t& c) { c.op_cld(); }, 2},  // CLD
                                                              /* 0xD9 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_cmp();
     },
     4},                     // CMP
    /* 0xDA */ {op_nop, 2},  // nop
                             /* 0xDB */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_dcm();
     },
     7},                                                         // dcm
    /* 0xDC */ {[](CpuLoopContext_t& c) { c.addr_absx(); }, 4},  // nop
                                                                 /* 0xDD */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_cmp();
     },
     4},  // CMP
          /* 0xDE */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_dec();
     },
     6},  // DEC
          /* 0xDF */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_dcm();
     },
     7},  // dcm
          /* 0xE0 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_cpx();
     },
     2},  // CPX
          /* 0xE1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_sbc_nmos();
     },
     6},                                                        // SBC
    /* 0xE2 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
                                                                /* 0xE3 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_ins();
     },
     8},  // ins
          /* 0xE4 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_cpx();
     },
     3},  // CPX
          /* 0xE5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_sbc_nmos();
     },
     3},  // SBC
          /* 0xE6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_inc();
     },
     5},  // INC
          /* 0xE7 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_ins();
     },
     5},                                                      // ins
    /* 0xE8 */ {[](CpuLoopContext_t& c) { c.op_inx(); }, 2},  // INX
                                                              /* 0xE9 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_sbc_nmos();
     },
     2},                     // SBC
    /* 0xEA */ {op_nop, 2},  // NOP
                             /* 0xEB */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_sbc_nmos();
     },
     2},  // sbc
          /* 0xEC */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_cpx();
     },
     4},  // CPX
          /* 0xED */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_sbc_nmos();
     },
     4},  // SBC
          /* 0xEE */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_inc();
     },
     6},  // INC
          /* 0xEF */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_ins();
     },
     6},  // ins
          /* 0xF0 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (c.flagz) c.branch_taken();
     },
     2},  // BEQ
          /* 0xF1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_sbc_nmos();
     },
     5},                                                      // SBC
    /* 0xF2 */ {[](CpuLoopContext_t& c) { c.op_hlt(); }, 2},  // hlt
                                                              /* 0xF3 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_ins();
     },
     8},                                                         // ins
    /* 0xF4 */ {[](CpuLoopContext_t& c) { c.addr_zpgx(); }, 4},  // nop
                                                                 /* 0xF5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_sbc_nmos();
     },
     4},  // SBC
          /* 0xF6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_inc();
     },
     6},  // INC
          /* 0xF7 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_ins();
     },
     6},                                                      // ins
    /* 0xF8 */ {[](CpuLoopContext_t& c) { c.op_sed(); }, 2},  // SED
                                                              /* 0xF9 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_sbc_nmos();
     },
     4},                     // SBC
    /* 0xFA */ {op_nop, 2},  // nop
                             /* 0xFB */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_ins();
     },
     7},                                                         // ins
    /* 0xFC */ {[](CpuLoopContext_t& c) { c.addr_absx(); }, 4},  // nop
                                                                 /* 0xFD */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_sbc_nmos();
     },
     4},  // SBC
          /* 0xFE */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_inc();
     },
     6},  // INC
          /* 0xFF */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_ins();
     },
     7},  // ins
};

static const OpcodeDesc_t s_opcodes_cmos[256] = {
    /* 0x00 */ {[](CpuLoopContext_t& c) { c.op_brk<true>(); }, 7},  // BRK
                                                                    /* 0x01 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_ora();
     },
     6},                                                        // ORA
    /* 0x02 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
    /* 0x03 */ {op_nop, 2},                                     // nop
                                                                /* 0x04 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_tsb();
     },
     5},  // TSB
          /* 0x05 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_ora();
     },
     3},  // ORA
          /* 0x06 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_asl();
     },
     5},                                                      // ASL
    /* 0x07 */ {op_nop, 2},                                   // nop
    /* 0x08 */ {[](CpuLoopContext_t& c) { c.op_php(); }, 3},  // PHP
                                                              /* 0x09 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_ora();
     },
     2},                                                       // ORA
    /* 0x0A */ {[](CpuLoopContext_t& c) { c.op_asla(); }, 2},  // ASL
    /* 0x0B */ {op_nop, 2},                                    // nop
                                                               /* 0x0C */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_tsb();
     },
     6},  // TSB
          /* 0x0D */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_ora();
     },
     4},  // ORA
          /* 0x0E */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_asl();
     },
     6},                     // ASL
    /* 0x0F */ {op_nop, 2},  // nop
                             /* 0x10 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (!c.flagn) c.branch_taken();
     },
     2},  // BPL
          /* 0x11 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_ora();
     },
     5},  // ORA
          /* 0x12 */
    {[](CpuLoopContext_t& c) {
       c.addr_izpg();
       c.op_ora();
     },
     5},                     // ORA
    /* 0x13 */ {op_nop, 2},  // nop
                             /* 0x14 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_trb();
     },
     5},  // TRB
          /* 0x15 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_ora();
     },
     4},  // ORA
          /* 0x16 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_asl();
     },
     6},                                                      // ASL
    /* 0x17 */ {op_nop, 2},                                   // nop
    /* 0x18 */ {[](CpuLoopContext_t& c) { c.op_clc(); }, 2},  // CLC
                                                              /* 0x19 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_ora();
     },
     4},                                                      // ORA
    /* 0x1A */ {[](CpuLoopContext_t& c) { c.op_ina(); }, 2},  // INC
    /* 0x1B */ {op_nop, 2},                                   // nop
                                                              /* 0x1C */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_trb();
     },
     6},  // TRB
          /* 0x1D */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_ora();
     },
     4},  // ORA
          /* 0x1E */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_asl();
     },
     6},                     // ASL
    /* 0x1F */ {op_nop, 2},  // nop
                             /* 0x20 */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_jsr();
     },
     6},  // JSR
          /* 0x21 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_and();
     },
     6},                                                        // AND
    /* 0x22 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
    /* 0x23 */ {op_nop, 2},                                     // nop
                                                                /* 0x24 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_bit();
     },
     3},  // BIT
          /* 0x25 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_and();
     },
     3},  // AND
          /* 0x26 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_rol();
     },
     5},                                                      // ROL
    /* 0x27 */ {op_nop, 2},                                   // nop
    /* 0x28 */ {[](CpuLoopContext_t& c) { c.op_plp(); }, 4},  // PLP
                                                              /* 0x29 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_and();
     },
     2},                                                       // AND
    /* 0x2A */ {[](CpuLoopContext_t& c) { c.op_rola(); }, 2},  // ROL
    /* 0x2B */ {op_nop, 2},                                    // nop
                                                               /* 0x2C */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_bit();
     },
     4},  // BIT
          /* 0x2D */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_and();
     },
     2},  // AND
          /* 0x2E */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_rol();
     },
     6},                     // ROL
    /* 0x2F */ {op_nop, 2},  // nop
                             /* 0x30 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (c.flagn) c.branch_taken();
     },
     2},  // BMI
          /* 0x31 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_and();
     },
     5},  // AND
          /* 0x32 */
    {[](CpuLoopContext_t& c) {
       c.addr_izpg();
       c.op_and();
     },
     5},                     // AND
    /* 0x33 */ {op_nop, 2},  // nop
                             /* 0x34 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_bit();
     },
     4},  // BIT
          /* 0x35 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_and();
     },
     4},  // AND
          /* 0x36 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_rol();
     },
     6},                                                      // ROL
    /* 0x37 */ {op_nop, 2},                                   // nop
    /* 0x38 */ {[](CpuLoopContext_t& c) { c.op_sec(); }, 2},  // SEC
                                                              /* 0x39 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_and();
     },
     4},                                                      // AND
    /* 0x3A */ {[](CpuLoopContext_t& c) { c.op_dea(); }, 2},  // DEC
    /* 0x3B */ {op_nop, 2},                                   // nop
                                                              /* 0x3C */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_bit();
     },
     4},  // BIT
          /* 0x3D */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_and();
     },
     4},  // AND
          /* 0x3E */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_rol();
     },
     6},                     // ROL
    /* 0x3F */ {op_nop, 2},  // nop
                             /* 0x40 */
    {[](CpuLoopContext_t& c) {
       c.op_rti();
       do_irq_profiling(c.executed_cycles);
     },
     6},  // RTI
          /* 0x41 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_eor();
     },
     6},                                                        // EOR
    /* 0x42 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
    /* 0x43 */ {op_nop, 2},                                     // nop
    /* 0x44 */ {[](CpuLoopContext_t& c) { c.addr_zpg(); }, 3},  // nop
                                                                /* 0x45 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_eor();
     },
     3},  // EOR
          /* 0x46 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_lsr();
     },
     5},                                                      // LSR
    /* 0x47 */ {op_nop, 2},                                   // nop
    /* 0x48 */ {[](CpuLoopContext_t& c) { c.op_pha(); }, 3},  // PHA
                                                              /* 0x49 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_eor();
     },
     2},                                                       // EOR
    /* 0x4A */ {[](CpuLoopContext_t& c) { c.op_lsra(); }, 2},  // LSR
    /* 0x4B */ {op_nop, 2},                                    // nop
                                                               /* 0x4C */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_jmp();
     },
     3},  // JMP
          /* 0x4D */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_eor();
     },
     4},  // EOR
          /* 0x4E */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_lsr();
     },
     6},                     // LSR
    /* 0x4F */ {op_nop, 2},  // nop
                             /* 0x50 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (!c.flagv) c.branch_taken();
     },
     2},  // BVC
          /* 0x51 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_eor();
     },
     5},  // EOR
          /* 0x52 */
    {[](CpuLoopContext_t& c) {
       c.addr_izpg();
       c.op_eor();
     },
     5},                                                         // EOR
    /* 0x53 */ {op_nop, 2},                                      // nop
    /* 0x54 */ {[](CpuLoopContext_t& c) { c.addr_zpgx(); }, 4},  // nop
                                                                 /* 0x55 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_eor();
     },
     4},  // EOR
          /* 0x56 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_lsr();
     },
     6},                                                      // LSR
    /* 0x57 */ {op_nop, 2},                                   // nop
    /* 0x58 */ {[](CpuLoopContext_t& c) { c.op_cli(); }, 2},  // CLI
                                                              /* 0x59 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_eor();
     },
     4},                                                         // EOR
    /* 0x5A */ {[](CpuLoopContext_t& c) { c.op_phy(); }, 3},     // PHY
    /* 0x5B */ {op_nop, 2},                                      // nop
    /* 0x5C */ {[](CpuLoopContext_t& c) { c.addr_absx(); }, 8},  // nop
                                                                 /* 0x5D */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_eor();
     },
     4},  // EOR
          /* 0x5E */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_lsr();
     },
     6},                                                      // LSR
    /* 0x5F */ {op_nop, 2},                                   // nop
    /* 0x60 */ {[](CpuLoopContext_t& c) { c.op_rts(); }, 6},  // RTS
                                                              /* 0x61 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_adc_cmos();
     },
     6},                                                        // ADC
    /* 0x62 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
    /* 0x63 */ {op_nop, 2},                                     // nop
                                                                /* 0x64 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_stz();
     },
     3},  // STZ
          /* 0x65 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_adc_cmos();
     },
     3},  // ADC
          /* 0x66 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_ror();
     },
     5},                                                      // ROR
    /* 0x67 */ {op_nop, 2},                                   // nop
    /* 0x68 */ {[](CpuLoopContext_t& c) { c.op_pla(); }, 4},  // PLA
                                                              /* 0x69 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_adc_cmos();
     },
     2},                                                       // ADC
    /* 0x6A */ {[](CpuLoopContext_t& c) { c.op_rora(); }, 2},  // ROR
    /* 0x6B */ {op_nop, 2},                                    // nop
                                                               /* 0x6C */
    {[](CpuLoopContext_t& c) {
       c.addr_iabs_cmos();
       c.op_jmp();
     },
     6},  // JMP
          /* 0x6D */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_adc_cmos();
     },
     4},  // ADC
          /* 0x6E */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_ror();
     },
     6},                     // ROR
    /* 0x6F */ {op_nop, 2},  // nop
                             /* 0x70 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (c.flagv) c.branch_taken();
     },
     2},  // BVS
          /* 0x71 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_adc_cmos();
     },
     5},  // ADC
          /* 0x72 */
    {[](CpuLoopContext_t& c) {
       c.addr_izpg();
       c.op_adc_cmos();
     },
     5},                     // ADC
    /* 0x73 */ {op_nop, 2},  // nop
                             /* 0x74 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_stz();
     },
     4},  // STZ
          /* 0x75 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_adc_cmos();
     },
     4},  // ADC
          /* 0x76 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_ror();
     },
     6},                                                      // ROR
    /* 0x77 */ {op_nop, 2},                                   // nop
    /* 0x78 */ {[](CpuLoopContext_t& c) { c.op_sei(); }, 2},  // SEI
                                                              /* 0x79 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_adc_cmos();
     },
     4},                                                      // ADC
    /* 0x7A */ {[](CpuLoopContext_t& c) { c.op_ply(); }, 4},  // PLY
    /* 0x7B */ {op_nop, 2},                                   // nop
                                                              /* 0x7C */
    {[](CpuLoopContext_t& c) {
       c.addr_iabsx();
       c.op_jmp();
     },
     6},  // JMP
          /* 0x7D */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_adc_cmos();
     },
     4},  // ADC
          /* 0x7E */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_ror();
     },
     6},                     // ROR
    /* 0x7F */ {op_nop, 2},  // nop
                             /* 0x80 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       c.branch_taken();
     },
     2},  // BRA
          /* 0x81 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_sta();
     },
     6},                                                        // STA
    /* 0x82 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
    /* 0x83 */ {op_nop, 2},                                     // nop
                                                                /* 0x84 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_sty();
     },
     3},  // STY
          /* 0x85 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_sta();
     },
     3},  // STA
          /* 0x86 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_stx();
     },
     3},                                                      // STX
    /* 0x87 */ {op_nop, 2},                                   // nop
    /* 0x88 */ {[](CpuLoopContext_t& c) { c.op_dey(); }, 2},  // DEY
                                                              /* 0x89 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_biti();
     },
     2},                                                      // BIT
    /* 0x8A */ {[](CpuLoopContext_t& c) { c.op_txa(); }, 2},  // TXA
    /* 0x8B */ {op_nop, 2},                                   // nop
                                                              /* 0x8C */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_sty();
     },
     4},  // STY
          /* 0x8D */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_sta();
     },
     4},  // STA
          /* 0x8E */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_stx();
     },
     4},                     // STX
    /* 0x8F */ {op_nop, 2},  // nop
                             /* 0x90 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (!c.flagc) c.branch_taken();
     },
     2},  // BCC
          /* 0x91 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_sta();
     },
     6},  // STA
          /* 0x92 */
    {[](CpuLoopContext_t& c) {
       c.addr_izpg();
       c.op_sta();
     },
     5},                     // STA
    /* 0x93 */ {op_nop, 2},  // nop
                             /* 0x94 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_sty();
     },
     4},  // STY
          /* 0x95 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_sta();
     },
     4},  // STA
          /* 0x96 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgy();
       c.op_stx();
     },
     4},                                                      // STX
    /* 0x97 */ {op_nop, 2},                                   // nop
    /* 0x98 */ {[](CpuLoopContext_t& c) { c.op_tya(); }, 2},  // TYA
                                                              /* 0x99 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_sta();
     },
     5},                                                      // STA
    /* 0x9A */ {[](CpuLoopContext_t& c) { c.op_txs(); }, 2},  // TXS
    /* 0x9B */ {op_nop, 2},                                   // nop
                                                              /* 0x9C */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_stz();
     },
     4},  // STZ
          /* 0x9D */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_sta();
     },
     5},  // STA
          /* 0x9E */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_stz();
     },
     5},                     // STZ
    /* 0x9F */ {op_nop, 2},  // nop
                             /* 0xA0 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_ldy();
     },
     2},  // LDY
          /* 0xA1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_lda();
     },
     6},  // LDA
          /* 0xA2 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_ldx();
     },
     2},                     // LDX
    /* 0xA3 */ {op_nop, 2},  // nop
                             /* 0xA4 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_ldy();
     },
     3},  // LDY
          /* 0xA5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_lda();
     },
     3},  // LDA
          /* 0xA6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_ldx();
     },
     3},                                                      // LDX
    /* 0xA7 */ {op_nop, 2},                                   // nop
    /* 0xA8 */ {[](CpuLoopContext_t& c) { c.op_tay(); }, 2},  // TAY
                                                              /* 0xA9 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_lda();
     },
     2},                                                      // LDA
    /* 0xAA */ {[](CpuLoopContext_t& c) { c.op_tax(); }, 2},  // TAX
    /* 0xAB */ {op_nop, 2},                                   // nop
                                                              /* 0xAC */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_ldy();
     },
     4},  // LDY
          /* 0xAD */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_lda();
     },
     4},  // LDA
          /* 0xAE */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_ldx();
     },
     4},                     // LDX
    /* 0xAF */ {op_nop, 2},  // nop
                             /* 0xB0 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (c.flagc) c.branch_taken();
     },
     2},  // BCS
          /* 0xB1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_lda();
     },
     5},  // LDA
          /* 0xB2 */
    {[](CpuLoopContext_t& c) {
       c.addr_izpg();
       c.op_lda();
     },
     5},                     // LDA
    /* 0xB3 */ {op_nop, 2},  // nop
                             /* 0xB4 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_ldy();
     },
     4},  // LDY
          /* 0xB5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_lda();
     },
     4},  // LDA
          /* 0xB6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgy();
       c.op_ldx();
     },
     4},                                                      // LDX
    /* 0xB7 */ {op_nop, 2},                                   // nop
    /* 0xB8 */ {[](CpuLoopContext_t& c) { c.op_clv(); }, 2},  // CLV
                                                              /* 0xB9 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_lda();
     },
     4},                                                      // LDA
    /* 0xBA */ {[](CpuLoopContext_t& c) { c.op_tsx(); }, 2},  // TSX
    /* 0xBB */ {op_nop, 2},                                   // nop
                                                              /* 0xBC */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_ldy();
     },
     4},  // LDY
          /* 0xBD */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_lda();
     },
     4},  // LDA
          /* 0xBE */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_ldx();
     },
     4},                     // LDX
    /* 0xBF */ {op_nop, 2},  // nop
                             /* 0xC0 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_cpy();
     },
     2},  // CPY
          /* 0xC1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_cmp();
     },
     6},                                                        // CMP
    /* 0xC2 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
    /* 0xC3 */ {op_nop, 2},                                     // nop
                                                                /* 0xC4 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_cpy();
     },
     3},  // CPY
          /* 0xC5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_cmp();
     },
     3},  // CMP
          /* 0xC6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_dec();
     },
     5},                                                      // DEC
    /* 0xC7 */ {op_nop, 2},                                   // nop
    /* 0xC8 */ {[](CpuLoopContext_t& c) { c.op_iny(); }, 2},  // INY
                                                              /* 0xC9 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_cmp();
     },
     2},                                                      // CMP
    /* 0xCA */ {[](CpuLoopContext_t& c) { c.op_dex(); }, 2},  // DEX
    /* 0xCB */ {op_nop, 2},                                   // nop
                                                              /* 0xCC */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_cpy();
     },
     4},  // CPY
          /* 0xCD */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_cmp();
     },
     4},  // CMP
          /* 0xCE */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_dec();
     },
     5},                     // DEC
    /* 0xCF */ {op_nop, 2},  // nop
                             /* 0xD0 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (!c.flagz) c.branch_taken();
     },
     2},  // BNE
          /* 0xD1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_cmp();
     },
     5},  // CMP
          /* 0xD2 */
    {[](CpuLoopContext_t& c) {
       c.addr_izpg();
       c.op_cmp();
     },
     5},                                                         // CMP
    /* 0xD3 */ {op_nop, 2},                                      // nop
    /* 0xD4 */ {[](CpuLoopContext_t& c) { c.addr_zpgx(); }, 4},  // nop
                                                                 /* 0xD5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_cmp();
     },
     4},  // CMP
          /* 0xD6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_dec();
     },
     6},                                                      // DEC
    /* 0xD7 */ {op_nop, 2},                                   // nop
    /* 0xD8 */ {[](CpuLoopContext_t& c) { c.op_cld(); }, 2},  // CLD
                                                              /* 0xD9 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_cmp();
     },
     4},                                                         // CMP
    /* 0xDA */ {[](CpuLoopContext_t& c) { c.op_phx(); }, 3},     // PHX
    /* 0xDB */ {op_nop, 2},                                      // nop
    /* 0xDC */ {[](CpuLoopContext_t& c) { c.addr_absx(); }, 4},  // nop
                                                                 /* 0xDD */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_cmp();
     },
     4},  // CMP
          /* 0xDE */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_dec();
     },
     6},                     // DEC
    /* 0xDF */ {op_nop, 2},  // nop
                             /* 0xE0 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_cpx();
     },
     2},  // CPX
          /* 0xE1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indx();
       c.op_sbc_cmos();
     },
     6},                                                        // SBC
    /* 0xE2 */ {[](CpuLoopContext_t& c) { c.addr_imm(); }, 2},  // nop
    /* 0xE3 */ {op_nop, 2},                                     // nop
                                                                /* 0xE4 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_cpx();
     },
     3},  // CPX
          /* 0xE5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_sbc_cmos();
     },
     3},  // SBC
          /* 0xE6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpg();
       c.op_inc();
     },
     5},                                                      // INC
    /* 0xE7 */ {op_nop, 2},                                   // nop
    /* 0xE8 */ {[](CpuLoopContext_t& c) { c.op_inx(); }, 2},  // INX
                                                              /* 0xE9 */
    {[](CpuLoopContext_t& c) {
       c.addr_imm();
       c.op_sbc_cmos();
     },
     2},                     // SBC
    /* 0xEA */ {op_nop, 2},  // NOP
    /* 0xEB */ {op_nop, 2},  // nop
                             /* 0xEC */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_cpx();
     },
     4},  // CPX
          /* 0xED */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_sbc_cmos();
     },
     4},  // SBC
          /* 0xEE */
    {[](CpuLoopContext_t& c) {
       c.addr_abs();
       c.op_inc();
     },
     6},                     // INC
    /* 0xEF */ {op_nop, 2},  // nop
                             /* 0xF0 */
    {[](CpuLoopContext_t& c) {
       c.addr_rel();
       if (c.flagz) c.branch_taken();
     },
     2},  // BEQ
          /* 0xF1 */
    {[](CpuLoopContext_t& c) {
       c.addr_indy();
       c.op_sbc_cmos();
     },
     5},  // SBC
          /* 0xF2 */
    {[](CpuLoopContext_t& c) {
       c.addr_izpg();
       c.op_sbc_cmos();
     },
     5},                                                         // SBC
    /* 0xF3 */ {op_nop, 2},                                      // nop
    /* 0xF4 */ {[](CpuLoopContext_t& c) { c.addr_zpgx(); }, 4},  // nop
                                                                 /* 0xF5 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_sbc_cmos();
     },
     4},  // SBC
          /* 0xF6 */
    {[](CpuLoopContext_t& c) {
       c.addr_zpgx();
       c.op_inc();
     },
     6},                                                      // INC
    /* 0xF7 */ {op_nop, 2},                                   // nop
    /* 0xF8 */ {[](CpuLoopContext_t& c) { c.op_sed(); }, 2},  // SED
                                                              /* 0xF9 */
    {[](CpuLoopContext_t& c) {
       c.addr_absy();
       c.op_sbc_cmos();
     },
     4},                                                         // SBC
    /* 0xFA */ {[](CpuLoopContext_t& c) { c.op_plx(); }, 4},     // PLX
    /* 0xFB */ {op_nop, 2},                                      // nop
    /* 0xFC */ {[](CpuLoopContext_t& c) { c.addr_absx(); }, 4},  // nop
                                                                 /* 0xFD */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_sbc_cmos();
     },
     4},  // SBC
          /* 0xFE */
    {[](CpuLoopContext_t& c) {
       c.addr_absx();
       c.op_inc();
     },
     6},                     // INC
    /* 0xFF */ {op_nop, 2},  // nop
};

template <bool is_cmos>
static auto cpu_execute_loop(uint32_t total_cycles) -> uint32_t {
  CpuLoopContext_t ctx;
  ctx.unpack_ps();

  const auto& table = is_cmos ? s_opcodes_cmos : s_opcodes_nmos;

  do {
    uint8_t opcode = 0;
    fetch_opcode(opcode, ctx.executed_cycles);

    ctx.extra_cycles = 0;
    const auto& desc = table[opcode];
    desc.handler(ctx);
    ctx.executed_cycles += desc.base_cycles + ctx.extra_cycles;

    ctx.pack_ps();
    ctx.check_nmi<is_cmos>();
    ctx.check_irq<is_cmos>();
  } while (ctx.executed_cycles < total_cycles);

  return ctx.executed_cycles;
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

auto cpu_destroy() -> void {}

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
  const std::lock_guard<std::mutex> lock(g_interrupt_mutex);
  g_bm_irq = 0;
}

auto cpu_irq_assert(IrqSrc_t device) -> void {
  const std::lock_guard<std::mutex> lock(g_interrupt_mutex);
  g_bm_irq |= 1U << device;
}

auto cpu_irq_deassert(IrqSrc_t device) -> void {
  const std::lock_guard<std::mutex> lock(g_interrupt_mutex);
  g_bm_irq &= ~(1U << device);
}

auto cpu_nmi_reset() -> void {
  const std::lock_guard<std::mutex> lock(g_interrupt_mutex);
  g_bm_nmi = 0;
  g_nmi_flank = false;
}

auto cpu_nmi_assert(IrqSrc_t device) -> void {
  const std::lock_guard<std::mutex> lock(g_interrupt_mutex);
  if (g_bm_nmi == 0) {  // NMI line is just becoming active
    g_nmi_flank = true;
  }
  g_bm_nmi |= 1U << device;
}

auto cpu_nmi_deassert(IrqSrc_t device) -> void {
  const std::lock_guard<std::mutex> lock(g_interrupt_mutex);
  g_bm_nmi &= ~(1U << device);
}

auto cpu_reset() -> void {
  regs.ps = (regs.ps | AF_INTERRUPT) & ~AF_DECIMAL;
  if (mem != nullptr) {
    regs.pc = *reinterpret_cast<uint16_t*>(mem + 0xFFFC);
  } else {
    regs.pc = 0;
  }
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

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-pro-bounds-pointer-arithmetic, bugprone-easily-swappable-parameters, google-readability-function-size)

auto cpu_step() -> void { cpu_execute(0); }
