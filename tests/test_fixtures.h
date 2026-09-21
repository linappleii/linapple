// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TestFixtures {

/**
 * @brief Loads a configuration file into the Configuration_t singleton.
 *
 * Declared rather than defined because this header is also included by test
 * targets that deliberately do not link the core (test-fixtures,
 * test-audio-dumper). A fixture that never calls ScopedTestConfig_t::load()
 * never references this symbol.
 */
auto load_configuration_file(const std::string& path) -> bool;

inline auto get_fixture_path(const std::string& filename) -> std::string {
#ifdef TEST_FIXTURES_DIR
  std::string p = std::string(TEST_FIXTURES_DIR) + "/" + filename;
  if (access(p.c_str(), R_OK) == 0) {
    return p;
  }
#endif
#ifdef SOURCE_RES_DIR
  std::string p_res = std::string(SOURCE_RES_DIR) + "/" + filename;
  if (access(p_res.c_str(), R_OK) == 0) {
    return p_res;
  }
#endif
  return filename;
}

/**
 * @brief RAII ephemeral copy of a test disk fixture.
 *
 * Ensures test isolation by cloning the source disk template to a temporary
 * file and automatically unlinking it upon destruction.
 */
struct EphemeralDiskFixture_t {
  std::string temp_path;

  explicit EphemeralDiskFixture_t(const std::string& fixture_name) {
    std::string src_path = get_fixture_path(fixture_name);
    if (access(src_path.c_str(), R_OK) != 0) {
      throw std::runtime_error(
          "Source fixture template not found or not readable: " + fixture_name);
    }

    std::string ext;
    size_t dot_pos = fixture_name.find_last_of('.');
    if (dot_pos != std::string::npos) {
      ext = fixture_name.substr(dot_pos);
    }

    const char* tmpdir = getenv("TMPDIR");
    std::string base_dir =
        (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    if (base_dir.back() != '/') {
      base_dir += '/';
    }

    std::string pattern = base_dir + "linapple_test_XXXXXX" + ext;
    std::vector<char> template_buf(pattern.begin(), pattern.end());
    template_buf.push_back('\0');

    int fd = mkstemps(template_buf.data(), static_cast<int>(ext.length()));
    if (fd < 0) {
      throw std::runtime_error(
          "Failed to create temporary file for disk fixture: " + fixture_name);
    }
    ::close(fd);

    temp_path = template_buf.data();

    std::ifstream src(src_path, std::ios::binary);
    if (!src.is_open()) {
      cleanup();
      throw std::runtime_error("Failed to open source fixture template: " +
                               src_path);
    }

    std::ofstream dst(temp_path, std::ios::binary | std::ios::trunc);
    if (!dst.is_open()) {
      cleanup();
      throw std::runtime_error("Failed to open destination temp file: " +
                               temp_path);
    }

    if (src.peek() != std::ifstream::traits_type::eof()) {
      dst << src.rdbuf();
      if (!dst.good()) {
        cleanup();
        throw std::runtime_error("Failed to copy fixture content to: " +
                                 temp_path);
      }
    }
  }

  ~EphemeralDiskFixture_t() { cleanup(); }

  // Non-copyable (enforcing single ownership)
  EphemeralDiskFixture_t(const EphemeralDiskFixture_t&) = delete;
  auto operator=(const EphemeralDiskFixture_t&)
      -> EphemeralDiskFixture_t& = delete;

  // Move-constructible
  EphemeralDiskFixture_t(EphemeralDiskFixture_t&& other) noexcept
      : temp_path(std::move(other.temp_path)) {
    other.temp_path.clear();
  }

  // Move-assignable
  auto operator=(EphemeralDiskFixture_t&& other) noexcept
      -> EphemeralDiskFixture_t& {
    if (this != &other) {
      cleanup();
      temp_path = std::move(other.temp_path);
      other.temp_path.clear();
    }
    return *this;
  }

  auto path() const -> const std::string& { return temp_path; }
  auto c_str() const -> const char* { return temp_path.c_str(); }

  auto release() -> std::string {
    std::string p = std::move(temp_path);
    temp_path.clear();
    return p;
  }

  auto cleanup() -> void {
    if (!temp_path.empty()) {
      unlink(temp_path.c_str());
      temp_path.clear();
    }
  }

  EphemeralDiskFixture_t() = default;

  static auto create_blank(const std::string& filename, size_t size_bytes)
      -> EphemeralDiskFixture_t {
    std::string ext;
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos != std::string::npos) {
      ext = filename.substr(dot_pos);
    }

    const char* tmpdir = getenv("TMPDIR");
    std::string base_dir =
        (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    if (base_dir.back() != '/') {
      base_dir += '/';
    }

    std::string pattern = base_dir + "linapple_test_XXXXXX" + ext;
    std::vector<char> template_buf(pattern.begin(), pattern.end());
    template_buf.push_back('\0');

    int fd = mkstemps(template_buf.data(), static_cast<int>(ext.length()));
    if (fd < 0) {
      throw std::runtime_error(
          "Failed to create temporary file for blank disk fixture");
    }

    if (size_bytes > 0) {
      if (ftruncate(fd, static_cast<off_t>(size_bytes)) != 0) {
        ::close(fd);
        unlink(template_buf.data());
        throw std::runtime_error("Failed to set size for blank disk fixture");
      }
    }
    ::close(fd);

    EphemeralDiskFixture_t fixture;
    fixture.temp_path = template_buf.data();
    return fixture;
  }
};

inline auto create_ephemeral(const std::string& fixture_name)
    -> EphemeralDiskFixture_t {
  return EphemeralDiskFixture_t(fixture_name);
}

inline auto create_ephemeral_blank(const std::string& filename,
                                   size_t size_bytes)
    -> EphemeralDiskFixture_t {
  return EphemeralDiskFixture_t::create_blank(filename, size_bytes);
}

/**
 * @brief RAII scoped temporary file in /tmp.
 *
 * Creates a unique empty temporary file with an optional suffix and unlinks it
 * upon destruction.
 */
class ScopedTempFile_t {
 private:
  std::string path_;

 public:
  explicit ScopedTempFile_t(const std::string& ext = "") {
    const char* tmpdir = std::getenv("TMPDIR");
    std::string base_dir =
        (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    if (base_dir.back() != '/') {
      base_dir += '/';
    }

    std::string pattern = base_dir + "linapple_test_XXXXXX" + ext;
    std::vector<char> template_buf(pattern.begin(), pattern.end());
    template_buf.push_back('\0');

    int fd = mkstemps(template_buf.data(), static_cast<int>(ext.length()));
    if (fd >= 0) {
      ::close(fd);
      path_ = template_buf.data();
    }
  }

  ~ScopedTempFile_t() { unlink_file(); }

  ScopedTempFile_t(const ScopedTempFile_t&) = delete;
  auto operator=(const ScopedTempFile_t&) -> ScopedTempFile_t& = delete;

  ScopedTempFile_t(ScopedTempFile_t&& other) noexcept
      : path_(std::move(other.path_)) {
    other.path_.clear();
  }

  auto operator=(ScopedTempFile_t&& other) noexcept -> ScopedTempFile_t& {
    if (this != &other) {
      unlink_file();
      path_ = std::move(other.path_);
      other.path_.clear();
    }
    return *this;
  }

  auto path() const -> const std::string& { return path_; }
  auto c_str() const -> const char* { return path_.c_str(); }

  auto unlink_file() -> void {
    if (!path_.empty()) {
      unlink(path_.c_str());
      path_.clear();
    }
  }
};

/**
 * @brief RAII scoped temporary directory.
 *
 * Creates a unique empty temporary directory under TMPDIR or /tmp and
 * recursively removes all child files and subdirectories upon destruction.
 */
class ScopedTempDir_t {
 private:
  std::string path_;

  static auto remove_all(const std::string& dir_path) -> void {
    DIR* dir = opendir(dir_path.c_str());
    if (dir == nullptr) {
      return;
    }
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }
      std::string child_path = dir_path + "/" + entry->d_name;
      struct stat st{};
      if (lstat(child_path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
          remove_all(child_path);
        } else {
          unlink(child_path.c_str());
        }
      }
    }
    closedir(dir);
    rmdir(dir_path.c_str());
  }

 public:
  explicit ScopedTempDir_t(
      const std::string& prefix = "linapple_browser_test_") {
    const char* tmpdir = std::getenv("TMPDIR");
    std::string base_dir =
        (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    if (base_dir.back() != '/') {
      base_dir += '/';
    }

    std::string pattern = base_dir + prefix + "XXXXXX";
    std::vector<char> template_buf(pattern.begin(), pattern.end());
    template_buf.push_back('\0');

    char* created_dir = mkdtemp(template_buf.data());
    if (created_dir == nullptr) {
      throw std::runtime_error("Failed to create temporary directory: " +
                               pattern);
    }
    path_ = created_dir;
  }

  ~ScopedTempDir_t() { cleanup(); }

  ScopedTempDir_t(const ScopedTempDir_t&) = delete;
  auto operator=(const ScopedTempDir_t&) -> ScopedTempDir_t& = delete;

  ScopedTempDir_t(ScopedTempDir_t&& other) noexcept
      : path_(std::move(other.path_)) {
    other.path_.clear();
  }

  auto operator=(ScopedTempDir_t&& other) noexcept -> ScopedTempDir_t& {
    if (this != &other) {
      cleanup();
      path_ = std::move(other.path_);
      other.path_.clear();
    }
    return *this;
  }

  auto path() const -> const std::string& { return path_; }
  auto c_str() const -> const char* { return path_.c_str(); }

  auto cleanup() -> void {
    if (!path_.empty()) {
      remove_all(path_);
      path_.clear();
    }
  }
};

class ScopedEnvVar_t {
 private:
  std::string name_;
  std::string previous_;
  bool had_previous_ = false;

 public:
  ScopedEnvVar_t(const char* name, const std::string& value) : name_(name) {
    const char* prev = std::getenv(name);
    had_previous_ = (prev != nullptr);
    if (had_previous_) {
      previous_ = prev;
    }
    ::setenv(name, value.c_str(), 1);
  }

  ~ScopedEnvVar_t() {
    if (had_previous_) {
      ::setenv(name_.c_str(), previous_.c_str(), 1);
    } else {
      ::unsetenv(name_.c_str());
    }
  }

  ScopedEnvVar_t(const ScopedEnvVar_t&) = delete;
  auto operator=(const ScopedEnvVar_t&) -> ScopedEnvVar_t& = delete;
  ScopedEnvVar_t(ScopedEnvVar_t&&) = delete;
  auto operator=(ScopedEnvVar_t&&) -> ScopedEnvVar_t& = delete;
};

/**
 * @brief RAII hermetic emulator configuration for a single test.
 *
 * Each test declares the machine it needs and nothing more. Without this, the
 * developer's own ~/.config/linapple/linapple.conf becomes the machine under
 * test, while CI — having none — gets the Registry defaults instead, which
 * put Mockingboards in slots 4 and 5, a Disk II in 6 and a Harddisk in 7.
 * The ambient file also decides the machine type, auto-mounts image paths
 * from the developer's filesystem, and sets disk turbo.
 *
 * The config is written into a temporary directory and the XDG variables are
 * pointed at it, so nothing under $HOME is read or created. Any slot the
 * description leaves blank is written as None.
 */
class ScopedTestConfig_t {
 public:
  // Not the A2TYPE_* enum; these are the config file's own integers.
  enum MachineType_t {
    machine_apple2 = 0,
    machine_apple2_plus = 1,
    machine_apple2e = 2,
    machine_apple2e_enhanced = 3
  };

  enum { slot_count = 7 };

  struct Entry_t {
    std::string section;
    std::string key;
    std::string value;
  };

  struct Description_t {
    int machine_type = machine_apple2e_enhanced;
    // Index 0 is Slot 1. An empty entry means None.
    std::array<std::string, slot_count> slots;
    std::vector<Entry_t> extras;
  };

  // An Enhanced //e with nothing in any slot: the internal speaker, keyboard
  // and joystick, and no card anywhere.
  static auto enhanced_2e_only() -> Description_t { return Description_t(); }

  // Disk turbo is stated rather than inherited from whatever config happens
  // to be on the machine running the suite.
  static auto disk_ii_only() -> Description_t {
    Description_t description;
    description.slots[5] = "Disk II";
    description.extras.push_back({"Configuration", "Disk Turbo", "1"});
    return description;
  }

  explicit ScopedTestConfig_t(const Description_t& description)
      : dir_("linapple_test_config_"),
        path_(dir_.path() + "/linapple.conf"),
        config_home_("XDG_CONFIG_HOME", dir_.path()),
        data_home_("XDG_DATA_HOME", dir_.path()),
        config_dirs_("XDG_CONFIG_DIRS", dir_.path()) {
    std::ofstream out(path_, std::ios::trunc);
    if (!out.is_open()) {
      throw std::runtime_error("Failed to write test configuration: " + path_);
    }

    out << "[Configuration]\n";
    out << "Computer Emulation = " << description.machine_type << "\n";

    out << "\n[Slots]\n";
    for (size_t i = 0; i < description.slots.size(); ++i) {
      const std::string& name = description.slots[i];
      out << "Slot " << (i + 1) << " = " << (name.empty() ? "None" : name)
          << "\n";
    }

    // Each extra repeats its own section header. The INI parser simply
    // switches the current section, so grouping is unnecessary.
    for (const Entry_t& entry : description.extras) {
      out << "\n[" << entry.section << "]\n"
          << entry.key << " = " << entry.value << "\n";
    }

    out.flush();
    if (!out.good()) {
      throw std::runtime_error("Failed to write test configuration: " + path_);
    }
  }

  ScopedTestConfig_t(const ScopedTestConfig_t&) = delete;
  auto operator=(const ScopedTestConfig_t&) -> ScopedTestConfig_t& = delete;
  ScopedTestConfig_t(ScopedTestConfig_t&&) = delete;
  auto operator=(ScopedTestConfig_t&&) -> ScopedTestConfig_t& = delete;

  auto path() const -> const std::string& { return path_; }
  auto c_str() const -> const char* { return path_.c_str(); }

  // For fixtures that populate the Configuration_t singleton themselves
  // rather than going through AppController.
  auto load() const -> bool { return load_configuration_file(path_); }

 private:
  ScopedTempDir_t dir_;
  std::string path_;
  ScopedEnvVar_t config_home_;
  ScopedEnvVar_t data_home_;
  ScopedEnvVar_t config_dirs_;
};

}  // namespace TestFixtures
