#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Note: Not re-entrant or thread-safe due to internal static buffers.
 */

/**
 * Returns a static pointer to avoid complex manual memory management in the C
 * ABI.
 * 
 * @param input Null-terminated string.
 * @return Static pointer to a 32-character hex string (+ null).
 */
#ifdef __cplusplus
auto md5str(const char* input) -> char*;
#else
char* md5str(const char* input);
#endif

#ifdef __cplusplus
}
#endif
