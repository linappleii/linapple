// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/sdl/DiskUI.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <string>

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

extern "C" auto disk_ui_format_display_name(const char* file_name, char* out,
                                            size_t out_size) -> void {
  if (out == nullptr || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (file_name == nullptr) {
    return;
  }

  std::string title(file_name);

  // Names that shout are an artefact of the DOS 3.3 era rather than a choice,
  // so they read better with only the first letter left capital.
  const bool shouts =
      std::none_of(title.begin(), title.end(), [](unsigned char ch) {
        return std::islower(ch) != 0;
      });
  constexpr size_t min_length_to_recase = 3;
  if (shouts && title.length() >= min_length_to_recase) {
    for (size_t i = 1; i < title.length(); ++i) {
      title[i] =
          static_cast<char>(std::tolower(static_cast<unsigned char>(title[i])));
    }
  }

  const size_t dot = title.rfind('.');
  if (dot != std::string::npos && dot > 0) {
    title.erase(dot);
  }

  if (title.length() > disk_ui_display_name_max) {
    title.resize(disk_ui_display_name_max);
  }

  const size_t copy_length = std::min(title.length(), out_size - 1);
  std::memcpy(out, title.data(), copy_length);
  out[copy_length] = '\0';
}
