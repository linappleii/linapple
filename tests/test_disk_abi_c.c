// SPDX-License-Identifier: GPL-2.0-only
/* C99 compilation smoke test — if this file compiles, the headers are
 * C99-compatible. */
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"

void disk_abi_c_smoke(void) {
  DiskInsertCmd_t cmd;
  cmd.drive = disk_drive_0;
  cmd.write_protected = 0;
  cmd.create_if_necessary = 0;
  cmd.path[0] = '\0';

  DiskFormatDriver_t driver;
  driver.abi_version = disk_format_abi_version;
  driver.capabilities = disk_driver_cap_write;
  driver.name = "smoke";
  driver.creatable_exts = 0;
  driver.supported_exts = 0;
  driver.probe = 0;
  driver.open = 0;
  driver.close = 0;
  driver.is_write_protected = 0;
  driver.read_track = 0;
  driver.write_track = 0;
  driver.create = 0;
  driver.read_flux_bit = 0;
  (void)cmd;
  (void)driver;
}
