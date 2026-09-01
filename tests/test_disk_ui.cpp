#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>

#include "apple2/peripherals/disk/DiskError.h"
#include "doctest.h"
#include "frontends/common/sdl/DiskUI.h"

TEST_CASE("DiskUI: error Message Mapping") {
  CHECK(strcmp(disk_ui_get_error_message(disk_err_none), "Success") == 0);
  CHECK(strcmp(disk_ui_get_error_message(disk_err_file_not_found),
               "Disk image file not found.") == 0);
  CHECK(strcmp(disk_ui_get_error_message(disk_err_io),
               "I/O error reading the disk image.") == 0);
  CHECK(strcmp(disk_ui_get_error_message(disk_err_unsupported_format),
               "Unsupported or unrecognized disk format.") == 0);
  CHECK(strcmp(disk_ui_get_error_message(disk_err_corrupt),
               "The disk image appears to be corrupt or malformed.") == 0);
  CHECK(strcmp(disk_ui_get_error_message(disk_err_out_of_memory),
               "System ran out of memory while loading the disk.") == 0);
  CHECK(strcmp(disk_ui_get_error_message(disk_err_write_protected),
               "The disk or file is write protected.") == 0);
  CHECK(strcmp(disk_ui_get_error_message(999),
               "An unknown error occurred while loading the disk.") == 0);
}
