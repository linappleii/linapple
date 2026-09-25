// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "doctest.h"

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
  CHECK(strcmp(disk_ui_get_error_message(disk_err_invalid_argument),
               "The disk request was not valid.") == 0);
  CHECK(strcmp(disk_ui_get_error_message(disk_err_unsupported),
               "The disk image is larger or more complex than this emulator "
               "supports.") == 0);
  CHECK(strcmp(disk_ui_get_error_message(999),
               "An unknown error occurred while loading the disk.") == 0);
}

TEST_CASE("DiskUI: every disk error has a message of its own") {
  constexpr DiskError_e every_error[] = {
      disk_err_none,    disk_err_file_not_found,   disk_err_unsupported_format,
      disk_err_corrupt, disk_err_write_protected,  disk_err_out_of_memory,
      disk_err_io,      disk_err_invalid_argument, disk_err_unsupported};
  constexpr size_t error_count = sizeof(every_error) / sizeof(every_error[0]);
  // The enum has no count sentinel, so the list is pinned to it from the
  // other end: its last entry is the enum's last value and the values before
  // it are consecutive, which the loop below checks.
  static_assert(every_error[error_count - 1] == disk_err_unsupported,
                "the list must end at the enum's last value");
  static_assert(disk_err_unsupported == error_count - 1,
                "the list must be as long as the enum");

  const char* const fallback = disk_ui_get_error_message(999);
  for (size_t i = 0; i < error_count; ++i) {
    CHECK(static_cast<size_t>(every_error[i]) == i);
    const char* const message = disk_ui_get_error_message(every_error[i]);
    REQUIRE(message != nullptr);
    CHECK(message[0] != '\0');
    CHECK(strcmp(message, fallback) != 0);
  }
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
