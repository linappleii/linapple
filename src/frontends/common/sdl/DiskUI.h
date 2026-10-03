// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>

// Longest title the window bar and the drive labels have room for.
constexpr size_t k_disk_ui_display_name_max = 15;

[[nodiscard]] auto disk_ui_get_error_message(int error_code) noexcept -> const
    char*;

// Strips extension, lower-cases shouty names, and truncates to
// k_disk_ui_display_name_max characters.
auto disk_ui_format_display_name(const char* file_name, char* out,
                                 size_t out_size) -> void;
