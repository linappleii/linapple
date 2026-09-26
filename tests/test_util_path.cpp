// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "core/Util_Path.h"
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

TEST_CASE("Util_Path: join handles empty components and separators") {
  CHECK(Path::join("", "file.txt") == "file.txt");
  CHECK(Path::join("/dir", "") == "/dir");
  CHECK(Path::join("", "") == "");
  CHECK(Path::join("/dir/", "file.txt") == "/dir/file.txt");
  CHECK(Path::join("/dir", "file.txt") == "/dir/file.txt");
  CHECK(Path::join("/dir/", "/file.txt") == "/dir/file.txt");
  CHECK(Path::join("/dir", "/file.txt") == "/dir/file.txt");
  CHECK(Path::join("dir", "sub/file.dsk") == "dir/sub/file.dsk");
}

TEST_CASE("Util_Path: find_data_file rejects empty filename") {
  CHECK(Path::find_data_file("").empty());
}

TEST_CASE(
    "Util_Path: ensure_dir_exists creates directories without trailing slash") {
  const std::string test_base = "/tmp/linapple_test_ensure_dir";
  const std::string test_sub = test_base + "/sub";
  const std::string test_leaf = test_sub + "/leaf";

  rmdir(test_leaf.c_str());
  rmdir(test_sub.c_str());
  rmdir(test_base.c_str());

  Path::ensure_dir_exists(test_leaf);

  struct stat st{};
  CHECK(stat(test_leaf.c_str(), &st) == 0);
  CHECK((st.st_mode & S_IFDIR) != 0);

  rmdir(test_leaf.c_str());
  rmdir(test_sub.c_str());
  rmdir(test_base.c_str());
}

TEST_CASE("Util_Path: file_size queries length and preserves seek position") {
  CHECK(Path::file_size(nullptr) == -1);

  FILE* tmp = std::tmpfile();
  REQUIRE(tmp != nullptr);
  FilePtr_t file(tmp, fclose);

  const char data[] = "0123456789abcdef";
  REQUIRE(std::fwrite(data, 1, sizeof(data), file.get()) == sizeof(data));

  // Verify size calculation
  CHECK(Path::file_size(file.get()) == static_cast<int64_t>(sizeof(data)));

  // Verify seek position preservation
  REQUIRE(std::fseek(file.get(), 4, SEEK_SET) == 0);
  CHECK(Path::file_size(file.get()) == static_cast<int64_t>(sizeof(data)));
  CHECK(std::ftell(file.get()) == 4);
}

TEST_CASE("Util_Path: system paths return non-empty directory strings") {
  CHECK_FALSE(Path::get_executable_dir().empty());
  CHECK(Path::get_executable_dir().back() == '/');

  CHECK_FALSE(Path::get_user_data_dir().empty());
  CHECK(Path::get_user_data_dir().back() == '/');

  CHECK_FALSE(Path::get_user_config_dir().empty());
  CHECK(Path::get_user_config_dir().back() == '/');

  CHECK_FALSE(Path::get_plugin_search_paths().empty());
  CHECK_FALSE(Path::get_data_search_paths().empty());
}
