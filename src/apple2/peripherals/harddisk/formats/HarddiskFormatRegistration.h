// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"

/* Announces a driver to the loader before main() runs. Survives
   harddisk_loader_reset, which is what separates a driver compiled into the
   binary from one a test pushes in by hand. */
void harddisk_loader_register_permanent(const HarddiskFormatDriver_t* driver);

// A driver declares one of these at the end of its own translation unit, which
// is the whole of what adding a format costs: nothing central names it.
struct HarddiskFormatRegistration_t {
  explicit HarddiskFormatRegistration_t(const HarddiskFormatDriver_t* driver) {
    harddisk_loader_register_permanent(driver);
  }
};
