// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "doctest.h"

// A sanitized name is the download's on-disk name, so anything that survives
// here is joined straight onto a cache directory. Every case below is a name
// an FTP server is free to send.

TEST_CASE("Util_Path: sanitize_filename passes ordinary media names") {
  CHECK(Path::sanitize_filename("valid_disk.dsk") == "valid_disk.dsk");
  CHECK(Path::sanitize_filename("My Game (1983).po") == "My Game (1983).po");
}

TEST_CASE("Util_Path: sanitize_filename rejects directory components") {
  CHECK(Path::sanitize_filename("../../../etc/passwd") == "");
  CHECK(Path::sanitize_filename("..\\..\\windows\\system32\\calc.exe") == "");
  CHECK(Path::sanitize_filename("..") == "");
  CHECK(Path::sanitize_filename(".") == "");
  CHECK(Path::sanitize_filename("dir/subdir/disk.po") == "");
}

TEST_CASE("Util_Path: sanitize_filename rejects hidden dotfiles") {
  CHECK(Path::sanitize_filename(".bashrc") == "");
  CHECK(Path::sanitize_filename(".profile") == "");
}

TEST_CASE("Util_Path: sanitize_filename rejects empty and control characters") {
  CHECK(Path::sanitize_filename("") == "");
  CHECK(Path::sanitize_filename("in\x01valid.dsk") == "");
  CHECK(Path::sanitize_filename("in\x7Fvalid.dsk") == "");
}
