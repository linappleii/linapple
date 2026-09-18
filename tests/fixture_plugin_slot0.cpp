// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "apple2/peripherals/Peripheral.h"
#include "fixture_plugin_slot0.h"

// A motherboard-internal device that exists only as a shared object. The
// loader's slot-0 path can be reached no other way: a builtin never travels
// through dlopen, and the emulator's own plugins are absent from a static
// build.

namespace {

struct Slot0Fixture_t {
  int32_t slot;
};

auto fixture_init(int slot, HostInterface_t* host) -> void* {
  (void)host;
  std::unique_ptr<Slot0Fixture_t> device(new Slot0Fixture_t());
  device->slot = static_cast<int32_t>(slot);
  return device.release();
}

auto fixture_shutdown(void* instance) -> void {
  std::unique_ptr<Slot0Fixture_t> reclaimed(
      static_cast<Slot0Fixture_t*>(instance));
}

auto fixture_query(void* instance, uint32_t cmd_id, void* out,
                   size_t* out_size) -> PeripheralStatus_t {
  if (cmd_id != slot0_fixture_query_slot) {
    return peripheral_incompatible;
  }
  if (out_size == nullptr) {
    return peripheral_error;
  }
  const size_t required = sizeof(int32_t);
  if (out == nullptr) {
    *out_size = required;
    return peripheral_ok;
  }
  if (*out_size < required) {
    return peripheral_error;
  }
  *static_cast<int32_t*>(out) = static_cast<Slot0Fixture_t*>(instance)->slot;
  *out_size = required;
  return peripheral_ok;
}

const Peripheral_t g_slot0_fixture_peripheral = {
    LINAPPLE_ABI_VERSION,
    SLOT0_FIXTURE_ID,
    SLOT0_FIXTURE_NAME,
    "Motherboard device supplied as a loadable plugin",
    "LinApple Contributors",
    "1.0.0",
    PERIPHERAL_MASK_INTERNAL,
    0,
    fixture_init,
    nullptr,
    fixture_shutdown,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    fixture_query};

}  // namespace

PERIPHERAL_REGISTER(g_slot0_fixture_peripheral)
