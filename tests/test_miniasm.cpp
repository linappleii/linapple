// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AppEnvironment.h"

static auto run_miniasm_line(const char* line) -> bool {
  auto* ctx = mem_get_active_context();
  int idx = 0;
  for (const char* p = line; *p; ++p, ++idx) {
    if (ctx->memmain)
      ctx->memmain[0x0200 + idx] = static_cast<uint8_t>(*p | 0x80);
    if (mem) mem[0x0200 + idx] = static_cast<uint8_t>(*p | 0x80);
  }
  if (ctx->memmain) ctx->memmain[0x0200 + idx] = 0x8D;
  if (mem) mem[0x0200 + idx] = 0x8D;

  // Set Monitor CSW ($36-$37) and KSW ($38-$39)
  if (ctx->memmain) {
    ctx->memmain[0x36] = 0xF0;
    ctx->memmain[0x37] = 0xFD;  // COUT1
    ctx->memmain[0x38] = 0x1B;
    ctx->memmain[0x39] = 0xFD;  // KEYIN
    ctx->memmain[0x1000] = 0x8D;
    ctx->memmain[0x1001] = 0x07;
    ctx->memmain[0x1002] = 0xC0;
    ctx->memmain[0x1003] = 0x4C;
    ctx->memmain[0x1004] = 0x9C;
    ctx->memmain[0x1005] = 0xCF;
  }
  if (mem) {
    mem[0x36] = 0xF0;
    mem[0x37] = 0xFD;
    mem[0x38] = 0x1B;
    mem[0x39] = 0xFD;
    mem[0x1000] = 0x8D;
    mem[0x1001] = 0x07;
    mem[0x1002] = 0xC0;
    mem[0x1003] = 0x4C;
    mem[0x1004] = 0x9C;
    mem[0x1005] = 0xCF;
  }

  cpu_get_registers()->pc = 0x1000;
  cpu_get_registers()->sp = 0x01FF;
  cpu_get_registers()->ps = 0x20;

  bool matched = false;
  for (int step = 0; step < 50000; ++step) {
    cpu_execute(0);
    uint16_t pc = cpu_get_registers()->pc;
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

TEST_CASE("Enhanced Apple //e Mini-Assembler") {
  AppConfig_t config = {};
  app_config_default(&config);
  config.apple2_type = A2TYPE_APPLE2EENHANCED;
  config.is_boot = false;
  app_env_resolve_paths(&config);

  REQUIRE(app_controller_initialize(&config) == 0);

  SUBCASE("Assembles 300:LDA #$01") {
    CHECK(run_miniasm_line("300:LDA #$01"));
    CHECK(mem[0x0300] == 0xA9);
    CHECK(mem[0x0301] == 0x01);
  }

  SUBCASE("Assembles 300:NOP") {
    CHECK(run_miniasm_line("300:NOP"));
    CHECK(mem[0x0300] == 0xEA);
  }

  SUBCASE("Assembles 300:RTS") {
    CHECK(run_miniasm_line("300:RTS"));
    CHECK(mem[0x0300] == 0x60);
  }

  SUBCASE("Assembles 300:STA $0400") {
    CHECK(run_miniasm_line("300:STA $0400"));
    CHECK(mem[0x0300] == 0x8D);
    CHECK(mem[0x0301] == 0x00);
    CHECK(mem[0x0302] == 0x04);
  }

  SUBCASE("Assembles 300:JMP $C000") {
    CHECK(run_miniasm_line("300:JMP $C000"));
    CHECK(mem[0x0300] == 0x4C);
    CHECK(mem[0x0301] == 0x00);
    CHECK(mem[0x0302] == 0xC0);
  }

  SUBCASE("Assembles 300:BNE $0310") {
    CHECK(run_miniasm_line("300:BNE $0310"));
    CHECK(mem[0x0300] == 0xD0);
    CHECK(mem[0x0301] == 0x0E);
  }

  SUBCASE("Assembles consecutive instructions") {
    CHECK(run_miniasm_line("300:LDA #$42"));
    CHECK(mem[0x0300] == 0xA9);
    CHECK(mem[0x0301] == 0x42);

    CHECK(run_miniasm_line(" STA $0400"));
    CHECK(mem[0x0302] == 0x8D);
    CHECK(mem[0x0303] == 0x00);
    CHECK(mem[0x0304] == 0x04);

    CHECK(run_miniasm_line(" RTS"));
    CHECK(mem[0x0305] == 0x60);
  }

  SUBCASE("Rejects invalid mnemonic") {
    CHECK_FALSE(run_miniasm_line("300:XYZ #$01"));
  }

  app_controller_shutdown();
}
