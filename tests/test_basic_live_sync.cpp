// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/Apple2Types.h"
#include "apple2/Memory.h"
#include "core/BasicLiveSync.h"
#include "doctest.h"

extern Apple2Type current_apple2_type;

namespace {

constexpr size_t test_mem_size = 65536;
constexpr uint8_t addr_txttab_l = 0x67;
constexpr uint8_t addr_txttab_h = 0x68;
constexpr uint8_t addr_vartab_l = 0x69;
constexpr uint8_t addr_vartab_h = 0x6A;
constexpr uint8_t addr_arytab_l = 0x6B;
constexpr uint8_t addr_arytab_h = 0x6C;
constexpr uint8_t addr_strend_l = 0x6D;
constexpr uint8_t addr_strend_h = 0x6E;
constexpr uint8_t addr_fretop_l = 0x6F;
constexpr uint8_t addr_fretop_h = 0x70;
constexpr uint8_t addr_himem_l = 0x73;
constexpr uint8_t addr_himem_h = 0x74;
constexpr uint8_t addr_prgend_l = 0xAF;
constexpr uint8_t addr_prgend_h = 0xB0;

constexpr uint8_t val_txttab_l = 0x01;
constexpr uint8_t val_txttab_h = 0x08;
constexpr uint8_t val_himem_l = 0x00;
constexpr uint8_t val_himem_h = 0x96;

static std::array<uint8_t, test_mem_size> mock_ram{};

struct ScopedMemoryContext_t {
  uint8_t* original_mem{mem};
  Apple2Type original_type{current_apple2_type};

  ScopedMemoryContext_t() = default;

  ~ScopedMemoryContext_t() {
    mem = original_mem;
    current_apple2_type = original_type;
  }

  ScopedMemoryContext_t(const ScopedMemoryContext_t&) = delete;
  auto operator=(const ScopedMemoryContext_t&)
      -> ScopedMemoryContext_t& = delete;
  ScopedMemoryContext_t(ScopedMemoryContext_t&&) = delete;
  auto operator=(ScopedMemoryContext_t&&) -> ScopedMemoryContext_t& = delete;
};

static auto setup_mock_memory() -> void {
  mock_ram.fill(0);
  mem = mock_ram.data();

  // Set default Applesoft zero page pointers
  // TXTTAB: $0801
  mock_ram.at(addr_txttab_l) = val_txttab_l;
  mock_ram.at(addr_txttab_h) = val_txttab_h;

  // HIMEM: $9600
  mock_ram.at(addr_himem_l) = val_himem_l;
  mock_ram.at(addr_himem_h) = val_himem_h;

  // PRGEND, VARTAB, ARYTAB: $0801
  mock_ram.at(addr_prgend_l) = val_txttab_l;
  mock_ram.at(addr_prgend_h) = val_txttab_h;
  mock_ram.at(addr_vartab_l) = val_txttab_l;
  mock_ram.at(addr_vartab_h) = val_txttab_h;
  mock_ram.at(addr_arytab_l) = val_txttab_l;
  mock_ram.at(addr_arytab_h) = val_txttab_h;
  mock_ram.at(addr_strend_l) = val_txttab_l;
  mock_ram.at(addr_strend_h) = val_txttab_h;
  mock_ram.at(addr_fretop_l) = val_himem_l;
  mock_ram.at(addr_fretop_h) = val_himem_h;
}

}  // namespace

TEST_CASE("BasicLiveSync: Explicit Line Mode Roundtrip") {
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();
  current_apple2_type = A2TYPE_APPLE2EENHANCED;

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
  CHECK(mock_ram.at(0x0803) == 0x0A);
  CHECK(mock_ram.at(0x0804) == 0x00);
  CHECK(mock_ram.at(0x0805) == 0x97);
  CHECK(mock_ram.at(0x0806) == 0x00);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported == source);
}

TEST_CASE("BasicLiveSync: Positional Line Mode") {
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();
  current_apple2_type = A2TYPE_APPLE2EENHANCED;

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
      static_cast<uint16_t>(mock_ram.at(addr_line1_num_l) |
                            (mock_ram.at(addr_line1_num_h) << shift_8));
  CHECK(line1_num == 10);

  std::string exported =
      basic_sync_export_to_string(basic_line_mode_positional);
  CHECK(exported == source);
}

TEST_CASE("BasicLiveSync: REM and Quoted String Keyword Protection") {
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();
  current_apple2_type = A2TYPE_APPLE2EENHANCED;

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
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();

  // Test Apple ][+ mode (uppercase only)
  current_apple2_type = A2TYPE_APPLE2PLUS;
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
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();
  current_apple2_type = A2TYPE_APPLE2EENHANCED;

  // Set tight HIMEM ($0810) - only enough space for 1 line
  constexpr uint8_t tight_himem_l = 0x10;
  constexpr uint8_t tight_himem_h = 0x08;
  mock_ram.at(addr_himem_l) = tight_himem_l;
  mock_ram.at(addr_himem_h) = tight_himem_h;

  std::string source =
      "10 HOME\n"
      "20 PRINT \"FIRST LINE\"\n"
      "30 PRINT \"SECOND LINE WILL NOT FIT\"\n"
      "40 PRINT \"THIRD LINE\"\n";

  bool ok = basic_sync_import_from_string(source, basic_line_mode_explicit);
  CHECK(ok);

  constexpr uint8_t shift_8 = 8;
  auto prgend = static_cast<uint16_t>(mock_ram.at(addr_prgend_l) |
                                      (mock_ram.at(addr_prgend_h) << shift_8));
  CHECK(prgend < 0x0810);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported.find("10 HOME\n") != std::string::npos);
  CHECK(exported.find("THIRD LINE") == std::string::npos);
}

TEST_CASE("BasicLiveSync: Line Length Truncation") {
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();
  current_apple2_type = A2TYPE_APPLE2EENHANCED;

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
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();
  current_apple2_type = A2TYPE_APPLE2EENHANCED;

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
  CHECK(mock_ram.at(addr_print_token) == 0xBA);
  CHECK(mock_ram.at(addr_atn_token) == 0xE1);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported == source);
}

TEST_CASE("BasicLiveSync: Raw Buffer Import Overload") {
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();
  current_apple2_type = A2TYPE_APPLE2EENHANCED;

  const char* raw_source = "10 PRINT \"BUFFER TEST\"\n";
  size_t raw_len = std::strlen(raw_source);

  // Null pointer safety
  CHECK_FALSE(
      basic_sync_import_from_string(nullptr, 10, basic_line_mode_explicit));

  // Empty buffer
  CHECK(basic_sync_import_from_string("", 0, basic_line_mode_explicit));
  CHECK(basic_sync_export_to_string(basic_line_mode_explicit).empty());

  // Buffer without trailing newline
  const char* no_newline = "10 PRINT \"NO NEWLINE\"";
  CHECK(basic_sync_import_from_string(no_newline, std::strlen(no_newline),
                                      basic_line_mode_explicit));
  CHECK(basic_sync_export_to_string(basic_line_mode_explicit) ==
        "10 PRINT \"NO NEWLINE\"\n");

  // Valid buffer import
  bool ok = basic_sync_import_from_string(raw_source, raw_len,
                                          basic_line_mode_explicit);
  CHECK(ok);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported == raw_source);
}

TEST_CASE("BasicLiveSync: Line Number Overflow Safety") {
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();
  current_apple2_type = A2TYPE_APPLE2EENHANCED;

  // Extremely long line number must clamp to 63999 and never throw out_of_range
  std::string overflow_source =
      "999999999999999999999999999999 PRINT \"SAFE\"\n";
  bool ok =
      basic_sync_import_from_string(overflow_source, basic_line_mode_explicit);
  CHECK(ok);

  std::string exported = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(exported == "63999 PRINT \"SAFE\"\n");
}

TEST_CASE("BasicLiveSync: Lifecycle and Configuration") {
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();

  // Ensure starting state is inactive
  basic_sync_shutdown();
  CHECK_FALSE(basic_sync_is_active());

  // Null or empty path does not activate
  basic_sync_init(nullptr, basic_line_mode_explicit);
  CHECK_FALSE(basic_sync_is_active());

  basic_sync_init("", basic_line_mode_positional);
  CHECK_FALSE(basic_sync_is_active());

  // Valid init
  const char* test_file = "/tmp/linapple_sync_lifecycle_test.bas";
  const char* test_file2 = "/tmp/linapple_sync_lifecycle_test2.bas";
  std::remove(test_file);
  std::remove(test_file2);

  basic_sync_init(test_file, basic_line_mode_positional);
  CHECK(basic_sync_is_active());

  const auto& config = basic_sync_get_config();
  CHECK(config.enabled);
  CHECK(config.line_mode == basic_line_mode_positional);
  CHECK(config.file_path == test_file);

  // Re-initialization without prior shutdown
  basic_sync_init(test_file2, basic_line_mode_explicit);
  CHECK(basic_sync_is_active());
  CHECK(basic_sync_get_config().file_path == test_file2);
  CHECK(basic_sync_get_config().line_mode == basic_line_mode_explicit);

  // Idempotent shutdown
  basic_sync_shutdown();
  basic_sync_shutdown();
  CHECK_FALSE(basic_sync_is_active());
  CHECK(basic_sync_get_config().file_path.empty());

  std::remove(test_file);
  std::remove(test_file2);
}

TEST_CASE("BasicLiveSync: File Export and Import Roundtrip") {
  ScopedMemoryContext_t mem_guard;
  setup_mock_memory();
  current_apple2_type = A2TYPE_APPLE2EENHANCED;

  const char* test_file = "/tmp/linapple_sync_file_test.bas";
  std::remove(test_file);

  // Export/import with no active configuration fails safely
  basic_sync_shutdown();
  CHECK_FALSE(basic_sync_export_file());
  CHECK_FALSE(basic_sync_import_file());

  // Initialize sync
  basic_sync_init(test_file, basic_line_mode_explicit);
  CHECK(basic_sync_is_active());

  // Inject program into RAM
  std::string source = "10 PRINT \"FILE TEST\"\n20 GOTO 10\n";
  bool ok = basic_sync_import_from_string(source, basic_line_mode_explicit);
  REQUIRE(ok);

  // Export to file
  bool export_ok = basic_sync_export_file();
  CHECK(export_ok);

  // Exercise basic_sync_update frame throttling for auto-export
  for (int frame = 0; frame < 20; ++frame) {
    basic_sync_update();
  }

  // Clear RAM mock
  setup_mock_memory();
  std::string empty_exported =
      basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(empty_exported.empty());

  // Import from file
  bool import_ok = basic_sync_import_file();
  CHECK(import_ok);

  std::string restored = basic_sync_export_to_string(basic_line_mode_explicit);
  CHECK(restored == source);

  // Cleanup
  basic_sync_shutdown();
  std::remove(test_file);
}
