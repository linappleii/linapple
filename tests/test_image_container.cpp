// SPDX-License-Identifier: GPL-2.0-only
#include <dirent.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>
#include <zip.h>
#include <zlib.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/media/image_container/ImageContainer.h"
#include "core/Util_Path.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr size_t macbinary_header_len = 128;
constexpr size_t macbinary_crc_offset = 124;
constexpr size_t dsk_image_size = 143360;
constexpr size_t temp_path_len = 512;
// Wide enough that any plausible image passes on size alone, so only the
// ratio can refuse; the value is the caller's, not the library's.
constexpr size_t generous_threshold = 4 * 1024 * 1024;

// CRC-16 of the MacBinary II standard: polynomial 0x1021, initial value 0.
auto macbinary_crc16(const uint8_t* data, size_t length) -> uint16_t {
  uint16_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
    for (int bit = 0; bit < 8; ++bit) {
      crc = ((crc & 0x8000) != 0) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                  : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

auto read_file(const std::string& path) -> std::vector<uint8_t> {
  FilePtr_t f(fopen(path.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  std::vector<uint8_t> data;
  std::array<uint8_t, 16384> chunk{};
  size_t got = 0;
  while ((got = fread(chunk.data(), 1, chunk.size(), f.get())) > 0) {
    data.insert(data.end(), chunk.begin(), chunk.begin() + got);
  }
  return data;
}

auto write_file(const std::string& path, const std::vector<uint8_t>& data)
    -> void {
  FilePtr_t f(fopen(path.c_str(), "wb"), fclose);
  REQUIRE(f != nullptr);
  REQUIRE(fwrite(data.data(), 1, data.size(), f.get()) == data.size());
  REQUIRE(fclose(f.release()) == 0);
}

struct ScopedExtractedFile_t {
  std::array<char, temp_path_len> path{};
  bool is_temporary = false;

  ScopedExtractedFile_t() = default;
  ~ScopedExtractedFile_t() {
    if (is_temporary && path[0] != '\0') {
      unlink(path.data());
    }
  }
  ScopedExtractedFile_t(const ScopedExtractedFile_t&) = delete;
  auto operator=(const ScopedExtractedFile_t&)
      -> ScopedExtractedFile_t& = delete;
  ScopedExtractedFile_t(ScopedExtractedFile_t&&) = delete;
  auto operator=(ScopedExtractedFile_t&&) -> ScopedExtractedFile_t& = delete;

  auto prepare(const std::string& archive,
               size_t threshold = generous_threshold) -> ImageContainerError_e {
    return image_container_prepare_compressed_path(
        archive.c_str(), path.data(), path.size(), threshold, &is_temporary);
  }
};

auto payload_name_of(const std::string& archive) -> std::string {
  std::array<char, 256> name{};
  REQUIRE(image_container_payload_name(archive.c_str(), name.data(),
                                       name.size()) == image_container_ok);
  return std::string(name.data());
}

auto count_container_temps(const std::string& dir) -> int {
  DIR* handle = opendir(dir.c_str());
  REQUIRE(handle != nullptr);
  int count = 0;
  for (dirent* entry = readdir(handle); entry != nullptr;
       entry = readdir(handle)) {
    if (strncmp(entry->d_name, "linapple_", 9) == 0) {
      ++count;
    }
  }
  closedir(handle);
  return count;
}

auto write_gz(const std::string& path, const std::vector<uint8_t>& payload)
    -> void {
  gzFile gz = gzopen(path.c_str(), "wb");
  REQUIRE(gz != nullptr);
  REQUIRE(
      gzwrite(gz, payload.data(), static_cast<unsigned int>(payload.size())) ==
      static_cast<int>(payload.size()));
  REQUIRE(gzclose(gz) == Z_OK);
}

// Caps the size a file may grow to for the enclosing scope. The kernel also
// raises SIGXFSZ at the cap, which would kill the test, so it is ignored for
// the same scope.
struct ScopedFileSizeLimit_t {
  struct rlimit saved{};
  void (*saved_handler)(int) = nullptr;

  explicit ScopedFileSizeLimit_t(rlim_t limit) {
    REQUIRE(getrlimit(RLIMIT_FSIZE, &saved) == 0);
    saved_handler = signal(SIGXFSZ, SIG_IGN);
    REQUIRE(saved_handler != SIG_ERR);
    struct rlimit capped = saved;
    capped.rlim_cur = limit;
    REQUIRE(setrlimit(RLIMIT_FSIZE, &capped) == 0);
  }
  ~ScopedFileSizeLimit_t() {
    setrlimit(RLIMIT_FSIZE, &saved);
    signal(SIGXFSZ, saved_handler);
  }
  ScopedFileSizeLimit_t(const ScopedFileSizeLimit_t&) = delete;
  auto operator=(const ScopedFileSizeLimit_t&)
      -> ScopedFileSizeLimit_t& = delete;
  ScopedFileSizeLimit_t(ScopedFileSizeLimit_t&&) = delete;
  auto operator=(ScopedFileSizeLimit_t&&) -> ScopedFileSizeLimit_t& = delete;
};

}  // namespace

TEST_CASE("ImageContainer: a zip archive names the image it carries") {
  const std::string archive = TestFixtures::get_fixture_path("minimal.dsk.zip");
  CHECK(payload_name_of(archive) == "minimal.dsk");
}

TEST_CASE("ImageContainer: [MB-1] a MacBinary II header is known by its CRC") {
  std::vector<uint8_t> header =
      read_file(TestFixtures::get_fixture_path("minimal-macbinary.dsk"));
  REQUIRE(header.size() == dsk_image_size + macbinary_header_len);
  header.resize(macbinary_header_len);
  REQUIRE(header[122] == 0x81);
  REQUIRE(header[123] == 0x81);
  REQUIRE(header[124] == 0x34);
  REQUIRE(header[125] == 0x6C);
  CHECK(macbinary_crc16(header.data(), macbinary_crc_offset) == 0x346C);

  const auto wrapped_size =
      static_cast<uint32_t>(dsk_image_size + macbinary_header_len);
  CHECK(image_container_detect_macbinary(header.data(), header.size(),
                                         wrapped_size) ==
        image_container_macbinary_header_len);

  SUBCASE("a MacBinary III writer version is accepted once the CRC agrees") {
    header[122] = 0x82;
    CHECK(image_container_detect_macbinary(header.data(), header.size(),
                                           wrapped_size) == 0);
    const uint16_t crc = macbinary_crc16(header.data(), macbinary_crc_offset);
    header[124] = static_cast<uint8_t>(crc >> 8);
    header[125] = static_cast<uint8_t>(crc & 0xFF);
    CHECK(image_container_detect_macbinary(header.data(), header.size(),
                                           wrapped_size) == 128);
  }
  SUBCASE("one wrong CRC byte is a refusal") {
    header[125] ^= 0x01;
    CHECK(image_container_detect_macbinary(header.data(), header.size(),
                                           wrapped_size) == 0);
  }
  SUBCASE("a reader version other than II is a refusal") {
    header[123] = 0x82;
    CHECK(image_container_detect_macbinary(header.data(), header.size(),
                                           wrapped_size) == 0);
  }
  SUBCASE("a non-zero byte at 74 or 82 is a refusal") {
    header[74] = 1;
    CHECK(image_container_detect_macbinary(header.data(), header.size(),
                                           wrapped_size) == 0);
    header[74] = 0;
    header[82] = 1;
    CHECK(image_container_detect_macbinary(header.data(), header.size(),
                                           wrapped_size) == 0);
  }
  SUBCASE("a MacBinary I header, zero at 122 and 123, is not recognised") {
    header[122] = 0;
    header[123] = 0;
    header[124] = 0;
    header[125] = 0;
    CHECK(image_container_detect_macbinary(header.data(), header.size(),
                                           wrapped_size) == 0);
  }
  SUBCASE("a header that is all the file there is wraps nothing") {
    CHECK(image_container_detect_macbinary(header.data(), header.size(),
                                           macbinary_header_len) == 0);
  }
  SUBCASE("a short header or a null one wraps nothing") {
    CHECK(image_container_detect_macbinary(
              header.data(), macbinary_header_len - 1, wrapped_size) == 0);
    CHECK(image_container_detect_macbinary(nullptr, header.size(),
                                           wrapped_size) == 0);
  }
}

TEST_CASE(
    "ImageContainer: [MB-3] an image that merely looks like MacBinary I is "
    "not a wrapper") {
  const std::vector<uint8_t> image =
      read_file(TestFixtures::get_fixture_path("minimal-falsepositive.dsk"));
  REQUIRE(image.size() == dsk_image_size);
  REQUIRE(image[0] == 0x00);
  REQUIRE(image[1] == 0x05);
  REQUIRE(image[122] == 0x00);
  CHECK(image_container_detect_macbinary(image.data(), macbinary_header_len,
                                         static_cast<uint32_t>(image.size())) ==
        0);
}

TEST_CASE(
    "ImageContainer: [CT-1] gzip and zip archives extract the bare image") {
  const std::vector<uint8_t> bare =
      read_file(TestFixtures::get_fixture_path("minimal.dsk"));
  REQUIRE(bare.size() == dsk_image_size);

  const char* archive_name = "minimal.dsk.gz";
  SUBCASE("gzip") { archive_name = "minimal.dsk.gz"; }
  SUBCASE("zip") { archive_name = "minimal.dsk.zip"; }

  const std::string archive = TestFixtures::get_fixture_path(archive_name);
  CHECK(payload_name_of(archive) == "minimal.dsk");

  ScopedExtractedFile_t out;
  REQUIRE(out.prepare(archive) == image_container_ok);
  CHECK(out.is_temporary);
  CHECK(std::string(out.path.data()).find("/linapple_") != std::string::npos);
  const std::vector<uint8_t> extracted = read_file(out.path.data());
  CHECK(extracted.size() == dsk_image_size);
  CHECK(extracted == bare);
}

TEST_CASE(
    "ImageContainer: [CT-2] a zip's directory and AppleDouble entries are "
    "passed over") {
  const std::vector<uint8_t> bare =
      read_file(TestFixtures::get_fixture_path("minimal.dsk"));

  const char* archive_name = "minimal-dir-first.zip";
  SUBCASE("a directory entry first") { archive_name = "minimal-dir-first.zip"; }
  SUBCASE("a __MACOSX sidecar first") { archive_name = "minimal-macosx.zip"; }

  const std::string archive = TestFixtures::get_fixture_path(archive_name);
  CHECK(payload_name_of(archive) == "minimal.dsk");

  ScopedExtractedFile_t out;
  REQUIRE(out.prepare(archive) == image_container_ok);
  CHECK(out.is_temporary);
  const std::vector<uint8_t> extracted = read_file(out.path.data());
  CHECK(extracted.size() == dsk_image_size);
  CHECK(extracted == bare);
}

TEST_CASE(
    "ImageContainer: [CT-3] a zip with no file entry is corrupt, not "
    "extracted as nothing") {
  TestFixtures::ScopedTempFile_t archive(".zip");
  {
    int err = 0;
    zip* za = zip_open(archive.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(za != nullptr);
    REQUIRE(zip_dir_add(za, "folder", 0) >= 0);
    REQUIRE(zip_close(za) == 0);
  }
  ScopedExtractedFile_t out;
  out.is_temporary = true;
  out.path[0] = 'x';
  CHECK(out.prepare(archive.path()) == image_container_corrupt);
  CHECK_FALSE(out.is_temporary);
  CHECK(out.path[0] == '\0');

  std::array<char, 64> name{};
  name[0] = 'x';
  CHECK(image_container_payload_name(archive.c_str(), name.data(),
                                     name.size()) == image_container_corrupt);
  CHECK(name[0] == '\0');
}

TEST_CASE(
    "ImageContainer: [CT-4] a path or name that does not fit is an invalid "
    "argument, not truncated") {
  const std::string image = TestFixtures::get_fixture_path("minimal.dsk");
  std::vector<char> path(image.size() + 1, 'x');
  bool is_temporary = true;

  CHECK(image_container_prepare_compressed_path(
            image.c_str(), path.data(), image.size(), generous_threshold,
            &is_temporary) == image_container_invalid_argument);
  CHECK_FALSE(is_temporary);
  CHECK(path[0] == '\0');

  CHECK(image_container_prepare_compressed_path(
            image.c_str(), path.data(), image.size() + 1, generous_threshold,
            &is_temporary) == image_container_ok);
  CHECK_FALSE(is_temporary);
  CHECK(std::string(path.data()) == image);

  std::array<char, 12> name{};
  CHECK(image_container_payload_name(image.c_str(), name.data(), 11) ==
        image_container_invalid_argument);
  CHECK(name[0] == '\0');
  CHECK(image_container_payload_name(image.c_str(), name.data(), 12) ==
        image_container_ok);
  CHECK(std::string(name.data()) == "minimal.dsk");

  const std::string archive = TestFixtures::get_fixture_path("minimal.dsk.zip");
  CHECK(image_container_payload_name(archive.c_str(), name.data(), 11) ==
        image_container_invalid_argument);
  CHECK(image_container_payload_name(archive.c_str(), name.data(), 12) ==
        image_container_ok);
  CHECK(std::string(name.data()) == "minimal.dsk");

  SUBCASE("a null argument or an empty buffer is refused the same way") {
    CHECK(image_container_prepare_compressed_path(
              nullptr, path.data(), path.size(), generous_threshold,
              &is_temporary) == image_container_invalid_argument);
    CHECK(image_container_prepare_compressed_path(
              image.c_str(), nullptr, path.size(), generous_threshold,
              &is_temporary) == image_container_invalid_argument);
    CHECK(image_container_prepare_compressed_path(
              image.c_str(), path.data(), path.size(), generous_threshold,
              nullptr) == image_container_invalid_argument);
    is_temporary = true;
    CHECK(image_container_prepare_compressed_path(
              image.c_str(), path.data(), 0, generous_threshold,
              &is_temporary) == image_container_invalid_argument);
    CHECK_FALSE(is_temporary);
    CHECK(image_container_payload_name(nullptr, name.data(), name.size()) ==
          image_container_invalid_argument);
    CHECK(image_container_payload_name(image.c_str(), nullptr, name.size()) ==
          image_container_invalid_argument);
    CHECK(image_container_payload_name(image.c_str(), name.data(), 0) ==
          image_container_invalid_argument);
  }
}

TEST_CASE(
    "ImageContainer: [CT-5] a temporary that does not close cleanly is an "
    "io failure and is removed") {
  // Six full 16 KiB chunks and a 1,000-byte tail: the tail is smaller than any
  // stdio block, so it waits in the buffer for fclose, and a size cap 100
  // bytes short of the payload fails that final flush rather than a write.
  constexpr size_t payload_size = 6 * 16384 + 1000;
  std::vector<uint8_t> payload(payload_size);
  for (size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<uint8_t>(i * 7);
  }
  TestFixtures::ScopedTempFile_t archive(".dsk.gz");
  write_gz(archive.path(), payload);

  const TestFixtures::ScopedTempDir_t temp_dir("linapple_container_case_");
  const TestFixtures::ScopedEnvVar_t tmpdir("TMPDIR", temp_dir.path());
  REQUIRE(count_container_temps(temp_dir.path()) == 0);

  ScopedExtractedFile_t control;
  REQUIRE(control.prepare(archive.path()) == image_container_ok);
  CHECK(read_file(control.path.data()) == payload);
  REQUIRE(count_container_temps(temp_dir.path()) == 1);

  ScopedExtractedFile_t out;
  out.is_temporary = true;
  {
    const ScopedFileSizeLimit_t cap(payload_size - 100);
    CHECK(out.prepare(archive.path()) == image_container_io);
  }
  CHECK_FALSE(out.is_temporary);
  CHECK(out.path[0] == '\0');
  CHECK(count_container_temps(temp_dir.path()) == 1);
}

TEST_CASE(
    "ImageContainer: [IC-1] a TMPDIR that does not exist is an io failure "
    "and leaves nothing behind") {
  const TestFixtures::ScopedTempDir_t parent("linapple_container_case_");
  const TestFixtures::ScopedEnvVar_t tmpdir("TMPDIR",
                                            parent.path() + "/missing");

  const std::string archive = TestFixtures::get_fixture_path("minimal.dsk.gz");
  ScopedExtractedFile_t out;
  out.is_temporary = true;
  out.path[0] = 'x';
  CHECK(out.prepare(archive) == image_container_io);
  CHECK_FALSE(out.is_temporary);
  CHECK(out.path[0] == '\0');
  CHECK(count_container_temps(parent.path()) == 0);
}

TEST_CASE(
    "ImageContainer: [IC-2] an archive that does not exist is not_found") {
  const TestFixtures::ScopedTempDir_t dir("linapple_container_case_");
  const std::string missing_gz = dir.path() + "/absent.dsk.gz";
  const std::string missing_zip = dir.path() + "/absent.dsk.zip";

  ScopedExtractedFile_t out;
  CHECK(out.prepare(missing_gz) == image_container_not_found);
  CHECK(out.prepare(missing_zip) == image_container_not_found);
  CHECK_FALSE(out.is_temporary);
  CHECK(count_container_temps(dir.path()) == 0);

  // A gzip name is answered without opening the stream, a zip is not.
  std::array<char, 64> name{};
  CHECK(image_container_payload_name(missing_gz.c_str(), name.data(),
                                     name.size()) == image_container_ok);
  CHECK(std::string(name.data()) == "absent.dsk");
  CHECK(image_container_payload_name(missing_zip.c_str(), name.data(),
                                     name.size()) == image_container_not_found);
}

TEST_CASE(
    "ImageContainer: [IC-3] output past the threshold is refused by the "
    "ratio, not the threshold") {
  const TestFixtures::ScopedTempDir_t temp_dir("linapple_container_case_");
  const TestFixtures::ScopedEnvVar_t tmpdir("TMPDIR", temp_dir.path());

  // The deflated gzip of a near-empty image is far past 100:1, so a zero
  // threshold refuses it; the stored zip of the same image is 1:1, so the
  // same zero threshold lets it through whole.
  ScopedExtractedFile_t gz;
  gz.is_temporary = true;
  CHECK(gz.prepare(TestFixtures::get_fixture_path("minimal.dsk.gz"), 0) ==
        image_container_too_large);
  CHECK_FALSE(gz.is_temporary);
  CHECK(gz.path[0] == '\0');
  CHECK(count_container_temps(temp_dir.path()) == 0);

  ScopedExtractedFile_t stored;
  CHECK(stored.prepare(TestFixtures::get_fixture_path("minimal.dsk.zip"), 0) ==
        image_container_ok);
  CHECK(stored.is_temporary);
  CHECK(read_file(stored.path.data()).size() == dsk_image_size);
}

TEST_CASE(
    "ImageContainer: [IC-4] a damaged archive is corrupt and leaves nothing "
    "behind") {
  std::vector<uint8_t> gz_bytes =
      read_file(TestFixtures::get_fixture_path("minimal.dsk.gz"));
  REQUIRE(gz_bytes.size() > 64);
  gz_bytes.resize(gz_bytes.size() / 2);
  TestFixtures::ScopedTempFile_t truncated_gz(".dsk.gz");
  write_file(truncated_gz.path(), gz_bytes);

  TestFixtures::ScopedTempFile_t not_a_zip(".dsk.zip");
  write_file(not_a_zip.path(), std::vector<uint8_t>(1024, 0xA5));

  // The archives above live in the default TMPDIR; only the library's
  // temporaries land in this directory, so a count of it is a count of them.
  const TestFixtures::ScopedTempDir_t temp_dir("linapple_container_case_");
  const TestFixtures::ScopedEnvVar_t tmpdir("TMPDIR", temp_dir.path());

  ScopedExtractedFile_t out;
  out.is_temporary = true;
  CHECK(out.prepare(truncated_gz.path()) == image_container_corrupt);
  CHECK_FALSE(out.is_temporary);
  CHECK(out.path[0] == '\0');
  CHECK(out.prepare(not_a_zip.path()) == image_container_corrupt);
  CHECK_FALSE(out.is_temporary);
  CHECK(count_container_temps(temp_dir.path()) == 0);

  std::array<char, 64> name{};
  CHECK(image_container_payload_name(not_a_zip.c_str(), name.data(),
                                     name.size()) == image_container_corrupt);
}

TEST_CASE(
    "ImageContainer: [IC-5] a .gz that is not gzip passes through byte for "
    "byte") {
  std::vector<uint8_t> plain(4096);
  for (size_t i = 0; i < plain.size(); ++i) {
    plain[i] = static_cast<uint8_t>(i * 13 + 1);
  }
  TestFixtures::ScopedTempFile_t archive(".dsk.gz");
  write_file(archive.path(), plain);

  ScopedExtractedFile_t out;
  REQUIRE(out.prepare(archive.path()) == image_container_ok);
  CHECK(out.is_temporary);
  CHECK(read_file(out.path.data()) == plain);
}

TEST_CASE("ImageContainer: [IC-6] the extension list is gz then zip") {
  const char* const* exts = image_container_supported_extensions();
  REQUIRE(exts != nullptr);
  REQUIRE(exts[0] != nullptr);
  REQUIRE(exts[1] != nullptr);
  CHECK(std::string(exts[0]) == "gz");
  CHECK(std::string(exts[1]) == "zip");
  CHECK(exts[2] == nullptr);
}
