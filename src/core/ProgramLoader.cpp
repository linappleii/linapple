// SPDX-License-Identifier: GPL-2.0-only
#include "core/ProgramLoader.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-pro-type-cstyle-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic,misc-include-cleaner,google-readability-function-size,cppcoreguidelines-owning-memory,google-runtime-int,modernize-use-auto,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay):
// Centralized program image loader for APL and PRG formats
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "apple2/CPU.h"
#include "apple2/Memory.h"

namespace {

constexpr uint16_t io_region_start = 0xC000;
constexpr uint16_t io_region_end = 0xCFFF;
constexpr uint32_t prg_header_size = 128;
constexpr uint32_t apl_header_size = 4;
constexpr uint8_t mem_fill_value = 0xFF;
constexpr uint32_t prg_magic = 0x214C470A;

enum class ProgramType_t { none, prg, apl };

struct ProgramHeader_t {
  ProgramType_t type;
  uint16_t load_addr;
  uint32_t length;
  uint32_t offset;
};

static auto detect_program(const char* path, ProgramHeader_t* out_header)
    -> ProgramLoadResult_t {
  if (path == nullptr || out_header == nullptr) {
    return program_load_file_error;
  }

  FILE* f = std::fopen(path, "rb");
  if (f == nullptr) {
    return program_load_file_error;
  }

  if (std::fseek(f, 0, SEEK_END) != 0) {
    std::fclose(f);
    return program_load_file_error;
  }
  long ftell_res = std::ftell(f);
  if (ftell_res < 0) {
    std::fclose(f);
    return program_load_file_error;
  }
  uint32_t file_size = static_cast<uint32_t>(ftell_res);
  std::fseek(f, 0, SEEK_SET);

  uint8_t buffer[prg_header_size] = {};
  size_t read = std::fread(buffer, 1, sizeof(buffer), f);
  std::fclose(f);

  if (read < 9) {
    return program_load_not_a_program;
  }

  uint32_t magic = 0;
  std::memcpy(&magic, buffer, 4);
  if (magic == prg_magic) {
    uint16_t word_len = 0;
    out_header->type = ProgramType_t::prg;
    std::memcpy(&out_header->load_addr, buffer + 5, 2);
    std::memcpy(&word_len, buffer + 7, 2);
    out_header->length = static_cast<uint32_t>(word_len) << 1;
    out_header->offset = prg_header_size;
    if (file_size < out_header->offset + out_header->length) {
      return program_load_invalid;
    }
    return program_load_ok;
  }

  uint16_t apl_len = 0;
  std::memcpy(&apl_len, buffer + 2, 2);
  bool size_match =
      ((static_cast<uint32_t>(apl_len) + apl_header_size) == file_size) ||
      ((static_cast<uint32_t>(apl_len) + apl_header_size +
        ((256 - ((apl_len + apl_header_size) & 255)) & 255)) == file_size);

  if (size_match) {
    out_header->type = ProgramType_t::apl;
    std::memcpy(&out_header->load_addr, buffer, 2);
    out_header->length = apl_len;
    out_header->offset = apl_header_size;
    if (file_size < out_header->offset + out_header->length) {
      return program_load_invalid;
    }
    return program_load_ok;
  }

  return program_load_not_a_program;
}

}  // namespace

auto program_loader_try_load(const char* path) -> ProgramLoadResult_t {
  if (path == nullptr || mem == nullptr || memdirty == nullptr) {
    return program_load_file_error;
  }

  ProgramHeader_t header = {ProgramType_t::none, 0, 0, 0};
  ProgramLoadResult_t res = detect_program(path, &header);

  if (res != program_load_ok) {
    return res;
  }

  if (header.load_addr >= io_region_start &&
      header.load_addr <= io_region_end) {
    return program_load_invalid;
  }

  if (static_cast<uint64_t>(header.load_addr) + header.length >
      io_region_start) {
    return program_load_invalid;
  }

  FILE* f = std::fopen(path, "rb");
  if (f == nullptr) {
    return program_load_file_error;
  }

  if (std::fseek(f, static_cast<long>(header.offset), SEEK_SET) != 0) {
    std::fclose(f);
    return program_load_file_error;
  }
  if (std::fread(mem + header.load_addr, 1, header.length, f) !=
      header.length) {
    std::fclose(f);
    return program_load_file_error;
  }
  std::fclose(f);

  std::memset(memdirty, mem_fill_value, NUM_PAGES_48K);
  auto* regs = cpu_get_registers();
  if (regs != nullptr) {
    regs->pc = header.load_addr;
  }

  return program_load_ok;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-pro-type-cstyle-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic,misc-include-cleaner,google-readability-function-size,cppcoreguidelines-owning-memory,google-runtime-int,modernize-use-auto,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
