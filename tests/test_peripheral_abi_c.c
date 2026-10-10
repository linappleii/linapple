// SPDX-License-Identifier: GPL-2.0-only
#include "test_peripheral_abi_c.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"

typedef struct {
  int slot;
  uint8_t last_val;
  HostInterface_t* host;
} TestPeripheral_t;

static uint8_t Test_IO(void* instance, uint16_t pc, uint16_t addr,
                       uint8_t write, uint8_t val, uint32_t cycles) {
  (void)pc;
  (void)cycles;
  (void)addr;
  TestPeripheral_t* p = (TestPeripheral_t*)instance;
  if (write) {
    p->last_val = val;
    return 0;
  }
  return p->last_val;
}

static const uint8_t g_test_c_rom[256] = {0x60};

/* C99 has no static_assert; an array of negative size fails the same way. */
typedef char
    host_local_time_is_24_bytes[sizeof(HostLocalTime_t) == 24 ? 1 : -1];

static void* Test_Init(int slot, HostInterface_t* host) {
  // Actually, let's just use a static for the test instance.
  static TestPeripheral_t static_instance;
  static_instance.slot = slot;
  static_instance.host = host;
  static_instance.last_val = 0;

  host->RegisterIO(slot, Test_IO, Test_IO, NULL, NULL);
  if (host->RegisterCxROM != NULL) {
    host->RegisterCxROM(slot, g_test_c_rom);
  }
  return &static_instance;
}

int test_c_peripheral_read_clock(HostInterface_t* host, int64_t* unix_seconds,
                                 uint8_t* weekday) {
  HostLocalTime_t now;
  if (host == NULL || host->GetLocalTime == NULL || !host->GetLocalTime(&now)) {
    return 0;
  }
  *unix_seconds = now.unix_seconds;
  *weekday = now.weekday;
  return 1;
}

static void Test_Reset(void* instance) {
  TestPeripheral_t* p = (TestPeripheral_t*)instance;
  p->last_val = 0;
}

static void Test_Shutdown(void* instance) { (void)instance; }

Peripheral_t g_test_c_peripheral = {
    LINAPPLE_ABI_VERSION,
    "test.c_peripheral",
    "TestCPeripheral",
    "A C-based test peripheral",
    "Test Author",
    "1.0.0",
    0xFF,
    -1,
    Test_Init,
    Test_Reset,
    Test_Shutdown,
    NULL,  // think
    NULL,  // on_vblank
    NULL,  // save_state
    NULL,  // load_state
    NULL,  // command
    NULL   // query
};

/* The sink members follow GetLocalTime, so a plugin built against a header
 * without them finds every member it knows at the offset it expects. */
typedef char
    printer_put_char_is_member_16[offsetof(HostInterface_t, PrinterPutChar) ==
                                          16 * sizeof(void (*)(void))
                                      ? 1
                                      : -1];
typedef char printer_get_status_is_member_17[offsetof(HostInterface_t,
                                                      PrinterGetStatus) ==
                                                     17 * sizeof(void (*)(void))
                                                 ? 1
                                                 : -1];
typedef char
    get_local_time_is_member_21[offsetof(HostInterface_t, GetLocalTime) ==
                                        21 * sizeof(void (*)(void))
                                    ? 1
                                    : -1];
typedef char sink_open_is_member_22[offsetof(HostInterface_t, SinkOpen) ==
                                            22 * sizeof(void (*)(void))
                                        ? 1
                                        : -1];
typedef char sink_write_is_member_23[offsetof(HostInterface_t, SinkWrite) ==
                                             23 * sizeof(void (*)(void))
                                         ? 1
                                         : -1];
typedef char sink_ready_is_member_24[offsetof(HostInterface_t, SinkReady) ==
                                             24 * sizeof(void (*)(void))
                                         ? 1
                                         : -1];
typedef char sink_close_is_member_25[offsetof(HostInterface_t, SinkClose) ==
                                             25 * sizeof(void (*)(void))
                                         ? 1
                                         : -1];
typedef char sink_read_is_member_26[offsetof(HostInterface_t, SinkRead) ==
                                            26 * sizeof(void (*)(void))
                                        ? 1
                                        : -1];
typedef char
    sink_set_line_is_member_27[offsetof(HostInterface_t, SinkSetLine) ==
                                       27 * sizeof(void (*)(void))
                                   ? 1
                                   : -1];
typedef char
    sink_get_lines_is_member_28[offsetof(HostInterface_t, SinkGetLines) ==
                                        28 * sizeof(void (*)(void))
                                    ? 1
                                    : -1];
typedef char
    schedule_event_is_member_29[offsetof(HostInterface_t, ScheduleEvent) ==
                                        29 * sizeof(void (*)(void))
                                    ? 1
                                    : -1];
typedef char get_machine_is_member_30[offsetof(HostInterface_t, GetMachine) ==
                                              30 * sizeof(void (*)(void))
                                          ? 1
                                          : -1];
typedef char
    get_frame_cycles_is_member_31[offsetof(HostInterface_t, GetFrameCycles) ==
                                          31 * sizeof(void (*)(void))
                                      ? 1
                                      : -1];
typedef char host_interface_has_32_members
    [sizeof(HostInterface_t) == 32 * sizeof(void (*)(void)) ? 1 : -1];
typedef char
    machine_values_are_pinned[peripheral_machine_apple2 == 0 &&
                                      peripheral_machine_apple2_plus == 1 &&
                                      peripheral_machine_apple2e == 2
                                  ? 1
                                  : -1];
typedef char sink_kinds_are_pinned
    [peripheral_sink_printer == 1 && peripheral_sink_serial == 2 ? 1 : -1];
typedef char
    serial_line_is_12_bytes[sizeof(PeripheralSerialLine_t) == 12 ? 1 : -1];
typedef char
    serial_parity_is_pinned[peripheral_serial_parity_none == 0 &&
                                    peripheral_serial_parity_odd == 1 &&
                                    peripheral_serial_parity_even == 2 &&
                                    peripheral_serial_parity_mark == 3 &&
                                    peripheral_serial_parity_space == 4
                                ? 1
                                : -1];

/* Returns -1 when the host offers no sink or refuses the slot, otherwise
 * writes the byte and reports whether the sink was ready to take it. */
int test_c_peripheral_sink_write(HostInterface_t* host, int slot,
                                 uint8_t byte) {
  void* sink = NULL;
  bool ready = false;
  if (host == NULL || host->SinkOpen == NULL || host->SinkWrite == NULL ||
      host->SinkReady == NULL) {
    return -1;
  }
  sink = host->SinkOpen(NULL, slot, peripheral_sink_printer);
  if (sink == NULL) {
    return -1;
  }
  ready = host->SinkReady(sink);
  host->SinkWrite(sink, byte);
  return ready ? 1 : 0;
}
