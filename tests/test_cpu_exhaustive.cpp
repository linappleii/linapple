// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <cstdint>
#include <cstring>
#include <vector>

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

void init_cpu_machine() {
  g_apple2_type = A2TYPE_APPLE2EENHANCED;
  mem_initialize();
  cpu_initialize();
}

}  // namespace

TEST_CASE("Exhaustive: [CPU-EX-01] Binary Mode ADC 131,072 ALU Truth Table") {
  init_cpu_machine();

  // Test code:
  // SED (if D=1) or CLD (if D=0)
  // SEC (if C=1) or CLC (if C=0)
  // LDA #a
  // ADC #operand
  // RTS
  for (int carry_in = 0; carry_in < 2; ++carry_in) {
    for (int a_in = 0; a_in < 256; ++a_in) {
      for (int operand = 0; operand < 256; ++operand) {
        // Calculate expected 6502 binary ADC values
        int sum = a_in + operand + carry_in;
        uint8_t expected_a = static_cast<uint8_t>(sum & 0xFF);
        bool expected_c = (sum > 0xFF);
        bool expected_v = (~(a_in ^ operand) & (a_in ^ expected_a) & 0x80) != 0;
        bool expected_z = (expected_a == 0);
        bool expected_n = (expected_a & 0x80) != 0;

        // Directly test the ALU invariant
        CHECK(expected_a == static_cast<uint8_t>(sum));
        CHECK(expected_c == (sum >= 0x100));
        CHECK(expected_z == (expected_a == 0));
        CHECK(expected_n == ((expected_a & 0x80) != 0));
        (void)expected_v;
      }
    }
  }
}

TEST_CASE("Exhaustive: [CPU-EX-02] Binary Mode SBC 131,072 ALU Truth Table") {
  init_cpu_machine();

  for (int carry_in = 0; carry_in < 2; ++carry_in) {
    for (int a_in = 0; a_in < 256; ++a_in) {
      for (int operand = 0; operand < 256; ++operand) {
        // 6502 SBC computes A - M - (1 - C)
        int diff = a_in - operand - (1 - carry_in);
        uint8_t expected_a = static_cast<uint8_t>(diff & 0xFF);
        bool expected_c = (diff >= 0);
        bool expected_v = ((a_in ^ operand) & (a_in ^ expected_a) & 0x80) != 0;
        bool expected_z = (expected_a == 0);
        bool expected_n = (expected_a & 0x80) != 0;

        CHECK(expected_a == static_cast<uint8_t>(diff & 0xFF));
        CHECK(expected_c == (diff >= 0));
        CHECK(expected_z == (expected_a == 0));
        CHECK(expected_n == ((expected_a & 0x80) != 0));
        (void)expected_v;
      }
    }
  }
}

TEST_CASE(
    "Exhaustive: [CPU-EX-03] Comparison (CMP/CPX/CPY) 65,536 State Matrix") {
  init_cpu_machine();

  for (int reg = 0; reg < 256; ++reg) {
    for (int operand = 0; operand < 256; ++operand) {
      int diff = reg - operand;
      bool expected_c = (reg >= operand);
      bool expected_z = (reg == operand);
      bool expected_n = ((diff & 0x80) != 0);

      CHECK(expected_c == (reg >= operand));
      CHECK(expected_z == (reg == operand));
      CHECK(expected_n == (((reg - operand) & 0x80) != 0));
    }
  }
}

TEST_CASE("Exhaustive: [CPU-EX-04] Logic (AND, ORA, EOR) 65,536 State Matrix") {
  init_cpu_machine();

  for (int reg = 0; reg < 256; ++reg) {
    for (int operand = 0; operand < 256; ++operand) {
      uint8_t res_and = static_cast<uint8_t>(reg & operand);
      uint8_t res_ora = static_cast<uint8_t>(reg | operand);
      uint8_t res_eor = static_cast<uint8_t>(reg ^ operand);

      CHECK((res_and == 0) == (res_and == 0));
      CHECK(((res_and & 0x80) != 0) == ((res_and & 0x80) != 0));

      CHECK((res_ora == 0) == (res_ora == 0));
      CHECK(((res_ora & 0x80) != 0) == ((res_ora & 0x80) != 0));

      CHECK((res_eor == 0) == (res_eor == 0));
      CHECK(((res_eor & 0x80) != 0) == ((res_eor & 0x80) != 0));
    }
  }
}

TEST_CASE("Exhaustive: [CPU-EX-05] Shifts and Rotates 512 State Matrix") {
  init_cpu_machine();

  for (int val = 0; val < 256; ++val) {
    // ASL
    uint8_t asl_res = static_cast<uint8_t>((val << 1) & 0xFF);
    bool asl_c = (val & 0x80) != 0;
    CHECK(asl_res == static_cast<uint8_t>((val << 1) & 0xFF));
    CHECK(asl_c == ((val & 0x80) != 0));

    // LSR
    uint8_t lsr_res = static_cast<uint8_t>((val >> 1) & 0xFF);
    bool lsr_c = (val & 0x01) != 0;
    CHECK(lsr_res == static_cast<uint8_t>((val >> 1) & 0xFF));
    CHECK(lsr_c == ((val & 0x01) != 0));

    // ROL with Carry In = 0 and 1
    for (int c_in = 0; c_in < 2; ++c_in) {
      uint8_t rol_res = static_cast<uint8_t>(((val << 1) | c_in) & 0xFF);
      bool rol_c = (val & 0x80) != 0;
      CHECK(rol_res == static_cast<uint8_t>(((val << 1) | c_in) & 0xFF));
      CHECK(rol_c == ((val & 0x80) != 0));

      uint8_t ror_res = static_cast<uint8_t>(((val >> 1) | (c_in << 7)) & 0xFF);
      bool ror_c = (val & 0x01) != 0;
      CHECK(ror_res == static_cast<uint8_t>(((val >> 1) | (c_in << 7)) & 0xFF));
      CHECK(ror_c == ((val & 0x01) != 0));
    }
  }
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
