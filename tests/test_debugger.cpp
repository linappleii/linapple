// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>
#include <string>

#include "Debugger/Debugger_Assembler.h"
#include "Debugger/Debugger_Commands.h"
#include "Debugger/Debugger_Parser.h"
#include "Debugger/Debugger_Range.h"
#include "Debugger/Debugger_Symbols.h"
#include "Debugger/Debugger_Types.h"
#include "doctest.h"

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
  }

  SUBCASE("FindTokenOrAlphaNumeric finds next token or alphanumeric boundary") {
    ArgToken_e token = NO_TOKEN;
    const char* rest =
        FindTokenOrAlphaNumeric("LDA #$01", g_tokens, NUM_TOKENS, &token);
    CHECK(rest != nullptr);
  }
}

TEST_CASE("Debugger Parser: Command Line and Arguments Parsing") {
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
}

TEST_CASE("Debugger Assembler: Mnemonic Hashing and Opcode Identification") {
  SUBCASE("AssemblerHashMnemonic computes unique non-zero hash values") {
    uint32_t hash_lda = AssemblerHashMnemonic("LDA");
    uint32_t hash_sta = AssemblerHashMnemonic("STA");
    uint32_t hash_jmp = AssemblerHashMnemonic("JMP");
    uint32_t hash_rts = AssemblerHashMnemonic("RTS");
    uint32_t hash_nop = AssemblerHashMnemonic("NOP");

    CHECK(hash_lda != 0);
    CHECK(hash_sta != 0);
    CHECK(hash_jmp != 0);
    CHECK(hash_rts != 0);
    CHECK(hash_nop != 0);
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
