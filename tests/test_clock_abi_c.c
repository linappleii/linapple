// SPDX-License-Identifier: GPL-2.0-only
/* C99 compilation smoke test: a plugin written in C must be able to include
 * the clock's ABI headers and lay out its state frame the way the card does.
 * The descriptor accessor is declared but not called, because in a plugin
 * build it lives inside the shared object. */
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/clock/Clock.h"
#include "apple2/peripherals/clock/ClockCommands.h"

size_t clock_abi_c_state_size(void) {
  ClockSaveState_t state;
  ClockSetEpochPayload_t set_epoch;
  ClockEpochQuery_t epoch_query;
  ClockTimeQuery_t time_query;
  ClockCmd_e cmd = clock_cmd_set_epoch;
  ClockQuery_e query = clock_query_get_time;

  state.version = CLOCK_STATE_VERSION;
  state.struct_size = (uint32_t)sizeof(ClockSaveState_t);
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
