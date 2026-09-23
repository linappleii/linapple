// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class)
// Justification: This header defines the C99-compatible public ABI for the
// clock card.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { CLOCKCARD_STATE_VERSION = 1, CLOCKCARD_LATCH_COUNT = 10 };

// The ten BCD digits the strobe latched are the card's only architectural
// state. fixed_epoch and use_fixed_epoch once pinned the host's clock; the
// pin was host configuration, not card state, so the card writes them as
// zeros and reads past them. The layout stays as it is so every frame ever
// written loads.
typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint64_t fixed_epoch;
  uint8_t latches[CLOCKCARD_LATCH_COUNT];
  uint8_t use_fixed_epoch;
  uint8_t reserved[5];
} ClockCardSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class)
