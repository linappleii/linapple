// SPDX-License-Identifier: GPL-2.0-only
/* C99 compilation smoke test — if this file compiles, the headers are
 * C99-compatible. */
#include "apple2/media/image_container/ImageContainer.h"
#include "apple2/peripherals/disk/Disk.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"

void disk_abi_c_smoke(void) {
  ImageContainerError_e container_error = image_container_ok;
  uint32_t wrapper_len = image_container_macbinary_header_len;
  (void)container_error;
  (void)wrapper_len;
  (void)image_container_supported_extensions();

  DiskInsertCmd_t cmd;
  cmd.drive = disk_drive_0;
  cmd.write_protected = 0;
  cmd.path[0] = '\0';

  DiskFormatNameQuery_t name_query;
  name_query.index = 0;
  name_query.capabilities = disk_driver_cap_create;
  name_query.name[0] = '\0';

  DiskFormatDriver_t driver;
  driver.abi_version = disk_format_abi_version;
  driver.capabilities = disk_driver_cap_write | disk_driver_cap_create;
  driver.name = "smoke";
  driver.supported_exts = 0;
  driver.probe = 0;
  driver.open = 0;
  driver.close = 0;
  driver.is_write_protected = 0;
  driver.read_track_bits = 0;
  driver.write_track_bits = 0;
  driver.create = 0;
  DiskSavedState_t saved_state;
  saved_state.header.version = disk_state_version;
  (void)cmd;
  (void)name_query;
  (void)driver;
  (void)saved_state;

  disk_loader_register(&driver);
  disk_loader_reset();
  (void)disk_loader_driver_count();
  (void)disk_get_descriptor();
}
