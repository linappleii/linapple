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

  PROGRAM_LOAD_OK = program_load_ok,
  PROGRAM_LOAD_NOT_A_PROGRAM = program_load_not_a_program,
  PROGRAM_LOAD_FILE_ERROR = program_load_file_error,
  PROGRAM_LOAD_INVALID = program_load_invalid
} ProgramLoadResult_t;

typedef ProgramLoadResult_t ProgramLoadResult_e;
typedef ProgramLoadResult_t ProgramLoadResult;

auto program_loader_try_load(const char* path) -> ProgramLoadResult_t;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static inline auto ProgramLoader_TryLoad(const char* path)
    -> ProgramLoadResult_t {
  return program_loader_try_load(path);
}
#endif
