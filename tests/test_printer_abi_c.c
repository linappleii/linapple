// SPDX-License-Identifier: GPL-2.0-only
/* C99 compilation smoke test: a plugin written in C must be able to include
 * the printer card's ABI headers and lay out its state frame the way the card
 * does. The descriptor accessor is declared but not called, because in a
 * plugin build it lives inside the shared object. */
#include "test_printer_abi_c.h"

#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/printer/Printer.h"
#include "apple2/peripherals/printer/PrinterCommands.h"

size_t printer_abi_c_state_size(void) {
  PrinterSaveState state;
  state.version = PRINTER_STATE_VERSION;
  state.struct_size = (uint32_t)sizeof(PrinterSaveState);
  state.total_chars_printed = 0;
  state.busy_cycles = 0;
  state.data_latch = 0;
  state.status_latch = 0;
  state.is_online = 0;
  state.is_busy = 0;
  return sizeof(state);
}

size_t printer_abi_c_version_offset(void) {
  return offsetof(PrinterSaveState, version);
}

size_t printer_abi_c_struct_size_offset(void) {
  return offsetof(PrinterSaveState, struct_size);
}

size_t printer_abi_c_total_chars_printed_offset(void) {
  return offsetof(PrinterSaveState, total_chars_printed);
}

size_t printer_abi_c_busy_cycles_offset(void) {
  return offsetof(PrinterSaveState, busy_cycles);
}

size_t printer_abi_c_data_latch_offset(void) {
  return offsetof(PrinterSaveState, data_latch);
}

size_t printer_abi_c_status_latch_offset(void) {
  return offsetof(PrinterSaveState, status_latch);
}

size_t printer_abi_c_is_online_offset(void) {
  return offsetof(PrinterSaveState, is_online);
}

size_t printer_abi_c_is_busy_offset(void) {
  return offsetof(PrinterSaveState, is_busy);
}

uint32_t printer_abi_c_state_version(void) { return PRINTER_STATE_VERSION; }
