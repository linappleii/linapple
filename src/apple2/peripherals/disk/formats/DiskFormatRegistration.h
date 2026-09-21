// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/peripherals/disk/DiskFormatDriver.h"

/* Announces a driver to the loader before main() runs. Survives
   disk_loader_reset, which is what separates a driver compiled into the binary
   from one a test pushes in by hand. */
void disk_loader_register_permanent(const DiskFormatDriver_t* driver);

// A driver declares one of these at the end of its own translation unit, which
// is the whole of what adding a format costs: nothing central names it.
struct DiskFormatRegistration_t {
  explicit DiskFormatRegistration_t(const DiskFormatDriver_t* driver) {
    disk_loader_register_permanent(driver);
  }
};
