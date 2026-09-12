// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
// Justification: This header defines the C99-compatible public ABI for the
// Speaker subsystem. C system headers, typedefs, and C-style return types are
// required for cross-language compatibility with C-based consumers.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SPEAKER_QUERY_IS_ACTIVE = 0x0100,
  speaker_query_is_active =
      SPEAKER_QUERY_IS_ACTIVE  // out: uint8_t (0=inactive, 1=active)
} SpeakerQuery_e;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
