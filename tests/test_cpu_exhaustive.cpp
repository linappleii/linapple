// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <cstdint>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "doctest.h"

namespace {

constexpr uint8_t FLAG_C = 0x01;
constexpr uint8_t FLAG_Z = 0x02;
constexpr uint8_t FLAG_I = 0x04;
constexpr uint8_t FLAG_D = 0x08;
constexpr uint8_t FLAG_B = 0x10;
constexpr uint8_t FLAG_R = 0x20;
constexpr uint8_t FLAG_V = 0x40;
constexpr uint8_t FLAG_N = 0x80;

constexpr uint16_t CODE_BASE_ADDR = 0x0300;

struct CpuTestFixture_t {
  CpuTestFixture_t() {
    g_apple2_type = A2TYPE_APPLE2EENHANCED;
    mem_initialize();
    cpu_initialize();
  }

  ~CpuTestFixture_t() {
    cpu_destroy();
    mem_destroy();
  }

  CpuTestFixture_t(const CpuTestFixture_t&) = delete;
  auto operator=(const CpuTestFixture_t&) -> CpuTestFixture_t& = delete;
  CpuTestFixture_t(CpuTestFixture_t&&) = delete;
  auto operator=(CpuTestFixture_t&&) -> CpuTestFixture_t& = delete;
};

}  // namespace

TEST_CASE("Exhaustive: [CPU-EX-01] Binary Mode ADC Execution") {
  CpuTestFixture_t fixture;
  auto* regs = cpu_get_registers();

  // Helper lambda for named edge case assertions
  auto test_adc_named = [regs](uint8_t a_in, uint8_t operand, bool carry_in,
                               uint8_t expected_a, bool expected_v,
                               bool expected_c, bool expected_n,
                               bool expected_z) {
    mem[CODE_BASE_ADDR] = 0x69;  // ADC #imm
    mem[CODE_BASE_ADDR + 1] = operand;
    regs->pc = CODE_BASE_ADDR;
    regs->a = a_in;
    regs->ps = carry_in ? FLAG_C : 0;
    cpu_execute(0);

    CHECK(regs->a == expected_a);
    CHECK(((regs->ps & FLAG_V) != 0) == expected_v);
    CHECK(((regs->ps & FLAG_C) != 0) == expected_c);
    CHECK(((regs->ps & FLAG_N) != 0) == expected_n);
    CHECK(((regs->ps & FLAG_Z) != 0) == expected_z);
  };

  // Classic overflow edge cases:
  // Positive + Positive -> Negative (Signed Overflow): 0x7F (+127) + 0x01 (+1)
  // = 0x80 (-128)
  test_adc_named(0x7F, 0x01, false, 0x80, true, false, true, false);

  // Negative + Negative -> Positive (Signed Overflow): 0x80 (-128) + 0x80
  // (-128) = 0x00 (+ carry)
  test_adc_named(0x80, 0x80, false, 0x00, true, true, false, true);

  // Positive + Positive -> Negative (Signed Overflow): 0x50 (+80) + 0x50 (+80)
  // = 0xA0 (-96)
  test_adc_named(0x50, 0x50, false, 0xA0, true, false, true, false);

  // Negative + Negative -> Positive (Signed Overflow): 0xD0 (-48) + 0x90 (-112)
  // = 0x60 (+96, + carry)
  test_adc_named(0xD0, 0x90, false, 0x60, true, true, false, false);

  // No-overflow baseline checks:
  // Positive + Positive -> Positive: 0x50 (+80) + 0x10 (+16) = 0x60 (+96)
  test_adc_named(0x50, 0x10, false, 0x60, false, false, false, false);

  // Negative + Negative -> Negative: 0xF0 (-16) + 0xF0 (-16) = 0xE0 (-32, +
  // carry)
  test_adc_named(0xF0, 0xF0, false, 0xE0, false, true, true, false);

  // Exhaustive 131,072 truth table verification (carry in {0, 1}, A in 0..255,
  // operand in 0..255)
  mem[CODE_BASE_ADDR] = 0x69;  // ADC #imm
  uint32_t failures = 0;

  for (int carry_in = 0; carry_in < 2; ++carry_in) {
    uint8_t initial_ps = (carry_in != 0) ? FLAG_C : 0;
    for (int a_in = 0; a_in < 256; ++a_in) {
      for (int operand = 0; operand < 256; ++operand) {
        mem[CODE_BASE_ADDR + 1] = static_cast<uint8_t>(operand);
        regs->pc = CODE_BASE_ADDR;
        regs->a = static_cast<uint8_t>(a_in);
        regs->ps = initial_ps;

        cpu_execute(0);

        int sum = a_in + operand + carry_in;
        uint8_t expected_a = static_cast<uint8_t>(sum & 0xFF);
        bool expected_c = (sum > 0xFF);
        bool expected_v = (~(a_in ^ operand) & (a_in ^ expected_a) & 0x80) != 0;
        bool expected_z = (expected_a == 0);
        bool expected_n = (expected_a & 0x80) != 0;

        bool actual_c = (regs->ps & FLAG_C) != 0;
        bool actual_z = (regs->ps & FLAG_Z) != 0;
        bool actual_v = (regs->ps & FLAG_V) != 0;
        bool actual_n = (regs->ps & FLAG_N) != 0;

        if (regs->a != expected_a || actual_c != expected_c ||
            actual_z != expected_z || actual_v != expected_v ||
            actual_n != expected_n) {
          ++failures;
        }
      }
    }
  }

  CHECK(failures == 0);
}

TEST_CASE("Exhaustive: [CPU-EX-02] Binary Mode SBC Execution") {
  CpuTestFixture_t fixture;
  auto* regs = cpu_get_registers();

  // Helper lambda for named edge case assertions
  auto test_sbc_named = [regs](uint8_t a_in, uint8_t operand, bool carry_in,
                               uint8_t expected_a, bool expected_v,
                               bool expected_c, bool expected_n,
                               bool expected_z) {
    mem[CODE_BASE_ADDR] = 0xE9;  // SBC #imm
    mem[CODE_BASE_ADDR + 1] = operand;
    regs->pc = CODE_BASE_ADDR;
    regs->a = a_in;
    regs->ps = carry_in ? FLAG_C : 0;
    cpu_execute(0);

    CHECK(regs->a == expected_a);
    CHECK(((regs->ps & FLAG_V) != 0) == expected_v);
    CHECK(((regs->ps & FLAG_C) != 0) == expected_c);
    CHECK(((regs->ps & FLAG_N) != 0) == expected_n);
    CHECK(((regs->ps & FLAG_Z) != 0) == expected_z);
  };

  // Classic overflow edge cases:
  // Positive - Negative -> Negative (Signed Overflow): 0x7F (+127) - 0xFF (-1)
  // = 0x80 (-128) [carry_in=1, no borrow]
  test_sbc_named(0x7F, 0xFF, true, 0x80, true, false, true, false);

  // Negative - Positive -> Positive (Signed Overflow): 0x80 (-128) - 0x01 (+1)
  // = 0x7F (+127) [carry_in=1]
  test_sbc_named(0x80, 0x01, true, 0x7F, true, true, false, false);

  // Positive - Negative -> Negative (Signed Overflow): 0x50 (+80) - 0xB0 (-80)
  // = 0xA0 (-96) [carry_in=1]
  test_sbc_named(0x50, 0xB0, true, 0xA0, true, false, true, false);

  // Negative - Negative -> Zero (No Overflow): 0x80 (-128) - 0x80 (-128) = 0x00
  // [carry_in=1]
  test_sbc_named(0x80, 0x80, true, 0x00, false, true, false, true);

  // Positive - Positive -> Positive (No Overflow): 0x50 (+80) - 0x10 (+16) =
  // 0x40 (+64) [carry_in=1]
  test_sbc_named(0x50, 0x10, true, 0x40, false, true, false, false);

  // Exhaustive 131,072 truth table verification (carry in {0, 1}, A in 0..255,
  // operand in 0..255)
  mem[CODE_BASE_ADDR] = 0xE9;  // SBC #imm
  uint32_t failures = 0;

  for (int carry_in = 0; carry_in < 2; ++carry_in) {
    uint8_t initial_ps = (carry_in != 0) ? FLAG_C : 0;
    for (int a_in = 0; a_in < 256; ++a_in) {
      for (int operand = 0; operand < 256; ++operand) {
        mem[CODE_BASE_ADDR + 1] = static_cast<uint8_t>(operand);
        regs->pc = CODE_BASE_ADDR;
        regs->a = static_cast<uint8_t>(a_in);
        regs->ps = initial_ps;

        cpu_execute(0);

        int diff = a_in - operand - (1 - carry_in);
        uint8_t expected_a = static_cast<uint8_t>(diff & 0xFF);
        bool expected_c = (diff >= 0);
        bool expected_v = ((a_in ^ operand) & (a_in ^ expected_a) & 0x80) != 0;
        bool expected_z = (expected_a == 0);
        bool expected_n = (expected_a & 0x80) != 0;

        bool actual_c = (regs->ps & FLAG_C) != 0;
        bool actual_z = (regs->ps & FLAG_Z) != 0;
        bool actual_v = (regs->ps & FLAG_V) != 0;
        bool actual_n = (regs->ps & FLAG_N) != 0;

        if (regs->a != expected_a || actual_c != expected_c ||
            actual_z != expected_z || actual_v != expected_v ||
            actual_n != expected_n) {
          ++failures;
        }
      }
    }
  }

  CHECK(failures == 0);
}

TEST_CASE("Exhaustive: [CPU-EX-03] Comparison (CMP/CPX/CPY) Execution") {
  CpuTestFixture_t fixture;
  auto* regs = cpu_get_registers();

  // Helper lambda for named comparison assertions
  auto test_cmp_named = [regs](uint8_t opcode, uint8_t reg_val, uint8_t operand,
                               char reg_choice, bool expected_c,
                               bool expected_z, bool expected_n) {
    mem[CODE_BASE_ADDR] = opcode;
    mem[CODE_BASE_ADDR + 1] = operand;
    regs->pc = CODE_BASE_ADDR;
    if (reg_choice == 'A') {
      regs->a = reg_val;
    } else if (reg_choice == 'X') {
      regs->x = reg_val;
    } else {
      regs->y = reg_val;
    }
    regs->ps = 0;

    cpu_execute(0);

    if (reg_choice == 'A') {
      CHECK(regs->a == reg_val);
    } else if (reg_choice == 'X') {
      CHECK(regs->x == reg_val);
    } else {
      CHECK(regs->y == reg_val);
    }
    CHECK(((regs->ps & FLAG_C) != 0) == expected_c);
    CHECK(((regs->ps & FLAG_Z) != 0) == expected_z);
    CHECK(((regs->ps & FLAG_N) != 0) == expected_n);
  };

  // Named edge cases:
  // CMP: reg > operand -> C=1, Z=0, N=0
  test_cmp_named(0xC9, 0x05, 0x03, 'A', true, false, false);
  // CMP: reg == operand -> C=1, Z=1, N=0
  test_cmp_named(0xC9, 0x42, 0x42, 'A', true, true, false);
  // CMP: reg < operand (diff = -2 = 0xFE) -> C=0, Z=0, N=1
  test_cmp_named(0xC9, 0x03, 0x05, 'A', false, false, true);

  // CPX: reg > operand -> C=1, Z=0, N=0
  test_cmp_named(0xE0, 0x80, 0x7F, 'X', true, false, false);
  // CPX: reg < operand (diff = -1 = 0xFF) -> C=0, Z=0, N=1
  test_cmp_named(0xE0, 0x7F, 0x80, 'X', false, false, true);

  // CPY: 0x00 vs 0xFF (diff = 0x01) -> C=0, Z=0, N=0
  test_cmp_named(0xC0, 0x00, 0xFF, 'Y', false, false, false);
  // CPY: 0xFF vs 0x00 (diff = 0xFF) -> C=1, Z=0, N=1
  test_cmp_named(0xC0, 0xFF, 0x00, 'Y', true, false, true);

  // Exhaustive CMP #imm (0xC9) 65,536 combinations
  {
    mem[CODE_BASE_ADDR] = 0xC9;
    uint32_t cmp_failures = 0;
    for (int reg = 0; reg < 256; ++reg) {
      for (int operand = 0; operand < 256; ++operand) {
        mem[CODE_BASE_ADDR + 1] = static_cast<uint8_t>(operand);
        regs->pc = CODE_BASE_ADDR;
        regs->a = static_cast<uint8_t>(reg);
        regs->ps = 0;

        cpu_execute(0);

        int diff = reg - operand;
        bool expected_c = (reg >= operand);
        bool expected_z = (reg == operand);
        bool expected_n = ((static_cast<uint8_t>(diff) & 0x80) != 0);

        bool actual_c = (regs->ps & FLAG_C) != 0;
        bool actual_z = (regs->ps & FLAG_Z) != 0;
        bool actual_n = (regs->ps & FLAG_N) != 0;

        if (regs->a != static_cast<uint8_t>(reg) || actual_c != expected_c ||
            actual_z != expected_z || actual_n != expected_n) {
          ++cmp_failures;
        }
      }
    }
    CHECK(cmp_failures == 0);
  }

  // Exhaustive CPX #imm (0xE0) 65,536 combinations
  {
    mem[CODE_BASE_ADDR] = 0xE0;
    uint32_t cpx_failures = 0;
    for (int reg = 0; reg < 256; ++reg) {
      for (int operand = 0; operand < 256; ++operand) {
        mem[CODE_BASE_ADDR + 1] = static_cast<uint8_t>(operand);
        regs->pc = CODE_BASE_ADDR;
        regs->x = static_cast<uint8_t>(reg);
        regs->ps = 0;

        cpu_execute(0);

        int diff = reg - operand;
        bool expected_c = (reg >= operand);
        bool expected_z = (reg == operand);
        bool expected_n = ((static_cast<uint8_t>(diff) & 0x80) != 0);

        bool actual_c = (regs->ps & FLAG_C) != 0;
        bool actual_z = (regs->ps & FLAG_Z) != 0;
        bool actual_n = (regs->ps & FLAG_N) != 0;

        if (regs->x != static_cast<uint8_t>(reg) || actual_c != expected_c ||
            actual_z != expected_z || actual_n != expected_n) {
          ++cpx_failures;
        }
      }
    }
    CHECK(cpx_failures == 0);
  }

  // Exhaustive CPY #imm (0xC0) 65,536 combinations
  {
    mem[CODE_BASE_ADDR] = 0xC0;
    uint32_t cpy_failures = 0;
    for (int reg = 0; reg < 256; ++reg) {
      for (int operand = 0; operand < 256; ++operand) {
        mem[CODE_BASE_ADDR + 1] = static_cast<uint8_t>(operand);
        regs->pc = CODE_BASE_ADDR;
        regs->y = static_cast<uint8_t>(reg);
        regs->ps = 0;

        cpu_execute(0);

        int diff = reg - operand;
        bool expected_c = (reg >= operand);
        bool expected_z = (reg == operand);
        bool expected_n = ((static_cast<uint8_t>(diff) & 0x80) != 0);

        bool actual_c = (regs->ps & FLAG_C) != 0;
        bool actual_z = (regs->ps & FLAG_Z) != 0;
        bool actual_n = (regs->ps & FLAG_N) != 0;

        if (regs->y != static_cast<uint8_t>(reg) || actual_c != expected_c ||
            actual_z != expected_z || actual_n != expected_n) {
          ++cpy_failures;
        }
      }
    }
    CHECK(cpy_failures == 0);
  }
}

TEST_CASE("Exhaustive: [CPU-EX-04] Logic (AND, ORA, EOR) Execution") {
  CpuTestFixture_t fixture;
  auto* regs = cpu_get_registers();

  // Helper lambda for named logic assertions
  auto test_logic_named = [regs](uint8_t opcode, uint8_t a_in, uint8_t operand,
                                 uint8_t expected_a, bool expected_z,
                                 bool expected_n) {
    mem[CODE_BASE_ADDR] = opcode;
    mem[CODE_BASE_ADDR + 1] = operand;
    regs->pc = CODE_BASE_ADDR;
    regs->a = a_in;
    regs->ps = 0;

    cpu_execute(0);

    CHECK(regs->a == expected_a);
    CHECK(((regs->ps & FLAG_Z) != 0) == expected_z);
    CHECK(((regs->ps & FLAG_N) != 0) == expected_n);
  };

  // Named edge cases:
  // AND: 0xFF & 0x00 = 0x00 -> Z=1, N=0
  test_logic_named(0x29, 0xFF, 0x00, 0x00, true, false);
  // AND: 0xFF & 0x80 = 0x80 -> Z=0, N=1
  test_logic_named(0x29, 0xFF, 0x80, 0x80, false, true);
  // AND: 0xAA & 0x55 = 0x00 -> Z=1, N=0
  test_logic_named(0x29, 0xAA, 0x55, 0x00, true, false);

  // ORA: 0x00 | 0x00 = 0x00 -> Z=1, N=0
  test_logic_named(0x09, 0x00, 0x00, 0x00, true, false);
  // ORA: 0x00 | 0x80 = 0x80 -> Z=0, N=1
  test_logic_named(0x09, 0x00, 0x80, 0x80, false, true);
  // ORA: 0x0F | 0x70 = 0x7F -> Z=0, N=0
  test_logic_named(0x09, 0x0F, 0x70, 0x7F, false, false);

  // EOR: 0xAA ^ 0xAA = 0x00 -> Z=1, N=0
  test_logic_named(0x49, 0xAA, 0xAA, 0x00, true, false);
  // EOR: 0x00 ^ 0x80 = 0x80 -> Z=0, N=1
  test_logic_named(0x49, 0x00, 0x80, 0x80, false, true);
  // EOR: 0x7F ^ 0x0F = 0x70 -> Z=0, N=0
  test_logic_named(0x49, 0x7F, 0x0F, 0x70, false, false);

  // Exhaustive AND #imm (0x29) 65,536 combinations
  {
    mem[CODE_BASE_ADDR] = 0x29;
    uint32_t and_failures = 0;
    for (int a_in = 0; a_in < 256; ++a_in) {
      for (int operand = 0; operand < 256; ++operand) {
        mem[CODE_BASE_ADDR + 1] = static_cast<uint8_t>(operand);
        regs->pc = CODE_BASE_ADDR;
        regs->a = static_cast<uint8_t>(a_in);
        regs->ps = 0;

        cpu_execute(0);

        uint8_t expected_a = static_cast<uint8_t>(a_in & operand);
        bool expected_z = (expected_a == 0);
        bool expected_n = (expected_a & 0x80) != 0;

        bool actual_z = (regs->ps & FLAG_Z) != 0;
        bool actual_n = (regs->ps & FLAG_N) != 0;

        if (regs->a != expected_a || actual_z != expected_z ||
            actual_n != expected_n) {
          ++and_failures;
        }
      }
    }
    CHECK(and_failures == 0);
  }

  // Exhaustive ORA #imm (0x09) 65,536 combinations
  {
    mem[CODE_BASE_ADDR] = 0x09;
    uint32_t ora_failures = 0;
    for (int a_in = 0; a_in < 256; ++a_in) {
      for (int operand = 0; operand < 256; ++operand) {
        mem[CODE_BASE_ADDR + 1] = static_cast<uint8_t>(operand);
        regs->pc = CODE_BASE_ADDR;
        regs->a = static_cast<uint8_t>(a_in);
        regs->ps = 0;

        cpu_execute(0);

        uint8_t expected_a = static_cast<uint8_t>(a_in | operand);
        bool expected_z = (expected_a == 0);
        bool expected_n = (expected_a & 0x80) != 0;

        bool actual_z = (regs->ps & FLAG_Z) != 0;
        bool actual_n = (regs->ps & FLAG_N) != 0;

        if (regs->a != expected_a || actual_z != expected_z ||
            actual_n != expected_n) {
          ++ora_failures;
        }
      }
    }
    CHECK(ora_failures == 0);
  }

  // Exhaustive EOR #imm (0x49) 65,536 combinations
  {
    mem[CODE_BASE_ADDR] = 0x49;
    uint32_t eor_failures = 0;
    for (int a_in = 0; a_in < 256; ++a_in) {
      for (int operand = 0; operand < 256; ++operand) {
        mem[CODE_BASE_ADDR + 1] = static_cast<uint8_t>(operand);
        regs->pc = CODE_BASE_ADDR;
        regs->a = static_cast<uint8_t>(a_in);
        regs->ps = 0;

        cpu_execute(0);

        uint8_t expected_a = static_cast<uint8_t>(a_in ^ operand);
        bool expected_z = (expected_a == 0);
        bool expected_n = (expected_a & 0x80) != 0;

        bool actual_z = (regs->ps & FLAG_Z) != 0;
        bool actual_n = (regs->ps & FLAG_N) != 0;

        if (regs->a != expected_a || actual_z != expected_z ||
            actual_n != expected_n) {
          ++eor_failures;
        }
      }
    }
    CHECK(eor_failures == 0);
  }
}

TEST_CASE(
    "Exhaustive: [CPU-EX-05] Shifts and Rotates (ASL, LSR, ROL, ROR) "
    "Execution") {
  CpuTestFixture_t fixture;
  auto* regs = cpu_get_registers();

  // Helper lambda for named shift/rotate assertions
  auto test_shift_named = [regs](uint8_t opcode, uint8_t a_in, bool carry_in,
                                 uint8_t expected_a, bool expected_c,
                                 bool expected_z, bool expected_n) {
    mem[CODE_BASE_ADDR] = opcode;
    regs->pc = CODE_BASE_ADDR;
    regs->a = a_in;
    regs->ps = carry_in ? FLAG_C : 0;

    cpu_execute(0);

    CHECK(regs->a == expected_a);
    CHECK(((regs->ps & FLAG_C) != 0) == expected_c);
    CHECK(((regs->ps & FLAG_Z) != 0) == expected_z);
    CHECK(((regs->ps & FLAG_N) != 0) == expected_n);
  };

  // Named edge cases:
  // ASL A: 0x80 -> A=0x00, C=1, Z=1, N=0
  test_shift_named(0x0A, 0x80, false, 0x00, true, true, false);
  // ASL A: 0x40 -> A=0x80, C=0, Z=0, N=1
  test_shift_named(0x0A, 0x40, false, 0x80, false, false, true);
  // ASL A: 0x01 -> A=0x02, C=0, Z=0, N=0
  test_shift_named(0x0A, 0x01, false, 0x02, false, false, false);

  // LSR A: 0x01 -> A=0x00, C=1, Z=1, N=0
  test_shift_named(0x4A, 0x01, false, 0x00, true, true, false);
  // LSR A: 0x80 -> A=0x40, C=0, Z=0, N=0
  test_shift_named(0x4A, 0x80, false, 0x40, false, false, false);
  // LSR A: 0x03 -> A=0x01, C=1, Z=0, N=0
  test_shift_named(0x4A, 0x03, false, 0x01, true, false, false);

  // ROL A: val=0x80, C=0 -> A=0x00, C=1, Z=1, N=0
  test_shift_named(0x2A, 0x80, false, 0x00, true, true, false);
  // ROL A: val=0x80, C=1 -> A=0x01, C=1, Z=0, N=0
  test_shift_named(0x2A, 0x80, true, 0x01, true, false, false);
  // ROL A: val=0x40, C=0 -> A=0x80, C=0, Z=0, N=1
  test_shift_named(0x2A, 0x40, false, 0x80, false, false, true);

  // ROR A: val=0x01, C=0 -> A=0x00, C=1, Z=1, N=0
  test_shift_named(0x6A, 0x01, false, 0x00, true, true, false);
  // ROR A: val=0x01, C=1 -> A=0x80, C=1, Z=0, N=1
  test_shift_named(0x6A, 0x01, true, 0x80, true, false, true);
  // ROR A: val=0x80, C=0 -> A=0x40, C=0, Z=0, N=0
  test_shift_named(0x6A, 0x80, false, 0x40, false, false, false);

  // Exhaustive ASL A (0x0A)
  {
    mem[CODE_BASE_ADDR] = 0x0A;
    uint32_t asl_failures = 0;
    for (int val = 0; val < 256; ++val) {
      regs->pc = CODE_BASE_ADDR;
      regs->a = static_cast<uint8_t>(val);
      regs->ps = 0;

      cpu_execute(0);

      uint8_t expected_a = static_cast<uint8_t>((val << 1) & 0xFF);
      bool expected_c = (val & 0x80) != 0;
      bool expected_z = (expected_a == 0);
      bool expected_n = (expected_a & 0x80) != 0;

      bool actual_c = (regs->ps & FLAG_C) != 0;
      bool actual_z = (regs->ps & FLAG_Z) != 0;
      bool actual_n = (regs->ps & FLAG_N) != 0;

      if (regs->a != expected_a || actual_c != expected_c ||
          actual_z != expected_z || actual_n != expected_n) {
        ++asl_failures;
      }
    }
    CHECK(asl_failures == 0);
  }

  // Exhaustive LSR A (0x4A)
  {
    mem[CODE_BASE_ADDR] = 0x4A;
    uint32_t lsr_failures = 0;
    for (int val = 0; val < 256; ++val) {
      regs->pc = CODE_BASE_ADDR;
      regs->a = static_cast<uint8_t>(val);
      regs->ps = 0;

      cpu_execute(0);

      uint8_t expected_a = static_cast<uint8_t>((val >> 1) & 0xFF);
      bool expected_c = (val & 0x01) != 0;
      bool expected_z = (expected_a == 0);
      bool expected_n = (expected_a & 0x80) != 0;  // Always false

      bool actual_c = (regs->ps & FLAG_C) != 0;
      bool actual_z = (regs->ps & FLAG_Z) != 0;
      bool actual_n = (regs->ps & FLAG_N) != 0;

      if (regs->a != expected_a || actual_c != expected_c ||
          actual_z != expected_z || actual_n != expected_n) {
        ++lsr_failures;
      }
    }
    CHECK(lsr_failures == 0);
  }

  // Exhaustive ROL A (0x2A) with carry_in in {0, 1}
  {
    mem[CODE_BASE_ADDR] = 0x2A;
    uint32_t rol_failures = 0;
    for (int c_in = 0; c_in < 2; ++c_in) {
      uint8_t initial_ps = (c_in != 0) ? FLAG_C : 0;
      for (int val = 0; val < 256; ++val) {
        regs->pc = CODE_BASE_ADDR;
        regs->a = static_cast<uint8_t>(val);
        regs->ps = initial_ps;

        cpu_execute(0);

        uint8_t expected_a = static_cast<uint8_t>(((val << 1) | c_in) & 0xFF);
        bool expected_c = (val & 0x80) != 0;
        bool expected_z = (expected_a == 0);
        bool expected_n = (expected_a & 0x80) != 0;

        bool actual_c = (regs->ps & FLAG_C) != 0;
        bool actual_z = (regs->ps & FLAG_Z) != 0;
        bool actual_n = (regs->ps & FLAG_N) != 0;

        if (regs->a != expected_a || actual_c != expected_c ||
            actual_z != expected_z || actual_n != expected_n) {
          ++rol_failures;
        }
      }
    }
    CHECK(rol_failures == 0);
  }

  // Exhaustive ROR A (0x6A) with carry_in in {0, 1}
  {
    mem[CODE_BASE_ADDR] = 0x6A;
    uint32_t ror_failures = 0;
    for (int c_in = 0; c_in < 2; ++c_in) {
      uint8_t initial_ps = (c_in != 0) ? FLAG_C : 0;
      for (int val = 0; val < 256; ++val) {
        regs->pc = CODE_BASE_ADDR;
        regs->a = static_cast<uint8_t>(val);
        regs->ps = initial_ps;

        cpu_execute(0);

        uint8_t expected_a =
            static_cast<uint8_t>(((val >> 1) | (c_in << 7)) & 0xFF);
        bool expected_c = (val & 0x01) != 0;
        bool expected_z = (expected_a == 0);
        bool expected_n = (expected_a & 0x80) != 0;

        bool actual_c = (regs->ps & FLAG_C) != 0;
        bool actual_z = (regs->ps & FLAG_Z) != 0;
        bool actual_n = (regs->ps & FLAG_N) != 0;

        if (regs->a != expected_a || actual_c != expected_c ||
            actual_z != expected_z || actual_n != expected_n) {
          ++ror_failures;
        }
      }
    }
    CHECK(ror_failures == 0);
  }
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
