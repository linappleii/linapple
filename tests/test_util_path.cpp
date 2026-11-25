// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

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
  FilePtr file(tmp, fclose);

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

namespace {

struct ScopedEnvVar {
  std::string name;
  bool had_original = false;
  std::string original_value;

  ScopedEnvVar(std::string var_name, const std::string& new_value)
      : name(std::move(var_name)) {
    const char* current = std::getenv(name.c_str());
    if (current != nullptr) {
      had_original = true;
      original_value = current;
    }
    setenv(name.c_str(), new_value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if (had_original) {
      setenv(name.c_str(), original_value.c_str(), 1);
    } else {
      unsetenv(name.c_str());
    }
  }

  ScopedEnvVar(const ScopedEnvVar&) = delete;
  auto operator=(const ScopedEnvVar&) -> ScopedEnvVar& = delete;
  ScopedEnvVar(ScopedEnvVar&&) = delete;
  auto operator=(ScopedEnvVar&&) -> ScopedEnvVar& = delete;
};

struct ScopedTempDataDir {
  std::string root;
  std::string app_dir;
  std::string probe_file;

  ScopedTempDataDir() {
    std::array<char, 64> tmpl = {"/tmp/linapple_path_test_XXXXXX"};
    const char* created = mkdtemp(tmpl.data());
    if (created != nullptr) {
      root = created;
      app_dir = Path::join(root, "linapple");
      Path::ensure_dir_exists(app_dir);
      probe_file = Path::join(app_dir, "probe.dat");
    }
  }

  ~ScopedTempDataDir() {
    if (!probe_file.empty()) {
      std::remove(probe_file.c_str());
    }
    if (!app_dir.empty()) {
      rmdir(app_dir.c_str());
    }
    if (!root.empty()) {
      rmdir(root.c_str());
    }
  }

  ScopedTempDataDir(const ScopedTempDataDir&) = delete;
  auto operator=(const ScopedTempDataDir&) -> ScopedTempDataDir& = delete;
  ScopedTempDataDir(ScopedTempDataDir&&) = delete;
  auto operator=(ScopedTempDataDir&&) -> ScopedTempDataDir& = delete;
};

}  // namespace

TEST_CASE(
    "Util_Path: get_data_search_paths includes ASSET_DIR and SYSCONF_DIR in "
    "precedence order") {
  const std::vector<std::string> paths = Path::get_data_search_paths();

  const std::string last_relative =
      Path::join(Path::get_executable_dir(), "../../etc/linapple/");
  const auto it_relative = std::find(paths.begin(), paths.end(), last_relative);
  const auto it_asset =
      std::find(paths.begin(), paths.end(), "/opt/linapple-test-assets/");
  const auto it_sysconf =
      std::find(paths.begin(), paths.end(), "/etc/linapple-test-conf/");
  const auto it_usr_local =
      std::find(paths.begin(), paths.end(), "/usr/local/share/linapple/");

  REQUIRE(it_relative != paths.end());
  REQUIRE(it_asset != paths.end());
  REQUIRE(it_sysconf != paths.end());
  REQUIRE(it_usr_local != paths.end());

  CHECK(it_relative < it_asset);
  CHECK(it_asset < it_sysconf);
  CHECK(it_sysconf < it_usr_local);
}

TEST_CASE(
    "Util_Path: find_data_file resolves files in XDG_DATA_HOME and returns "
    "empty for missing files") {
  ScopedTempDataDir temp;
  REQUIRE_FALSE(temp.root.empty());

  {
    const FilePtr file(std::fopen(temp.probe_file.c_str(), "wb"), std::fclose);
    REQUIRE(file != nullptr);
    const char payload[] = "ok";
    REQUIRE(std::fwrite(payload, 1, sizeof(payload) - 1, file.get()) ==
            sizeof(payload) - 1);
  }

  const ScopedEnvVar xdg_data_home("XDG_DATA_HOME", temp.root);
  CHECK(Path::find_data_file("probe.dat") == temp.probe_file);
  CHECK(Path::find_data_file("nonexistent_probe_file_xyz.dat").empty());
}
