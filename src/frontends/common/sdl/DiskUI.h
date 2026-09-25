// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest title the window bar and the drive labels have room for. */
enum { disk_ui_display_name_max = 15 };

/**
 * @brief Map disk error codes to human-readable strings for UI display.
 *
 * @param error_code The DiskError_e code returned by commands or queries.
 * @return A static string describing the error.
 */
const char* disk_ui_get_error_message(int error_code);

/**
 * @brief Turn an image file name into the short title shown to the user.
 *
 * Strips the extension, lower-cases a name that shouts, and truncates to
 * disk_ui_display_name_max characters.
 *
 * @param file_name The image's file name, without its directory.
 * @param out Buffer receiving the NUL-terminated title.
 * @param out_size Capacity of out, in bytes.
 */
void disk_ui_format_display_name(const char* file_name, char* out,
                                 size_t out_size);

#ifdef __cplusplus
}
#endif
