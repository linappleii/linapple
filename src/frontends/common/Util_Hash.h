// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// Note: Not thread-safe. Returns pointer to internal static buffer.
[[nodiscard]] auto md5str(const char* input) -> char*;
