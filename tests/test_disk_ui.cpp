#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "frontends/sdl3/DiskUI.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskError.h"
#include <cstring>

TEST_CASE("DiskUI: Error Message Mapping") {
    CHECK(strcmp(DiskUI_GetErrorMessage(disk_err_none), "Success") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(disk_err_file_not_found), "Disk image file not found.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(disk_err_io), "I/O error reading the disk image.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(disk_err_unsupported_format), "Unsupported or unrecognized disk format.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(disk_err_corrupt), "The disk image appears to be corrupt or malformed.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(disk_err_out_of_memory), "System ran out of memory while loading the disk.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(disk_err_write_protected), "The disk or file is write protected.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(999), "An unknown error occurred while loading the disk.") == 0);
}
