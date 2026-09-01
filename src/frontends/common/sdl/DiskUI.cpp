// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/sdl/DiskUI.h"

#include "apple2/peripherals/disk/DiskError.h"

extern "C" auto disk_ui_get_error_message(int error_code) -> const char* {
  switch (static_cast<DiskError_e>(error_code)) {
    case disk_err_none:
      return "Success";
    case disk_err_file_not_found:
      return "Disk image file not found.";
    case disk_err_io:
      return "I/O error reading the disk image.";
    case disk_err_unsupported_format:
      return "Unsupported or unrecognized disk format.";
    case disk_err_corrupt:
      return "The disk image appears to be corrupt or malformed.";
    case disk_err_out_of_memory:
      return "System ran out of memory while loading the disk.";
    case disk_err_write_protected:
      return "The disk or file is write protected.";
    default:
      return "An unknown error occurred while loading the disk.";
  }
}
