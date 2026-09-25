// SPDX-License-Identifier: GPL-2.0-only
#pragma once

/**
 * Note: Not re-entrant or thread-safe due to internal static buffers.
 */

/**
 * Returns a static pointer to a 32-character hex string (+ null).
 *
 * @param input Null-terminated string.
 * @return Static pointer to a 32-character hex string (+ null).
 */
auto md5str(const char* input) -> char*;
