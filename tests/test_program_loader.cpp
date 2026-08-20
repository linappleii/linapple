#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "core/ProgramLoader.h"
#include "core/Util_Path.h"
#include "doctest.h"

namespace {
constexpr int DSK_BLOCK_SIZE = 1024;
constexpr int DSK_BLOCKS = 140;
constexpr uint16_t ADDR_C000 = 0xC000;
constexpr uint16_t ADDR_1000 = 0x1000;
constexpr uint16_t ADDR_0800 = 0x0800;
constexpr uint16_t ADDR_B000 = 0xB000;
constexpr uint16_t LEN_0100 = 0x0100;
constexpr uint16_t LEN_2000 = 0x2000;
constexpr uint16_t LEN_0010 = 0x0010;
constexpr uint32_t PRG_MAGIC_VAL = 0x214C470A;
constexpr uint16_t PRG_WORD_LEN_8 = 8;
constexpr int PRG_HEADER_PAD_SIZE = 128 - 9;
constexpr uint8_t PRG_DATA_XOR_VAL = 0xAA;
}  // namespace

TEST_CASE(
    "ProgramLoader: [PRG-01] Detection Failure on DSK (Memory preserved)") {
  const char* dsk_path = "test.dsk";
  {
    FilePtr_t f(fopen(dsk_path, "wb"), fclose);
    std::array<uint8_t, DSK_BLOCK_SIZE> zero{};
    zero.fill(0);
    for (int i = 0; i < DSK_BLOCKS; ++i) {
      fwrite(zero.data(), 1, zero.size(), f.get());
    }
  }

  mem_initialize();
  uint8_t original_sample = mem[ADDR_1000];
  CHECK(program_loader_try_load(dsk_path) == program_load_not_a_program);
  CHECK(mem[ADDR_1000] == original_sample);
  remove(dsk_path);
}

TEST_CASE("ProgramLoader: [PRG-02] Missing File (FILE_ERROR)") {
  CHECK(program_loader_try_load("nonexistent.apl") == program_load_file_error);
}

TEST_CASE("ProgramLoader: [PRG-03] Range Check Rejection") {
  const char* bad_apl = "bad.apl";
  {
    FilePtr_t f(fopen(bad_apl, "wb"), fclose);
    uint16_t addr = ADDR_C000;
    uint16_t len = LEN_0100;
    fwrite(&addr, 1, 2, f.get());
    fwrite(&len, 1, 2, f.get());
    std::array<uint8_t, 256> data{};
    data.fill(0);
    fwrite(data.data(), 1, data.size(), f.get());
  }

  CHECK(program_loader_try_load(bad_apl) == program_load_invalid);

  // Also check overflow past $BFFF
  {
    FilePtr_t f(fopen(bad_apl, "wb"), fclose);
    uint16_t addr = ADDR_B000;
    uint16_t len = LEN_2000;  // Total end = $D000
    fwrite(&addr, 1, 2, f.get());
    fwrite(&len, 1, 2, f.get());
    std::vector<uint8_t> dummy(LEN_2000, 0);
    fwrite(dummy.data(), 1, dummy.size(), f.get());
  }
  CHECK(program_loader_try_load(bad_apl) == program_load_invalid);

  remove(bad_apl);
}

TEST_CASE("ProgramLoader: [PRG-04] APL Loading") {
  const char* test_apl = "test.apl";
  std::array<uint8_t, 16> data{};
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>(i + 1);
  }

  {
    FilePtr_t f(fopen(test_apl, "wb"), fclose);
    uint16_t addr = ADDR_0800;
    uint16_t len = static_cast<uint16_t>(data.size());
    fwrite(&addr, 1, 2, f.get());
    fwrite(&len, 1, 2, f.get());
    fwrite(data.data(), 1, data.size(), f.get());
  }

  mem_initialize();
  CHECK(program_loader_try_load(test_apl) == program_load_ok);
  CHECK(cpu_get_registers()->pc == ADDR_0800);
  CHECK(memcmp(mem + ADDR_0800, data.data(), data.size()) == 0);

  remove(test_apl);
}

TEST_CASE("ProgramLoader: [PRG-05] PRG Loading (Word to Byte length)") {
  const char* test_prg = "test.prg";
  std::array<uint8_t, 16> data{};
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>(PRG_DATA_XOR_VAL ^ i);
  }

  {
    FilePtr_t f(fopen(test_prg, "wb"), fclose);
    uint32_t magic = PRG_MAGIC_VAL;
    uint8_t pad1 = 0;
    uint16_t addr = ADDR_1000;
    uint16_t word_len = PRG_WORD_LEN_8;  // 16 bytes
    fwrite(&magic, 1, 4, f.get());
    fwrite(&pad1, 1, 1, f.get());
    fwrite(&addr, 1, 2, f.get());
    fwrite(&word_len, 1, 2, f.get());
    std::array<uint8_t, PRG_HEADER_PAD_SIZE> header_pad{};
    header_pad.fill(0);
    fwrite(header_pad.data(), 1, header_pad.size(), f.get());
    fwrite(data.data(), 1, data.size(), f.get());
  }

  mem_initialize();
  CHECK(program_loader_try_load(test_prg) == program_load_ok);
  CHECK(cpu_get_registers()->pc == ADDR_1000);
  CHECK(memcmp(mem + ADDR_1000, data.data(), data.size()) == 0);

  remove(test_prg);
}
