#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "frontends/sdl3/DiskUI.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include <cstring>

TEST_CASE("DiskUI: Error Message Mapping") {
    CHECK(strcmp(DiskUI_GetErrorMessage(DISK_ERR_NONE), "Success") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(DISK_ERR_FILE_NOT_FOUND), "Disk image file not found.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(DISK_ERR_IO), "I/O error reading the disk image.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(DISK_ERR_UNSUPPORTED_FORMAT), "Unsupported or unrecognized disk format.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(DISK_ERR_CORRUPT), "The disk image appears to be corrupt or malformed.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(DISK_ERR_OUT_OF_MEMORY), "System ran out of memory while loading the disk.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(DISK_ERR_WRITE_PROTECTED), "The disk or file is write protected.") == 0);
    CHECK(strcmp(DiskUI_GetErrorMessage(999), "An unknown error occurred while loading the disk.") == 0);
}
