// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
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

TEST_CASE("DiskUI: display names lose the extension and the shouting") {
  std::array<char, disk_ui_display_name_max + 1> name{};

  disk_ui_format_display_name("MASTER.DSK", name.data(), name.size());
  CHECK(strcmp(name.data(), "Master") == 0);

  // One lower-case letter is enough to show the author meant the case
  disk_ui_format_display_name("MicroChess.dsk", name.data(), name.size());
  CHECK(strcmp(name.data(), "MicroChess") == 0);

  disk_ui_format_display_name("a.dsk", name.data(), name.size());
  CHECK(strcmp(name.data(), "a") == 0);

  // A leading dot is the whole name, not an empty name with an extension
  disk_ui_format_display_name(".hidden", name.data(), name.size());
  CHECK(strcmp(name.data(), ".hidden") == 0);

  disk_ui_format_display_name("ProDOS 2.4.2.dsk", name.data(), name.size());
  CHECK(strcmp(name.data(), "ProDOS 2.4.2") == 0);

  disk_ui_format_display_name("An extremely long image name.dsk", name.data(),
                              name.size());
  CHECK(strcmp(name.data(), "An extremely lo") == 0);

  disk_ui_format_display_name("no extension", name.data(), name.size());
  CHECK(strcmp(name.data(), "no extension") == 0);
}

TEST_CASE("DiskUI: display name formatting refuses to run off its buffer") {
  std::array<char, 4> tiny{};
  disk_ui_format_display_name("MASTER.DSK", tiny.data(), tiny.size());
  CHECK(strcmp(tiny.data(), "Mas") == 0);

  std::array<char, 8> untouched{};
  untouched.fill('x');
  disk_ui_format_display_name(nullptr, untouched.data(), untouched.size());
  CHECK(untouched[0] == '\0');

  // Nothing to dereference and nowhere to write: both must simply return
  disk_ui_format_display_name("MASTER.DSK", nullptr, 0);
  disk_ui_format_display_name("MASTER.DSK", untouched.data(), 0);
}
