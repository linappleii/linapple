// SPDX-License-Identifier: GPL-2.0-only
#include "core/ProgramLoader.h"

#include <stdio.h>
#include <sys/stat.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/Util_Endian.h"
#include "core/Util_Path.h"

namespace {

constexpr uint16_t k_apple2_ram_limit = 0xC000;
constexpr size_t k_prg_header_size = 128;
constexpr size_t k_apl_header_size = 4;
constexpr uint8_t k_mem_fill_value = 0xFF;
constexpr uint32_t k_prg_magic = 0x214C470A;

constexpr size_t k_prg_load_addr_offset = 5;
constexpr size_t k_prg_word_len_offset = 7;
constexpr size_t k_min_prg_read_size = 9;

constexpr size_t k_apl_load_addr_offset = 0;
constexpr size_t k_apl_len_offset = 2;
constexpr uint32_t k_page_size = 256;
constexpr uint32_t k_page_mask = 255;

constexpr int64_t k_max_program_file_size = 0x10000 + k_prg_header_size;
constexpr uint16_t k_dos33_bload_addr = 0xAA72;
constexpr uint16_t k_dos33_bload_len = 0xAA60;

}  // namespace

auto program_loader_inspect(FILE* f, ProgramInfo_t* out_info) noexcept
    -> ProgramLoadResult_t {
  if (f == nullptr || out_info == nullptr) {
    return program_load_file_error;
  }

  struct stat st{};
  if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
    return program_load_file_error;
  }

  const auto ftell_res = Path::file_size(f);
  if (ftell_res <= 0 || ftell_res > k_max_program_file_size) {
    return (ftell_res < 0) ? program_load_file_error
                           : program_load_not_a_program;
  }
  const auto file_size = static_cast<uint32_t>(ftell_res);

  std::array<uint8_t, k_prg_header_size> buf{};
  const auto bytes_read = std::fread(buf.data(), 1, buf.size(), f);
  if (bytes_read < k_apl_header_size) {
    return program_load_not_a_program;
  }

  if (bytes_read >= k_min_prg_read_size) {
    const auto magic = read_u32_le(&buf[0]);
    if (magic == k_prg_magic) {
      const auto word_len = read_u16_le(&buf[k_prg_word_len_offset]);
      out_info->format = ProgramFormat_t::prg;
      out_info->load_addr = read_u16_le(&buf[k_prg_load_addr_offset]);
      out_info->length = static_cast<uint32_t>(word_len) * 2;
      out_info->offset = static_cast<uint32_t>(k_prg_header_size);
      if (file_size < out_info->offset + out_info->length) {
        return program_load_invalid;
      }
      return program_load_ok;
    }
  }

  const auto apl_len = read_u16_le(&buf[k_apl_len_offset]);
  const auto exact_size = static_cast<uint32_t>(apl_len) + k_apl_header_size;
  const auto pad = (k_page_size - (exact_size & k_page_mask)) & k_page_mask;
  const auto padded_size = exact_size + pad;

  const auto size_match =
      (exact_size == file_size) || (padded_size == file_size);
  if (size_match) {
    out_info->format = ProgramFormat_t::apl;
    out_info->load_addr = read_u16_le(&buf[k_apl_load_addr_offset]);
    out_info->length = apl_len;
    out_info->offset = static_cast<uint32_t>(k_apl_header_size);
    if (file_size < out_info->offset + out_info->length) {
      return program_load_invalid;
    }
    return program_load_ok;
  }

  return program_load_not_a_program;
}

auto program_loader_try_load(const char* path, ProgramInfo_t* out_info) noexcept
    -> ProgramLoadResult_t {
  if (path == nullptr || path[0] == '\0' || mem == nullptr ||
      memdirty == nullptr) {
    return program_load_file_error;
  }

  FilePtr_t f{std::fopen(path, "rb"), std::fclose};
  if (!f) {
    return program_load_file_error;
  }

  ProgramInfo_t info{};
  const auto res = program_loader_inspect(f.get(), &info);
  if (res != program_load_ok) {
    return res;
  }

  if (info.length == 0 || static_cast<uint64_t>(info.load_addr) + info.length >
                              k_apple2_ram_limit) {
    return program_load_invalid;
  }

  if (std::fseek(f.get(), static_cast<long>(info.offset), SEEK_SET) != 0) {
    return program_load_file_error;
  }

  std::vector<uint8_t> staging(info.length);
  if (std::fread(staging.data(), 1, staging.size(), f.get()) !=
      staging.size()) {
    return program_load_file_error;
  }
  std::memcpy(&mem[info.load_addr], staging.data(), staging.size());

  std::memset(memdirty, k_mem_fill_value, NUM_PAGES_48K);
  write_u16_le(&mem[k_dos33_bload_addr], info.load_addr);
  write_u16_le(&mem[k_dos33_bload_len], static_cast<uint16_t>(info.length));

  auto* regs = cpu_get_registers();
  if (regs != nullptr) {
    regs->pc = info.load_addr;
  }

  if (out_info != nullptr) {
    *out_info = info;
  }

  return program_load_ok;
}

auto program_loader_load_raw(const char* path, uint16_t load_addr,
                             ProgramInfo_t* out_info) noexcept
    -> ProgramLoadResult_t {
  if (path == nullptr || path[0] == '\0' || mem == nullptr ||
      memdirty == nullptr) {
    return program_load_file_error;
  }

  FilePtr_t f{std::fopen(path, "rb"), std::fclose};
  if (!f) {
    return program_load_file_error;
  }

  struct stat st{};
  if (fstat(fileno(f.get()), &st) != 0 || !S_ISREG(st.st_mode)) {
    return program_load_file_error;
  }

  const auto ftell_res = Path::file_size(f.get());
  if (ftell_res <= 0 || ftell_res > 65536) {
    return (ftell_res < 0) ? program_load_file_error
                           : program_load_not_a_program;
  }
  const auto size = static_cast<size_t>(ftell_res);

  auto actual_load_addr = load_addr;
  if (actual_load_addr == 0x0800 && size == 65536) {
    actual_load_addr = 0x0000;
  }

  if (static_cast<size_t>(actual_load_addr) + size > 65536) {
    return program_load_invalid;
  }

  std::vector<uint8_t> staging(size);
  if (std::fread(staging.data(), 1, staging.size(), f.get()) !=
      staging.size()) {
    return program_load_file_error;
  }

  std::memcpy(&mem[actual_load_addr], staging.data(), staging.size());
  std::memset(memdirty, k_mem_fill_value, NUM_PAGES_48K);

  auto* regs = cpu_get_registers();
  if (regs != nullptr) {
    regs->pc = actual_load_addr;
  }

  if (out_info != nullptr) {
    out_info->format = ProgramFormat_t::raw_binary;
    out_info->load_addr = actual_load_addr;
    out_info->length = static_cast<uint32_t>(size);
    out_info->offset = 0;
  }

  return program_load_ok;
}
