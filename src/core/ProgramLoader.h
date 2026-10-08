// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstdio>

enum class ProgramLoadResult : uint8_t {
  ok = 0,
  not_a_program = 1,
  file_error = 2,
  invalid = 3,
};

enum class ProgramFormat : uint8_t {
  none = 0,
  prg = 1,
  apl = 2,
  raw_binary = 3,
};

struct ProgramInfo {
  ProgramFormat format = ProgramFormat::none;
  uint16_t load_addr = 0;
  uint32_t length = 0;
  uint32_t offset = 0;
};

constexpr auto program_load_ok = ProgramLoadResult::ok;
constexpr auto program_load_not_a_program = ProgramLoadResult::not_a_program;
constexpr auto program_load_file_error = ProgramLoadResult::file_error;
constexpr auto program_load_invalid = ProgramLoadResult::invalid;

constexpr auto operator==(int lhs, ProgramLoadResult rhs) noexcept -> bool {
  return lhs == static_cast<int>(rhs);
}

constexpr auto operator==(ProgramLoadResult lhs, int rhs) noexcept -> bool {
  return static_cast<int>(lhs) == rhs;
}

constexpr auto operator!=(int lhs, ProgramLoadResult rhs) noexcept -> bool {
  return !(lhs == rhs);
}

constexpr auto operator!=(ProgramLoadResult lhs, int rhs) noexcept -> bool {
  return !(lhs == rhs);
}

auto program_loader_inspect(FILE* f, ProgramInfo* out_info) noexcept
    -> ProgramLoadResult;

auto program_loader_try_load(const char* path,
                             ProgramInfo* out_info = nullptr) noexcept
    -> ProgramLoadResult;

auto program_loader_load_raw(const char* path, uint16_t load_addr = 0x0800,
                             ProgramInfo* out_info = nullptr) noexcept
    -> ProgramLoadResult;
