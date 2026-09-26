// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/ProgramLoader.h"
#include "core/Util_Endian.h"
#include "core/Util_Path.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr uint16_t ADDR_0800 = 0x0800;
constexpr uint16_t ADDR_1000 = 0x1000;
constexpr uint16_t ADDR_BFFF = 0xBFFF;
constexpr uint16_t ADDR_C000 = 0xC000;
constexpr uint16_t ADDR_CFFF = 0xCFFF;
constexpr uint16_t ADDR_D000 = 0xD000;
constexpr uint16_t ADDR_FFFF = 0xFFFF;
constexpr uint32_t PRG_MAGIC_VAL = 0x214C470A;
constexpr uint8_t SENTINEL_BYTE = 0x5A;
constexpr uint8_t DIRTY_BYTE = 0xFF;
constexpr uint16_t DOS33_BLOAD_ADDR = 0xAA72;
constexpr uint16_t DOS33_BLOAD_LEN = 0xAA60;

// Test helper: Set memory and registers to known sentinel states
void setup_clean_machine(uint16_t initial_pc = 0x1234) {
  mem_initialize();
  std::memset(mem, SENTINEL_BYTE, MEMORY_64K);
  std::memset(memdirty, 0, NUM_PAGES_64K);
  cpu_initialize();
  auto* regs = cpu_get_registers();
  if (regs != nullptr) {
    regs->pc = initial_pc;
  }
}

// Helper: Assert memory has not been altered anywhere
void assert_memory_unmodified() {
  for (size_t i = 0; i < MEMORY_64K; ++i) {
    INFO("Memory modified at address: " << i);
    REQUIRE(mem[i] == SENTINEL_BYTE);
  }
}

// Helper: Assert memdirty has not been modified
void assert_memdirty_unmodified() {
  for (size_t i = 0; i < NUM_PAGES_48K; ++i) {
    INFO("memdirty modified at page: " << i);
    REQUIRE(memdirty[i] == 0);
  }
}

// Helper: Assert all 48K pages marked dirty on successful load
void assert_memdirty_marked() {
  for (size_t i = 0; i < NUM_PAGES_48K; ++i) {
    INFO("memdirty not marked at page: " << i);
    REQUIRE(memdirty[i] == DIRTY_BYTE);
  }
}

}  // namespace

// PRG-01: Null and Invalid Path Arguments
TEST_CASE("ProgramLoader: [PRG-01] Null and Invalid Path Arguments") {
  setup_clean_machine();
  CHECK(program_loader_try_load(nullptr) == program_load_file_error);
  CHECK(program_loader_try_load("") == program_load_file_error);
  assert_memory_unmodified();
  assert_memdirty_unmodified();
  CHECK(cpu_get_registers()->pc == 0x1234);
}

// PRG-02: Missing File Handling
TEST_CASE("ProgramLoader: [PRG-02] Missing File (FILE_ERROR)") {
  setup_clean_machine();
  CHECK(program_loader_try_load("nonexistent_file_linapple.apl") ==
        program_load_file_error);
  assert_memory_unmodified();
  assert_memdirty_unmodified();
  CHECK(cpu_get_registers()->pc == 0x1234);
}

// PRG-03: Zero-Byte and Sub-Header Short Files
TEST_CASE("ProgramLoader: [PRG-03] Short Files (< 9 bytes) & Zero-Byte Files") {
  setup_clean_machine();
  TestFixtures::ScopedTempFile_t empty_file(".bin");
  // 0-byte file
  CHECK(program_loader_try_load(empty_file.c_str()) ==
        program_load_not_a_program);

  // Files of size 1 to 8 bytes (all < 9 bytes)
  for (size_t sz = 1; sz <= 8; ++sz) {
    TestFixtures::ScopedTempFile_t short_file(".bin");
    std::vector<uint8_t> dummy(sz, 0xEE);
    {
      FilePtr_t f(fopen(short_file.c_str(), "wb"), fclose);
      fwrite(dummy.data(), 1, sz, f.get());
    }
    CHECK(program_loader_try_load(short_file.c_str()) ==
          program_load_not_a_program);
  }

  assert_memory_unmodified();
  assert_memdirty_unmodified();
  CHECK(cpu_get_registers()->pc == 0x1234);
}

// PRG-04: Non-Program File Rejection (Preserves Machine State)
TEST_CASE("ProgramLoader: [PRG-04] DSK Image Detection Failure") {
  setup_clean_machine();
  TestFixtures::ScopedTempFile_t dsk_file(".dsk");
  {
    FilePtr_t f(fopen(dsk_file.c_str(), "wb"), fclose);
    std::vector<uint8_t> zeroes(143360, 0);
    fwrite(zeroes.data(), 1, zeroes.size(), f.get());
  }

  CHECK(program_loader_try_load(dsk_file.c_str()) ==
        program_load_not_a_program);
  assert_memory_unmodified();
  assert_memdirty_unmodified();
  CHECK(cpu_get_registers()->pc == 0x1234);
}

// PRG-05: Exact Boundary Testing at $C000 (RAM vs IO/ROM Boundary)
TEST_CASE("ProgramLoader: [PRG-05] Exact $C000 Boundary Tests") {
  // Case A: Last valid byte in RAM ($BFFF + 1 byte = ends at $C000) -> OK
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t f_valid(".apl");
    uint16_t addr = ADDR_BFFF;
    uint16_t len = 1;
    uint8_t payload = 0x42;
    {
      FilePtr_t f(fopen(f_valid.c_str(), "wb"), fclose);
      fwrite(&addr, 1, 2, f.get());
      fwrite(&len, 1, 2, f.get());
      fwrite(&payload, 1, 1, f.get());
    }
    CHECK(program_loader_try_load(f_valid.c_str()) == program_load_ok);
    CHECK(mem[ADDR_BFFF] == 0x42);
    CHECK(mem[0xBFFE] == SENTINEL_BYTE);     // Guard before
    CHECK(mem[ADDR_C000] == SENTINEL_BYTE);  // Guard at $C000 untouched
    CHECK(cpu_get_registers()->pc == ADDR_BFFF);
    assert_memdirty_marked();
  }

  // Case B: 1-byte overflow into I/O ($BFFF + 2 bytes = ends at $C001) ->
  // INVALID
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t f_over(".apl");
    uint16_t addr = ADDR_BFFF;
    uint16_t len = 2;
    std::array<uint8_t, 2> payload = {0x11, 0x22};
    {
      FilePtr_t f(fopen(f_over.c_str(), "wb"), fclose);
      fwrite(&addr, 1, 2, f.get());
      fwrite(&len, 1, 2, f.get());
      fwrite(payload.data(), 1, payload.size(), f.get());
    }
    CHECK(program_loader_try_load(f_over.c_str()) == program_load_invalid);
    assert_memory_unmodified();
    assert_memdirty_unmodified();
    CHECK(cpu_get_registers()->pc == 0x1234);
  }

  // Case C: Start exactly at $C000 -> INVALID
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t f_c000(".apl");
    uint16_t addr = ADDR_C000;
    uint16_t len = 1;
    uint8_t payload = 0xAA;
    {
      FilePtr_t f(fopen(f_c000.c_str(), "wb"), fclose);
      fwrite(&addr, 1, 2, f.get());
      fwrite(&len, 1, 2, f.get());
      fwrite(&payload, 1, 1, f.get());
    }
    CHECK(program_loader_try_load(f_c000.c_str()) == program_load_invalid);
    assert_memory_unmodified();
  }

  // Case D: Start at $CFFF -> INVALID
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t f_cfff(".apl");
    uint16_t addr = ADDR_CFFF;
    uint16_t len = 1;
    uint8_t payload = 0xAA;
    {
      FilePtr_t f(fopen(f_cfff.c_str(), "wb"), fclose);
      fwrite(&addr, 1, 2, f.get());
      fwrite(&len, 1, 2, f.get());
      fwrite(&payload, 1, 1, f.get());
    }
    CHECK(program_loader_try_load(f_cfff.c_str()) == program_load_invalid);
    assert_memory_unmodified();
  }

  // Case E: Start at $D000 (Language Card / ROM) -> INVALID
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t f_d000(".apl");
    uint16_t addr = ADDR_D000;
    uint16_t len = 1;
    uint8_t payload = 0xAA;
    {
      FilePtr_t f(fopen(f_d000.c_str(), "wb"), fclose);
      fwrite(&addr, 1, 2, f.get());
      fwrite(&len, 1, 2, f.get());
      fwrite(&payload, 1, 1, f.get());
    }
    CHECK(program_loader_try_load(f_d000.c_str()) == program_load_invalid);
    assert_memory_unmodified();
  }

  // Case F: 16-bit Integer Overflow / Wraparound ($FFFF + 2 bytes) -> INVALID
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t f_wrap(".apl");
    uint16_t addr = ADDR_FFFF;
    uint16_t len = 2;
    std::array<uint8_t, 2> payload = {0x01, 0x02};
    {
      FilePtr_t f(fopen(f_wrap.c_str(), "wb"), fclose);
      fwrite(&addr, 1, 2, f.get());
      fwrite(&len, 1, 2, f.get());
      fwrite(payload.data(), 1, payload.size(), f.get());
    }
    CHECK(program_loader_try_load(f_wrap.c_str()) == program_load_invalid);
    assert_memory_unmodified();
  }
}

// PRG-06: PRG Header Magic Validation
TEST_CASE("ProgramLoader: [PRG-06] PRG Magic Validation") {
  setup_clean_machine();

  // Sub-case: Corrupted last magic byte
  {
    TestFixtures::ScopedTempFile_t bad_magic_file(".prg");
    uint32_t bad_magic = 0x214C4700;  // 0x0A replaced with 0x00
    uint8_t pad1 = 0;
    uint16_t addr = ADDR_1000;
    uint16_t word_len = 1;
    std::array<uint8_t, 128 - 9> pad{};
    std::array<uint8_t, 2> payload = {0x11, 0x22};
    {
      FilePtr_t f(fopen(bad_magic_file.c_str(), "wb"), fclose);
      fwrite(&bad_magic, 1, 4, f.get());
      fwrite(&pad1, 1, 1, f.get());
      fwrite(&addr, 1, 2, f.get());
      fwrite(&word_len, 1, 2, f.get());
      fwrite(pad.data(), 1, pad.size(), f.get());
      fwrite(payload.data(), 1, payload.size(), f.get());
    }
    CHECK(program_loader_try_load(bad_magic_file.c_str()) ==
          program_load_not_a_program);
    assert_memory_unmodified();
  }

  // Sub-case: Big-endian reversed magic
  {
    TestFixtures::ScopedTempFile_t be_magic_file(".prg");
    uint32_t be_magic = 0x0A474C21;
    uint8_t pad1 = 0;
    uint16_t addr = ADDR_1000;
    uint16_t word_len = 1;
    std::array<uint8_t, 128 - 9> pad{};
    std::array<uint8_t, 2> payload = {0x11, 0x22};
    {
      FilePtr_t f(fopen(be_magic_file.c_str(), "wb"), fclose);
      fwrite(&be_magic, 1, 4, f.get());
      fwrite(&pad1, 1, 1, f.get());
      fwrite(&addr, 1, 2, f.get());
      fwrite(&word_len, 1, 2, f.get());
      fwrite(pad.data(), 1, pad.size(), f.get());
      fwrite(payload.data(), 1, payload.size(), f.get());
    }
    CHECK(program_loader_try_load(be_magic_file.c_str()) ==
          program_load_not_a_program);
    assert_memory_unmodified();
  }
}

// PRG-07: PRG Word Length Calculation & Multiplicative Overflow
TEST_CASE("ProgramLoader: [PRG-07] PRG Word Length Arithmetic & Overflow") {
  // Case A: word_len = 0x8000 -> 0x10000 bytes (exceeds $C000) -> INVALID
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t f_over(".prg");
    uint32_t magic = PRG_MAGIC_VAL;
    uint8_t pad1 = 0;
    uint16_t addr = 0x0000;
    uint16_t word_len = 0x8000;  // 32768 words = 65536 bytes
    std::array<uint8_t, 128 - 9> pad{};
    {
      FilePtr_t f(fopen(f_over.c_str(), "wb"), fclose);
      fwrite(&magic, 1, 4, f.get());
      fwrite(&pad1, 1, 1, f.get());
      fwrite(&addr, 1, 2, f.get());
      fwrite(&word_len, 1, 2, f.get());
      fwrite(pad.data(), 1, pad.size(), f.get());
    }
    CHECK(program_loader_try_load(f_over.c_str()) == program_load_invalid);
    assert_memory_unmodified();
  }

  // Case B: word_len = 0xFFFF -> 131070 bytes -> INVALID
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t f_max(".prg");
    uint32_t magic = PRG_MAGIC_VAL;
    uint8_t pad1 = 0;
    uint16_t addr = 0x0000;
    uint16_t word_len = 0xFFFF;
    std::array<uint8_t, 128 - 9> pad{};
    {
      FilePtr_t f(fopen(f_max.c_str(), "wb"), fclose);
      fwrite(&magic, 1, 4, f.get());
      fwrite(&pad1, 1, 1, f.get());
      fwrite(&addr, 1, 2, f.get());
      fwrite(&word_len, 1, 2, f.get());
      fwrite(pad.data(), 1, pad.size(), f.get());
    }
    CHECK(program_loader_try_load(f_max.c_str()) == program_load_invalid);
    assert_memory_unmodified();
  }
}

// PRG-08: Partial Read & Truncated Payload (Guarantees Memory Atomicity)
TEST_CASE("ProgramLoader: [PRG-08] Truncated File Preserves Memory Atomicity") {
  // Case A: APL header claims 200 bytes, but file only has 10 bytes payload
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t trunc_apl(".apl");
    uint16_t addr = ADDR_1000;
    uint16_t len = 200;
    std::array<uint8_t, 10> partial_payload{};
    partial_payload.fill(0xCC);
    {
      FilePtr_t f(fopen(trunc_apl.c_str(), "wb"), fclose);
      fwrite(&addr, 1, 2, f.get());
      fwrite(&len, 1, 2, f.get());
      fwrite(partial_payload.data(), 1, partial_payload.size(), f.get());
    }
    CHECK(program_loader_try_load(trunc_apl.c_str()) ==
          program_load_not_a_program);
    assert_memory_unmodified();
    assert_memdirty_unmodified();
    CHECK(cpu_get_registers()->pc == 0x1234);
  }

  // Case B: PRG header claims 16 words (32 bytes), but payload truncated to 4
  // bytes
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t trunc_prg(".prg");
    uint32_t magic = PRG_MAGIC_VAL;
    uint8_t pad1 = 0;
    uint16_t addr = ADDR_1000;
    uint16_t word_len = 16;
    std::array<uint8_t, 128 - 9> pad{};
    std::array<uint8_t, 4> partial_payload = {0x1, 0x2, 0x3, 0x4};
    {
      FilePtr_t f(fopen(trunc_prg.c_str(), "wb"), fclose);
      fwrite(&magic, 1, 4, f.get());
      fwrite(&pad1, 1, 1, f.get());
      fwrite(&addr, 1, 2, f.get());
      fwrite(&word_len, 1, 2, f.get());
      fwrite(pad.data(), 1, pad.size(), f.get());
      fwrite(partial_payload.data(), 1, partial_payload.size(), f.get());
    }
    CHECK(program_loader_try_load(trunc_prg.c_str()) == program_load_invalid);
    assert_memory_unmodified();
    assert_memdirty_unmodified();
    CHECK(cpu_get_registers()->pc == 0x1234);
  }
}

// PRG-09: APL Sector-Padded File Recognition & Over-read Guard
TEST_CASE("ProgramLoader: [PRG-09] 256-Byte Sector Padded APL File") {
  setup_clean_machine();
  TestFixtures::ScopedTempFile_t padded_apl(".apl");

  uint16_t addr = ADDR_0800;
  uint16_t len = 10;  // 10 bytes payload
  std::vector<uint8_t> payload(10);
  for (size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<uint8_t>(0xA0 + i);
  }
  std::vector<uint8_t> padding(242, 0xEE);

  {
    FilePtr_t f(fopen(padded_apl.c_str(), "wb"), fclose);
    fwrite(&addr, 1, 2, f.get());
    fwrite(&len, 1, 2, f.get());
    fwrite(payload.data(), 1, payload.size(), f.get());
    fwrite(padding.data(), 1, padding.size(), f.get());
  }

  ProgramInfo_t info{};
  CHECK(program_loader_try_load(padded_apl.c_str(), &info) == program_load_ok);
  CHECK(info.format == ProgramFormat_t::apl);
  CHECK(info.load_addr == ADDR_0800);
  CHECK(info.length == 10);
  CHECK(cpu_get_registers()->pc == ADDR_0800);
  CHECK(memcmp(mem + ADDR_0800, payload.data(), payload.size()) == 0);

  // Assert guard bands:
  CHECK(mem[ADDR_0800 - 1] == SENTINEL_BYTE);
  // Crucial: Padding bytes (0xEE) must NOT be loaded into memory past ADDR_0800
  // + 10!
  CHECK(mem[ADDR_0800 + 10] == SENTINEL_BYTE);
  assert_memdirty_marked();
}

// PRG-10: Zero-Length Payloads
TEST_CASE("ProgramLoader: [PRG-10] Zero-Length Payloads (len = 0)") {
  // APL with len = 0 (file size = 4 bytes)
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t empty_apl(".apl");
    uint16_t addr = ADDR_1000;
    uint16_t len = 0;
    {
      FilePtr_t f(fopen(empty_apl.c_str(), "wb"), fclose);
      fwrite(&addr, 1, 2, f.get());
      fwrite(&len, 1, 2, f.get());
    }
    CHECK(program_loader_try_load(empty_apl.c_str()) == program_load_invalid);
    assert_memory_unmodified();
  }

  // PRG with word_len = 0 (header 128 bytes, 0 payload)
  {
    setup_clean_machine();
    TestFixtures::ScopedTempFile_t empty_prg(".prg");
    uint32_t magic = PRG_MAGIC_VAL;
    uint8_t pad1 = 0;
    uint16_t addr = ADDR_1000;
    uint16_t word_len = 0;
    std::array<uint8_t, 128 - 9> pad{};
    {
      FilePtr_t f(fopen(empty_prg.c_str(), "wb"), fclose);
      fwrite(&magic, 1, 4, f.get());
      fwrite(&pad1, 1, 1, f.get());
      fwrite(&addr, 1, 2, f.get());
      fwrite(&word_len, 1, 2, f.get());
      fwrite(pad.data(), 1, pad.size(), f.get());
    }
    CHECK(program_loader_try_load(empty_prg.c_str()) == program_load_invalid);
    assert_memory_unmodified();
    assert_memdirty_unmodified();
    CHECK(cpu_get_registers()->pc == 0x1234);
  }
}

// PRG-11: Full APL Loading & DOS 3.3 Vectors
TEST_CASE("ProgramLoader: [PRG-11] Full APL Loading & Side-Effects") {
  setup_clean_machine();
  TestFixtures::ScopedTempFile_t test_apl(".apl");
  std::array<uint8_t, 16> data{};
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>(i + 1);
  }

  {
    FilePtr_t f(fopen(test_apl.c_str(), "wb"), fclose);
    uint16_t addr = ADDR_0800;
    uint16_t len = static_cast<uint16_t>(data.size());
    fwrite(&addr, 1, 2, f.get());
    fwrite(&len, 1, 2, f.get());
    fwrite(data.data(), 1, data.size(), f.get());
  }

  ProgramInfo_t info{};
  CHECK(program_loader_try_load(test_apl.c_str(), &info) == program_load_ok);
  CHECK(info.format == ProgramFormat_t::apl);
  CHECK(info.load_addr == ADDR_0800);
  CHECK(info.length == data.size());
  CHECK(cpu_get_registers()->pc == ADDR_0800);
  CHECK(memcmp(mem + ADDR_0800, data.data(), data.size()) == 0);
  CHECK(mem[ADDR_0800 - 1] == SENTINEL_BYTE);
  CHECK(mem[ADDR_0800 + data.size()] == SENTINEL_BYTE);
  assert_memdirty_marked();

  // Verify DOS 3.3 vectors
  CHECK(read_u16_le(&mem[DOS33_BLOAD_ADDR]) == ADDR_0800);
  CHECK(read_u16_le(&mem[DOS33_BLOAD_LEN]) == data.size());
}

// PRG-12: Full PRG Loading & DOS 3.3 Vectors
TEST_CASE("ProgramLoader: [PRG-12] Full PRG Loading & Side-Effects") {
  setup_clean_machine();
  TestFixtures::ScopedTempFile_t test_prg(".prg");
  std::array<uint8_t, 16> data{};
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>(0xAA ^ i);
  }

  {
    FilePtr_t f(fopen(test_prg.c_str(), "wb"), fclose);
    uint32_t magic = PRG_MAGIC_VAL;
    uint8_t pad1 = 0;
    uint16_t addr = ADDR_1000;
    uint16_t word_len = 8;  // 16 bytes
    fwrite(&magic, 1, 4, f.get());
    fwrite(&pad1, 1, 1, f.get());
    fwrite(&addr, 1, 2, f.get());
    fwrite(&word_len, 1, 2, f.get());
    std::array<uint8_t, 128 - 9> pad{};
    fwrite(pad.data(), 1, pad.size(), f.get());
    fwrite(data.data(), 1, data.size(), f.get());
  }

  ProgramInfo_t info{};
  CHECK(program_loader_try_load(test_prg.c_str(), &info) == program_load_ok);
  CHECK(info.format == ProgramFormat_t::prg);
  CHECK(info.load_addr == ADDR_1000);
  CHECK(info.length == data.size());
  CHECK(cpu_get_registers()->pc == ADDR_1000);
  CHECK(memcmp(mem + ADDR_1000, data.data(), data.size()) == 0);
  CHECK(mem[ADDR_1000 - 1] == SENTINEL_BYTE);
  CHECK(mem[ADDR_1000 + data.size()] == SENTINEL_BYTE);
  assert_memdirty_marked();

  // Verify DOS 3.3 vectors
  CHECK(read_u16_le(&mem[DOS33_BLOAD_ADDR]) == ADDR_1000);
  CHECK(read_u16_le(&mem[DOS33_BLOAD_LEN]) == data.size());
}

// PRG-13: Enum & Operator Interop Verification
TEST_CASE("ProgramLoader: [PRG-13] Result Enum Properties and Interop") {
  static_assert(sizeof(ProgramLoadResult_t) == 1,
                "ProgramLoadResult_t must be 1 byte");
  CHECK(ProgramLoadResult_t::ok == program_load_ok);
  CHECK(ProgramLoadResult_t::not_a_program == program_load_not_a_program);
  CHECK(ProgramLoadResult_t::file_error == program_load_file_error);
  CHECK(ProgramLoadResult_t::invalid == program_load_invalid);

  CHECK(program_load_ok == 0);
  CHECK(0 == program_load_ok);
  CHECK(program_load_not_a_program == 1);
  CHECK(1 == program_load_not_a_program);
  CHECK(program_load_file_error == 2);
  CHECK(2 == program_load_file_error);
  CHECK(program_load_invalid == 3);
  CHECK(3 == program_load_invalid);

  CHECK(program_load_ok != 1);
  CHECK(1 != program_load_ok);
}

// PRG-14: Pure Header Inspection API (No Side Effects)
TEST_CASE(
    "ProgramLoader: [PRG-14] Pure Stream Inspection (Zero Side-Effects)") {
  setup_clean_machine();
  TestFixtures::ScopedTempFile_t test_apl(".apl");
  uint16_t addr = 0x2000;
  uint16_t len = 64;
  std::vector<uint8_t> payload(64, 0x33);
  {
    FilePtr_t f(fopen(test_apl.c_str(), "wb"), fclose);
    fwrite(&addr, 1, 2, f.get());
    fwrite(&len, 1, 2, f.get());
    fwrite(payload.data(), 1, payload.size(), f.get());
  }

  ProgramInfo_t info{};
  {
    FilePtr_t f(fopen(test_apl.c_str(), "rb"), fclose);
    REQUIRE(f != nullptr);
    CHECK(program_loader_inspect(f.get(), &info) == program_load_ok);
    CHECK(info.format == ProgramFormat_t::apl);
    CHECK(info.load_addr == 0x2000);
    CHECK(info.length == 64);
    CHECK(info.offset == 4);
  }

  // Inspection must NOT mutate virtual machine state
  assert_memory_unmodified();
  assert_memdirty_unmodified();
  CHECK(cpu_get_registers()->pc == 0x1234);
}

// PRG-15: Raw Binary Program Loading
TEST_CASE("ProgramLoader: [PRG-15] Raw Binary Loading") {
  setup_clean_machine();
  TestFixtures::ScopedTempFile_t test_bin(".bin");
  std::vector<uint8_t> bin_data(256, 0x77);
  {
    FilePtr_t f(fopen(test_bin.c_str(), "wb"), fclose);
    fwrite(bin_data.data(), 1, bin_data.size(), f.get());
  }

  ProgramInfo_t info{};
  CHECK(program_loader_load_raw(test_bin.c_str(), 0x0800, &info) ==
        program_load_ok);
  CHECK(info.format == ProgramFormat_t::raw_binary);
  CHECK(info.load_addr == 0x0800);
  CHECK(info.length == 256);
  CHECK(cpu_get_registers()->pc == 0x0800);
  CHECK(mem[0x0800] == 0x77);
  CHECK(mem[0x0800 + 255] == 0x77);
  CHECK(mem[0x0800 - 1] == SENTINEL_BYTE);
  CHECK(mem[0x0800 + 256] == SENTINEL_BYTE);
  assert_memdirty_marked();
}
