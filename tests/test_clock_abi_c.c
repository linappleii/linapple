// SPDX-License-Identifier: GPL-2.0-only
/* C99 compilation smoke test: a plugin written in C must be able to include
 * the clock's ABI headers and lay out its state frame the way the card does.
 * The descriptor accessor is declared but not called, because in a plugin
 * build it lives inside the shared object. */
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/clock/ClockCard.h"
#include "apple2/peripherals/clock/ClockCardCommands.h"

size_t clockcard_abi_c_state_size(void) {
  ClockCardSaveState_t state;
  ClockCardSetEpochPayload_t set_epoch;
  ClockCardEpochQuery_t epoch_query;
  ClockCardTimeQuery_t time_query;
  ClockCardCmd_e cmd = clockcard_cmd_set_epoch;
  ClockCardQuery_e query = clockcard_query_time;

  state.version = CLOCKCARD_STATE_VERSION;
  state.struct_size = (uint32_t)sizeof(ClockCardSaveState_t);
  set_epoch.epoch = 0;
  epoch_query.is_fixed = 0;
  time_query.month = 1;
  (void)cmd;
  (void)query;
  (void)set_epoch;
  (void)epoch_query;
  (void)time_query;
  return sizeof(state);
}
