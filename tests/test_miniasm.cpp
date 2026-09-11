// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AppEnvironment.h"

namespace {

struct MiniAsmHarness_t {
  bool is_initialized = false;

  MiniAsmHarness_t() {
    AppConfig_t config = {};
    app_config_default(&config);
    config.apple2_type = A2TYPE_APPLE2EENHANCED;
    config.is_boot = false;
    app_env_resolve_paths(&config);

    if (app_controller_initialize(&config) == 0) {
      is_initialized = true;
    }
  }

  ~MiniAsmHarness_t() {
    if (is_initialized) {
      app_controller_shutdown();
      is_initialized = false;
    }
  }

  MiniAsmHarness_t(const MiniAsmHarness_t&) = delete;
  auto operator=(const MiniAsmHarness_t&) -> MiniAsmHarness_t& = delete;
  MiniAsmHarness_t(MiniAsmHarness_t&&) = delete;
  auto operator=(MiniAsmHarness_t&&) -> MiniAsmHarness_t& = delete;

  auto write_byte(uint16_t addr, uint8_t val) -> void {
    if (memdirty != nullptr) {
      memdirty[addr >> 8] = 0xFF;
    }
    uint8_t* page = (memwrite != nullptr) ? memwrite[addr >> 8] : nullptr;
    if (page != nullptr) {
      *(page + (addr & 0xFF)) = val;
    } else if (mem != nullptr) {
      mem[addr] = val;
    }
  }

  auto read_byte(uint16_t addr) const -> uint8_t {
    if (mem != nullptr) {
      return mem[addr];
    }
    return 0;
  }

  auto run_line(const char* line) -> bool {
    if (!is_initialized) {
      return false;
    }

    uint16_t idx = 0;
    for (const char* p = line; *p != '\0'; ++p, ++idx) {
      write_byte(static_cast<uint16_t>(0x0200 + idx),
                 static_cast<uint8_t>(static_cast<uint8_t>(*p) | 0x80));
    }
    write_byte(static_cast<uint16_t>(0x0200 + idx), 0x8D);

    // Monitor CSW ($36-$37) and KSW ($38-$39)
    write_byte(0x36, 0xF0);
    write_byte(0x37, 0xFD);
    write_byte(0x38, 0x1B);
    write_byte(0x39, 0xFD);

    // Loader stub at $1000 ($8D $07 $C0, $4C $9C $CF)
    write_byte(0x1000, 0x8D);
    write_byte(0x1001, 0x07);
    write_byte(0x1002, 0xC0);
    write_byte(0x1003, 0x4C);
    write_byte(0x1004, 0x9C);
    write_byte(0x1005, 0xCF);

    CpuRegisters_t* regs = cpu_get_registers();
    regs->pc = 0x1000;
    regs->sp = 0x01FF;
    regs->ps = 0x20;

    bool matched = false;
    for (int step = 0; step < 50000; ++step) {
      cpu_execute(0);
      uint16_t pc = regs->pc;
      if (pc == 0xCF55) {
        matched = true;
      }
      if (matched &&
          (pc == 0xFCF0 || pc == 0xFD1B || pc == 0xFD0C || pc == 0xFD67)) {
        return true;
      }
      if (pc == 0xCF97 || pc == 0xFF69 || pc == 0xFCD2) {
        return false;
      }
    }
    return matched;
  }
};

}  // namespace

TEST_CASE("Enhanced Apple //e Mini-Assembler") {
  MiniAsmHarness_t harness;
  REQUIRE(harness.is_initialized);

  SUBCASE("Assembles 300:LDA #$01") {
    CHECK(harness.run_line("300:LDA #$01"));
    CHECK(harness.read_byte(0x0300) == 0xA9);
    CHECK(harness.read_byte(0x0301) == 0x01);
  }

  SUBCASE("Assembles 300:NOP") {
    CHECK(harness.run_line("300:NOP"));
    CHECK(harness.read_byte(0x0300) == 0xEA);
  }

  SUBCASE("Assembles 300:RTS") {
    CHECK(harness.run_line("300:RTS"));
    CHECK(harness.read_byte(0x0300) == 0x60);
  }

  SUBCASE("Assembles 300:STA $0400") {
    CHECK(harness.run_line("300:STA $0400"));
    CHECK(harness.read_byte(0x0300) == 0x8D);
    CHECK(harness.read_byte(0x0301) == 0x00);
    CHECK(harness.read_byte(0x0302) == 0x04);
  }

  SUBCASE("Assembles 300:JMP $C000") {
    CHECK(harness.run_line("300:JMP $C000"));
    CHECK(harness.read_byte(0x0300) == 0x4C);
    CHECK(harness.read_byte(0x0301) == 0x00);
    CHECK(harness.read_byte(0x0302) == 0xC0);
  }

  SUBCASE("Assembles 300:BNE $0310") {
    CHECK(harness.run_line("300:BNE $0310"));
    CHECK(harness.read_byte(0x0300) == 0xD0);
    CHECK(harness.read_byte(0x0301) == 0x0E);
  }

  SUBCASE("Assembles consecutive instructions") {
    CHECK(harness.run_line("300:LDA #$42"));
    CHECK(harness.read_byte(0x0300) == 0xA9);
    CHECK(harness.read_byte(0x0301) == 0x42);

    CHECK(harness.run_line(" STA $0400"));
    CHECK(harness.read_byte(0x0302) == 0x8D);
    CHECK(harness.read_byte(0x0303) == 0x00);
    CHECK(harness.read_byte(0x0304) == 0x04);

    CHECK(harness.run_line(" RTS"));
    CHECK(harness.read_byte(0x0305) == 0x60);
  }

  SUBCASE("Rejects invalid mnemonic") {
    CHECK_FALSE(harness.run_line("300:XYZ #$01"));
  }
}
