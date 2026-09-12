// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstring>

#include "Debugger/Debugger_Assembler.h"
#include "Debugger/Debugger_Console.h"
#include "Debugger/Debugger_Parser.h"
#include "Debugger/Debugger_Range.h"
#include "Debugger/Debugger_Types.h"
#include "core/Util_Text.h"
#include "doctest.h"

namespace {

struct ScopedDebuggerState_t {
  Arg_t saved_args[MAX_ARGS]{};
  Arg_t saved_arg_raw[MAX_ARGS]{};
  int saved_arg_raw_count{0};
  int saved_display_total{0};
  int saved_display_start{0};
  int saved_display_lines{0};
  int saved_display_width{0};
  conchar_t saved_display[CONSOLE_DISPLAY_HEIGHT][CONSOLE_WIDTH]{};

  ScopedDebuggerState_t() {
    std::memcpy(saved_args, g_args, sizeof(saved_args));
    std::memcpy(saved_arg_raw, g_arg_raw, sizeof(saved_arg_raw));
    saved_arg_raw_count = g_arg_raw_count;
    saved_display_total = g_console_display_total;
    saved_display_start = g_console_display_start;
    saved_display_lines = g_console_display_lines;
    saved_display_width = g_console_display_width;
    std::memcpy(saved_display, g_console_display, sizeof(saved_display));

    ArgsClear();
    g_console_display_total = 0;
    g_console_display_start = 0;
    std::memset(g_console_display, 0, sizeof(g_console_display));
  }

  ~ScopedDebuggerState_t() {
    std::memcpy(g_args, saved_args, sizeof(saved_args));
    std::memcpy(g_arg_raw, saved_arg_raw, sizeof(saved_arg_raw));
    g_arg_raw_count = saved_arg_raw_count;
    g_console_display_total = saved_display_total;
    g_console_display_start = saved_display_start;
    g_console_display_lines = saved_display_lines;
    g_console_display_width = saved_display_width;
    std::memcpy(g_console_display, saved_display, sizeof(saved_display));
  }

  ScopedDebuggerState_t(const ScopedDebuggerState_t&) = delete;
  auto operator=(const ScopedDebuggerState_t&)
      -> ScopedDebuggerState_t& = delete;
  ScopedDebuggerState_t(ScopedDebuggerState_t&&) = delete;
  auto operator=(ScopedDebuggerState_t&&) -> ScopedDebuggerState_t& = delete;
};

}  // namespace

TEST_CASE("Debugger Parser: String and Case Manipulation") {
  SUBCASE("util_strupr converts lowercase ASCII to uppercase") {
    char text[] = "lda #$01, sta $2000";
    util_strupr(text);
    CHECK(strcmp(text, "LDA #$01, STA $2000") == 0);
  }

  SUBCASE("RemoveWhiteSpaceReverse trims trailing spaces") {
    char text[] = "300:LDA #$01   ";
    int remaining = RemoveWhiteSpaceReverse(text);
    CHECK(strcmp(text, "300:LDA #$01") == 0);
    CHECK(remaining == 11);
  }

  SUBCASE("TextConvertTabsToSpaces replaces tabs with spaces") {
    char dst[64] = {0};
    const char src[] = "\tLDA\t#$00";
    TextConvertTabsToSpaces(dst, src, sizeof(dst), 4);
    CHECK(dst[0] == ' ');
    CHECK(dst[1] == ' ');
    CHECK(dst[2] == ' ');
    CHECK(dst[3] == ' ');
    CHECK(strcmp(dst, "    LDA #$00") == 0);
  }

  SUBCASE("util_safe_strncat handles capacity boundaries safely") {
    char buf[10] = "Hello";
    util_safe_strncat(buf, " World", sizeof(buf));
    CHECK(strcmp(buf, "Hello Wor") == 0);
    CHECK(strlen(buf) == 9);

    char small_buf[4] = "Hi";
    util_safe_strncat(small_buf, "!", sizeof(small_buf));
    CHECK(strcmp(small_buf, "Hi!") == 0);

    // No-op on zero size or null
    util_safe_strncat(nullptr, "test", 10);
    util_safe_strncat(buf, nullptr, sizeof(buf));
    util_safe_strncat(buf, "test", 0);
  }
}

TEST_CASE("Debugger Parser: Tokenization and Matching") {
  SUBCASE("ParserFindToken matches operator tokens") {
    ArgToken_e token = NO_TOKEN;
    const char* rest = ParserFindToken(":1000", g_tokens, NUM_TOKENS, &token);
    CHECK(token == TOKEN_COLON);
    CHECK(rest != nullptr);
    CHECK(strcmp(rest, "1000") == 0);

    token = NO_TOKEN;
    rest = ParserFindToken(",20", g_tokens, NUM_TOKENS, &token);
    CHECK(token == TOKEN_COMMA);
    CHECK(rest != nullptr);
    CHECK(strcmp(rest, "20") == 0);

    token = NO_TOKEN;
    rest = ParserFindToken("LDA", g_tokens, NUM_TOKENS, &token);
    CHECK(token == NO_TOKEN);
    CHECK(rest == nullptr);
  }

  SUBCASE("FindTokenOrAlphaNumeric identifies alphanumeric start or tokens") {
    ArgToken_e token = NO_TOKEN;
    const char* rest =
        FindTokenOrAlphaNumeric("LDA #$01", g_tokens, NUM_TOKENS, &token);
    CHECK(token == TOKEN_ALPHANUMERIC);
    CHECK(rest != nullptr);
    CHECK(strcmp(rest, "LDA #$01") == 0);

    token = NO_TOKEN;
    rest = FindTokenOrAlphaNumeric(":1000", g_tokens, NUM_TOKENS, &token);
    CHECK(token == TOKEN_COLON);
    CHECK(rest != nullptr);
    CHECK(strcmp(rest, "1000") == 0);

    token = NO_TOKEN;
    rest = FindTokenOrAlphaNumeric(",20", g_tokens, NUM_TOKENS, &token);
    CHECK(token == TOKEN_COMMA);
    CHECK(rest != nullptr);
    CHECK(strcmp(rest, "20") == 0);

    token = TOKEN_ALPHANUMERIC;
    rest = FindTokenOrAlphaNumeric("?unknown", g_tokens, NUM_TOKENS, &token);
    CHECK(token == NO_TOKEN);
    CHECK(rest != nullptr);
    CHECK(strcmp(rest, "?unknown") == 0);
  }

  SUBCASE("SkipUntilToken advances through alphanumeric chars to next token") {
    ArgToken_e token = NO_TOKEN;
    const char* rest = SkipUntilToken("LDA #$01", g_tokens, NUM_TOKENS, &token);
    CHECK(token == TOKEN_SPACE);
    CHECK(rest != nullptr);
    CHECK(strcmp(rest, " #$01") == 0);
  }
}

TEST_CASE("Debugger Parser: Command Line and Arguments Parsing") {
  const ScopedDebuggerState_t state_guard;

  SUBCASE("Parse simple memory examine command") {
    char input[64] = "300";
    int nArgs = ParseInput(input, true);
    CHECK(nArgs == 0);
    CHECK(strcmp(g_args[0].sArg, "300") == 0);
  }

  SUBCASE("Parse command with quoted filename and arguments") {
    char input[128] = "BLOAD \"MYFILE.BIN\", 2000";
    int nArgs = ParseInput(input, true);
    CHECK(nArgs == 3);
    CHECK(strcmp(g_args[0].sArg, "BLOAD") == 0);
    CHECK(strcmp(g_args[1].sArg, "MYFILE.BIN") == 0);
    CHECK(g_args[2].eToken == TOKEN_COMMA);
    CHECK(strcmp(g_args[3].sArg, "2000") == 0);
  }

  SUBCASE("Parse memory range with colon") {
    char input[64] = "M 300:310";
    int nArgs = ParseInput(input, true);
    CHECK(nArgs == 3);
    CHECK(strcmp(g_args[0].sArg, "M") == 0);
    CHECK(strcmp(g_args[1].sArg, "300") == 0);
    CHECK(g_args[2].eToken == TOKEN_COLON);
    CHECK(strcmp(g_args[3].sArg, "310") == 0);
  }
}

TEST_CASE("Debugger Range: Parsing and Calculations") {
  const ScopedDebuggerState_t state_guard;

  SUBCASE("Range_Get and Range_CalcEndLen with address range") {
    g_args[1].nValue = 0x1000;
    g_args[2].eToken = TOKEN_COLON;
    g_args[3].nValue = 0x10FF;

    uint16_t addr1 = 0;
    uint16_t addr2 = 0;
    RangeType_t rtype = Range_Get(addr1, addr2, 1);
    CHECK(rtype == RANGE_HAS_END);
    CHECK(addr1 == 0x1000);
    CHECK(addr2 == 0x10FF);

    RangeEndLen_t end_len = {0, 0};
    bool ok = Range_CalcEndLen(rtype, addr1, addr2, end_len);
    CHECK(ok == true);
    CHECK(end_len.nAddressEnd == 0x10FF);
    CHECK(end_len.nAddressLen == 0x100);
  }

  SUBCASE("Range_Get and Range_CalcEndLen with reversed address range") {
    g_args[1].nValue = 0x2000;
    g_args[2].eToken = TOKEN_COLON;
    g_args[3].nValue = 0x1000;

    uint16_t addr1 = 0;
    uint16_t addr2 = 0;
    RangeType_t rtype = Range_Get(addr1, addr2, 1);
    CHECK(rtype == RANGE_HAS_END);
    CHECK(addr1 == 0x1000);
    CHECK(addr2 == 0x2000);

    RangeEndLen_t end_len = {0, 0};
    bool ok = Range_CalcEndLen(rtype, addr1, addr2, end_len);
    CHECK(ok == true);
    CHECK(end_len.nAddressEnd == 0x2000);
    CHECK(end_len.nAddressLen == 0x1001);
  }

  SUBCASE("Range_Get with comma length") {
    g_args[1].nValue = 0x2000;
    g_args[2].eToken = TOKEN_COMMA;
    g_args[3].nValue = 0x10;

    uint16_t addr1 = 0;
    uint16_t addr2 = 0;
    RangeType_t rtype = Range_Get(addr1, addr2, 1);
    CHECK(rtype == RANGE_HAS_LEN);
    CHECK(addr1 == 0x2000);
    CHECK(addr2 == 0x10);

    RangeEndLen_t end_len = {0, 0};
    bool ok = Range_CalcEndLen(rtype, addr1, addr2, end_len);
    CHECK(ok == true);
    CHECK(end_len.nAddressEnd == 0x200F);
    CHECK(end_len.nAddressLen == 0x10);
  }

  SUBCASE("Range_Get missing second argument returns error") {
    g_args[1].nValue = 0x3000;
    g_args[2].eToken = NO_TOKEN;

    uint16_t addr1 = 0;
    uint16_t addr2 = 0;
    RangeType_t rtype = Range_Get(addr1, addr2, 1);
    CHECK(rtype == RANGE_MISSING_ARG_2);
    CHECK(addr1 == 0x3000);
    CHECK(addr2 == 0);

    RangeEndLen_t end_len = {0, 0};
    bool ok = Range_CalcEndLen(rtype, addr1, addr2, end_len);
    CHECK(ok == false);
  }
}

TEST_CASE("Debugger Assembler: Mnemonic Hashing and Opcode Identification") {
  SUBCASE("AssemblerHashMnemonic computes exact hashes and case invariance") {
    uint32_t hash_lda = AssemblerHashMnemonic("LDA");
    uint32_t hash_sta = AssemblerHashMnemonic("STA");
    uint32_t hash_jmp = AssemblerHashMnemonic("JMP");
    uint32_t hash_rts = AssemblerHashMnemonic("RTS");
    uint32_t hash_nop = AssemblerHashMnemonic("NOP");

    CHECK(hash_lda == 113889u);
    CHECK(hash_sta == 121569u);
    CHECK(hash_jmp == 112144u);
    CHECK(hash_rts == 120563u);
    CHECK(hash_nop == 116304u);
    CHECK(AssemblerHashMnemonic("lda") == hash_lda);
    CHECK(AssemblerHashMnemonic("sta") == hash_sta);
    CHECK(hash_lda != hash_sta);
    CHECK(hash_lda != hash_jmp);
  }

  SUBCASE("IsOpcodeBranch identifies conditional branches and BRA") {
    CHECK(IsOpcodeBranch(0x10) == true);  // BPL
    CHECK(IsOpcodeBranch(0x30) == true);  // BMI
    CHECK(IsOpcodeBranch(0x50) == true);  // BVC
    CHECK(IsOpcodeBranch(0x70) == true);  // BVS
    CHECK(IsOpcodeBranch(0x90) == true);  // BCC
    CHECK(IsOpcodeBranch(0xB0) == true);  // BCS
    CHECK(IsOpcodeBranch(0xD0) == true);  // BNE
    CHECK(IsOpcodeBranch(0xF0) == true);  // BEQ
    CHECK(IsOpcodeBranch(0x80) == true);  // BRA (65C02)

    CHECK(IsOpcodeBranch(0xEA) == false);  // NOP
    CHECK(IsOpcodeBranch(0x4C) == false);  // JMP abs
    CHECK(IsOpcodeBranch(0x20) == false);  // JSR abs
    CHECK(IsOpcodeBranch(0x60) == false);  // RTS
  }

  SUBCASE("IsOpcodeValid distinguishes standard vs invalid opcodes") {
    CHECK(IsOpcodeValid(0xEA) == true);   // NOP
    CHECK(IsOpcodeValid(0xA9) == true);   // LDA #
    CHECK(IsOpcodeValid(0x60) == true);   // RTS
    CHECK(IsOpcodeValid(0x02) == false);  // KIL / JAM on standard 6502
  }
}

TEST_CASE("Debugger Console: Viewport Display Sizing and Bounding (TASK-5)") {
  const ScopedDebuggerState_t state_guard;

  static_assert(CONSOLE_DISPLAY_HEIGHT == 48, "Viewport height must be 48");
  static_assert(sizeof(g_console_display) == 7680,
                "g_console_display must be right-sized to 48 x 80 x 2 bytes");

  constexpr conchar_t kInputCanary = static_cast<conchar_t>(0x55AA);
  g_console_display[0][0] = kInputCanary;

  conchar_t line[CONSOLE_WIDTH] = {0};
  for (int i = 0; i < 100; ++i) {
    line[0] = static_cast<conchar_t>('0' + (i % 10));
    ConsoleDisplayPush(line);
  }

  CHECK(g_console_display_total == 47);
  CHECK(g_console_display[0][0] == kInputCanary);
  CHECK(g_console_display[CONSOLE_FIRST_LINE][0] ==
        static_cast<conchar_t>('9'));
  CHECK(g_console_display[CONSOLE_DISPLAY_HEIGHT - 1][0] ==
        static_cast<conchar_t>('3'));
}
