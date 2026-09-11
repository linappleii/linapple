// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TestFixtures {

inline auto get_fixture_path(const std::string& filename) -> std::string {
#ifdef TEST_FIXTURES_DIR
  std::string p = std::string(TEST_FIXTURES_DIR) + "/" + filename;
  if (access(p.c_str(), R_OK) == 0) {
    return p;
  }
#endif
  for (const auto* prefix :
       {"tests/fixtures/", "../tests/fixtures/", "../../tests/fixtures/",
        "../../../tests/fixtures/"}) {
    std::string candidate = std::string(prefix) + filename;
    if (access(candidate.c_str(), R_OK) == 0) {
      return candidate;
    }
  }
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

}  // namespace TestFixtures
