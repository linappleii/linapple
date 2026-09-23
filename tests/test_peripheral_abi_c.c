// SPDX-License-Identifier: GPL-2.0-only
#include <stdint.h>

#include "apple2/peripherals/Peripheral.h"

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
