#include "frontends/sdl3/DiskUI.h"

#include <cstring>

#include "apple2/peripherals/disk/DiskError.h"

extern "C" auto DiskUI_GetErrorMessage(int error_code) -> const char* {
  switch (static_cast<DiskError_e>(error_code)) {
    case DISK_ERR_NONE:
      return "Success";
    case DISK_ERR_FILE_NOT_FOUND:
      return "Disk image file not found.";
    case DISK_ERR_IO:
      return "I/O error reading the disk image.";
    case DISK_ERR_UNSUPPORTED_FORMAT:
      return "Unsupported or unrecognized disk format.";
    case DISK_ERR_CORRUPT:
      return "The disk image appears to be corrupt or malformed.";
    case DISK_ERR_OUT_OF_MEMORY:
      return "System ran out of memory while loading the disk.";
    case DISK_ERR_WRITE_PROTECTED:
      return "The disk or file is write protected.";
    default:
      return "An unknown error occurred while loading the disk.";
  }
}
