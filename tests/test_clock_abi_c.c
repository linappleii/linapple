// SPDX-License-Identifier: GPL-2.0-only
/* C99 compilation smoke test: a plugin written in C must be able to include
 * the clock card's ABI headers and lay out its state frame the way the card
 * does. The descriptor accessor is declared but not called, because in a
 * plugin build it lives inside the shared object. */
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/clock/ClockCard.h"
#include "apple2/peripherals/clock/ClockCardCommands.h"

size_t clockcard_abi_c_state_size(void) {
  ClockCardSaveState_t state;
  state.version = CLOCKCARD_STATE_VERSION;
  state.struct_size = (uint32_t)sizeof(ClockCardSaveState_t);
  state.fixed_epoch = 0;
  state.use_fixed_epoch = 0;
  state.latches[CLOCKCARD_LATCH_COUNT - 1] = 0;
  return sizeof(state);
}

size_t clockcard_abi_c_fixed_epoch_offset(void) {
  return offsetof(ClockCardSaveState_t, fixed_epoch);
}

size_t clockcard_abi_c_latches_offset(void) {
  return offsetof(ClockCardSaveState_t, latches);
}

size_t clockcard_abi_c_use_fixed_epoch_offset(void) {
  return offsetof(ClockCardSaveState_t, use_fixed_epoch);
}

size_t clockcard_abi_c_reserved_offset(void) {
  return offsetof(ClockCardSaveState_t, reserved);
}

uint32_t clockcard_abi_c_state_version(void) { return CLOCKCARD_STATE_VERSION; }
