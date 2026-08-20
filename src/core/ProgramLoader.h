// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  program_load_ok = 0,
  program_load_not_a_program = 1,
  program_load_file_error = 2,
  program_load_invalid = 3,

} ProgramLoadResult_t;

auto program_loader_try_load(const char* path) -> ProgramLoadResult_t;

#ifdef __cplusplus
}
#endif
