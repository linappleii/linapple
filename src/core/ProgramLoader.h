// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstdio>

enum class ProgramLoadResult_t : uint8_t {
  ok = 0,
  not_a_program = 1,
  file_error = 2,
  invalid = 3,
};

enum class ProgramFormat_t : uint8_t {
  none = 0,
  prg = 1,
  apl = 2,
  raw_binary = 3,
};

struct ProgramInfo_t {
  ProgramFormat_t format = ProgramFormat_t::none;
  uint16_t load_addr = 0;
  uint32_t length = 0;
  uint32_t offset = 0;
};

constexpr auto program_load_ok = ProgramLoadResult_t::ok;
constexpr auto program_load_not_a_program = ProgramLoadResult_t::not_a_program;
constexpr auto program_load_file_error = ProgramLoadResult_t::file_error;
constexpr auto program_load_invalid = ProgramLoadResult_t::invalid;

constexpr auto operator==(int lhs, ProgramLoadResult_t rhs) noexcept -> bool {
  return lhs == static_cast<int>(rhs);
}

constexpr auto operator==(ProgramLoadResult_t lhs, int rhs) noexcept -> bool {
  return static_cast<int>(lhs) == rhs;
}

constexpr auto operator!=(int lhs, ProgramLoadResult_t rhs) noexcept -> bool {
  return !(lhs == rhs);
}

constexpr auto operator!=(ProgramLoadResult_t lhs, int rhs) noexcept -> bool {
  return !(lhs == rhs);
}

[[nodiscard]] auto program_loader_inspect(FILE* f,
                                          ProgramInfo_t* out_info) noexcept
    -> ProgramLoadResult_t;

[[nodiscard]] auto program_loader_try_load(
    const char* path, ProgramInfo_t* out_info = nullptr) noexcept
    -> ProgramLoadResult_t;

[[nodiscard]] auto program_loader_load_raw(
    const char* path, uint16_t load_addr = 0x0800,
    ProgramInfo_t* out_info = nullptr) noexcept -> ProgramLoadResult_t;
