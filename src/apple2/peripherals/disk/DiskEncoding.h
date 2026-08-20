// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
//             cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification: This header defines a language-neutral C ABI for the GCR
// nibblization engine. C system headers, typedefs, and C-style arrays are
// required for compatibility with C-based consumers.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { disk_encoding_work_buffer_size = 0x3000 };

auto disk_encoding_nibblize_track(uint8_t* work_buffer,
                                  uint8_t* track_image_buffer,
                                  bool is_dos_order, int track) -> uint32_t;

auto disk_encoding_nibblize_track_custom_order(uint8_t* work_buffer,
                                               uint8_t* track_image_buffer,
                                               const uint8_t* sector_order,
                                               int track) -> uint32_t;

auto disk_encoding_denibblize_track(uint8_t* work_buffer, uint8_t* track_image,
                                    bool is_dos_order, int nibbles) -> void;

auto disk_encoding_skew_track(uint8_t* track_image_buffer, uint8_t* work_buffer,
                              int track, int nibbles) -> void;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
//           cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
