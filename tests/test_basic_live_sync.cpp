// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstdint>
#include <string>

#include "apple2/Apple2Types.h"
#include "apple2/Memory.h"
#include "core/BasicLiveSync.h"
#include "doctest.h"

extern eApple2Type g_apple2_type;

namespace {

constexpr size_t TEST_MEM_SIZE = 65536;
constexpr uint8_t ADDR_TXTTAB_L = 0x67;
constexpr uint8_t ADDR_TXTTAB_H = 0x68;
constexpr uint8_t ADDR_VARTAB_L = 0x69;
constexpr uint8_t ADDR_VARTAB_H = 0x6A;
constexpr uint8_t ADDR_ARYTAB_L = 0x6B;
constexpr uint8_t ADDR_ARYTAB_H = 0x6C;
constexpr uint8_t ADDR_STREND_L = 0x6D;
constexpr uint8_t ADDR_STREND_H = 0x6E;
constexpr uint8_t ADDR_FRETOP_L = 0x6F;
constexpr uint8_t ADDR_FRETOP_H = 0x70;
constexpr uint8_t ADDR_HIMEM_L = 0x73;
constexpr uint8_t ADDR_HIMEM_H = 0x74;
constexpr uint8_t ADDR_PRGEND_L = 0xAF;
constexpr uint8_t ADDR_PRGEND_H = 0xB0;

constexpr uint8_t VAL_TXTTAB_L = 0x01;
constexpr uint8_t VAL_TXTTAB_H = 0x08;
constexpr uint8_t VAL_HIMEM_L = 0x00;
constexpr uint8_t VAL_HIMEM_H = 0x96;

static std::array<uint8_t, TEST_MEM_SIZE> g_mock_ram{};

static void SetupMockMemory() {
  g_mock_ram.fill(0);
  mem = g_mock_ram.data();

  // Set default Applesoft zero page pointers
  // TXTTAB: $0801
  g_mock_ram.at(ADDR_TXTTAB_L) = VAL_TXTTAB_L;
  g_mock_ram.at(ADDR_TXTTAB_H) = VAL_TXTTAB_H;

  // HIMEM: $9600
  g_mock_ram.at(ADDR_HIMEM_L) = VAL_HIMEM_L;
  g_mock_ram.at(ADDR_HIMEM_H) = VAL_HIMEM_H;

  // PRGEND, VARTAB, ARYTAB: $0801
  g_mock_ram.at(ADDR_PRGEND_L) = VAL_TXTTAB_L;
  g_mock_ram.at(ADDR_PRGEND_H) = VAL_TXTTAB_H;
  g_mock_ram.at(ADDR_VARTAB_L) = VAL_TXTTAB_L;
  g_mock_ram.at(ADDR_VARTAB_H) = VAL_TXTTAB_H;
  g_mock_ram.at(ADDR_ARYTAB_L) = VAL_TXTTAB_L;
  g_mock_ram.at(ADDR_ARYTAB_H) = VAL_TXTTAB_H;
  g_mock_ram.at(ADDR_STREND_L) = VAL_TXTTAB_L;
  g_mock_ram.at(ADDR_STREND_H) = VAL_TXTTAB_H;
  g_mock_ram.at(ADDR_FRETOP_L) = VAL_HIMEM_L;
  g_mock_ram.at(ADDR_FRETOP_H) = VAL_HIMEM_H;
}

}  // namespace

TEST_CASE("BasicLiveSync: Explicit Line Mode Roundtrip") {
  SetupMockMemory();
  g_apple2_type = A2TYPE_APPLE2EENHANCED;

  std::string source =
      "10 HOME\n"
      "20 PRINT \"HELLO WORLD\"\n"
      "30 GOTO 10\n";

  bool ok = basic_sync_import_from_string(source, basic_line_mode_explicit);
  CHECK(ok);

  // Verify memory structure
  // First line at $0801:
  // Next ptr at 0x0801/0x0802
  // Line number at 0x0803/0x0804 (10 = 0x000A)
  // HOME token at 0x0805 ($97)
  // End of line at 0x0806 ($00)
  CHECK(g_mock_ram.at(0x0803) == 0x0A);
  CHECK(g_mock_ram.at(0x0804) == 0x00);
  CHECK(g_mock_ram.at(0x0805) == 0x97);
  CHECK(g_mock_ram.at(0x0806) == 0x00);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported == source);
}

TEST_CASE("BasicLiveSync: Positional Line Mode") {
  SetupMockMemory();
  g_apple2_type = A2TYPE_APPLE2EENHANCED;

  std::string source;
  constexpr int pad_first = 10;
  constexpr int pad_second_start = 11;
  constexpr int pad_second_end = 20;

  for (int i = 1; i < pad_first; ++i) {
    source += "\n";
  }
  source += "HOME\n";
  for (int i = pad_second_start; i < pad_second_end; ++i) {
    source += "\n";
  }
  source += "PRINT \"APPLE II\"\n";

  bool ok = basic_sync_import_from_string(source, basic_line_mode_positional);
  CHECK(ok);

  // Verify line 10 and line 20 created in RAM
  constexpr uint16_t addr_line1_num_l = 0x0803;
  constexpr uint16_t addr_line1_num_h = 0x0804;
  constexpr uint8_t shift_8 = 8;
  auto line1_num =
      static_cast<uint16_t>(g_mock_ram.at(addr_line1_num_l) |
                            (g_mock_ram.at(addr_line1_num_h) << shift_8));
  CHECK(line1_num == 10);

  std::string exported =
      basic_sync_export_to_string(basic_line_mode_positional);
  CHECK(exported == source);
}

TEST_CASE("BasicLiveSync: REM and Quoted String Keyword Protection") {
  SetupMockMemory();
  g_apple2_type = A2TYPE_APPLE2EENHANCED;

  // "PRINT" inside string and after REM should not be tokenized as $BA
  std::string source =
      "10 PRINT \"PRINT ME\"\n"
      "20 REM DO NOT PRINT THIS\n";

  bool ok = basic_sync_import_from_string(source, basic_line_mode_explicit);
  CHECK(ok);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported == source);
}

TEST_CASE("BasicLiveSync: Character Filtering & Hardware Casing") {
  SetupMockMemory();

  // Test Apple ][+ mode (uppercase only)
  g_apple2_type = A2TYPE_APPLE2PLUS;
  std::string source = "10 print \"hello world\"\n";
  bool ok = basic_sync_import_from_string(source, basic_line_mode_explicit);
  CHECK(ok);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported == "10 PRINT \"HELLO WORLD\"\n");

  // Test non-printable character stripping
  std::string dirty_source = "10 \x01\x02PRINT \"HI\x7F\"\n";
  ok = basic_sync_import_from_string(dirty_source, basic_line_mode_explicit);
  CHECK(ok);
  exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported == "10 PRINT \"HI\"\n");
}

TEST_CASE("BasicLiveSync: HIMEM Memory Overflow Protection") {
  SetupMockMemory();
  g_apple2_type = A2TYPE_APPLE2EENHANCED;

  // Set tight HIMEM ($0810) - only enough space for 1 line
  constexpr uint8_t tight_himem_l = 0x10;
  constexpr uint8_t tight_himem_h = 0x08;
  g_mock_ram.at(ADDR_HIMEM_L) = tight_himem_l;
  g_mock_ram.at(ADDR_HIMEM_H) = tight_himem_h;

  std::string source =
      "10 HOME\n"
      "20 PRINT \"FIRST LINE\"\n"
      "30 PRINT \"SECOND LINE WILL NOT FIT\"\n"
      "40 PRINT \"THIRD LINE\"\n";

  bool ok = basic_sync_import_from_string(source, basic_line_mode_explicit);
  CHECK(ok);

  constexpr uint8_t shift_8 = 8;
  auto prgend = static_cast<uint16_t>(
      g_mock_ram.at(ADDR_PRGEND_L) | (g_mock_ram.at(ADDR_PRGEND_H) << shift_8));
  CHECK(prgend < 0x0810);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported.find("10 HOME\n") != std::string::npos);
  CHECK(exported.find("THIRD LINE") == std::string::npos);
}

TEST_CASE("BasicLiveSync: Line Length Truncation") {
  SetupMockMemory();
  g_apple2_type = A2TYPE_APPLE2EENHANCED;

  constexpr size_t extra_chars = 300;
  std::string huge_line = "10 REM ";
  huge_line.append(extra_chars, 'A');
  huge_line += "\n";

  bool ok = basic_sync_import_from_string(huge_line, basic_line_mode_explicit);
  CHECK(ok);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported.length() <= 257);  // 255 chars + newline
}

TEST_CASE("BasicLiveSync: Math Tokens Longest-Prefix Matching") {
  SetupMockMemory();
  g_apple2_type = A2TYPE_APPLE2EENHANCED;

  std::string source = "10 PRINT ATN(1) + COS(X)\n";
  bool ok = basic_sync_import_from_string(source, basic_line_mode_explicit);
  CHECK(ok);

  // In RAM at $0801:
  // $0801/0802: next ptr
  // $0803/0804: line number 10 (0x0A, 0x00)
  // $0805: PRINT ($BA)
  // $0806: space ($20)
  // $0807: ATN ($E1) - NOT AT ($C5)
  constexpr uint16_t addr_print_token = 0x0805;
  constexpr uint16_t addr_atn_token = 0x0807;
  CHECK(g_mock_ram.at(addr_print_token) == 0xBA);
  CHECK(g_mock_ram.at(addr_atn_token) == 0xE1);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported == source);
}
