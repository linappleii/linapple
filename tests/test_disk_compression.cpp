// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>
#include <zip.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/disk/formats/DiskContainer.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

struct ScopedExtractedFile_t {
  char path[512]{};
  bool is_temporary{false};

  ScopedExtractedFile_t() = default;
  ~ScopedExtractedFile_t() { cleanup(); }

  ScopedExtractedFile_t(const ScopedExtractedFile_t&) = delete;
  auto operator=(const ScopedExtractedFile_t&)
      -> ScopedExtractedFile_t& = delete;
  ScopedExtractedFile_t(ScopedExtractedFile_t&&) = delete;
  auto operator=(ScopedExtractedFile_t&&) -> ScopedExtractedFile_t& = delete;

  auto cleanup() -> void {
    if (is_temporary && path[0] != '\0') {
      unlink(path);
      path[0] = '\0';
      is_temporary = false;
    }
  }
};

auto create_test_zip(const char* zip_path, const char* entry_name,
                     const uint8_t* data, size_t size) -> bool {
  int err = 0;
  zip* za = zip_open(zip_path, ZIP_CREATE | ZIP_TRUNCATE, &err);
  if (za == nullptr) {
    return false;
  }
  zip_source* src = zip_source_buffer(za, data, size, 0);
  if (src == nullptr) {
    zip_close(za);
    return false;
  }
  if (zip_file_add(za, entry_name, src, ZIP_FL_OVERWRITE) < 0) {
    zip_source_free(src);
    zip_close(za);
    return false;
  }
  return zip_close(za) == 0;
}

auto create_test_gz(const char* gz_path, const uint8_t* data, size_t size)
    -> bool {
  gzFile gz = gzopen(gz_path, "wb");
  if (gz == nullptr) {
    return false;
  }
  if (size > 0) {
    if (gzwrite(gz, data, static_cast<unsigned int>(size)) <= 0) {
      gzclose(gz);
      return false;
    }
  }
  return gzclose(gz) == Z_OK;
}

}  // namespace

TEST_CASE("DiskCompression: [ZIP-1] Normal Floppy ZIP within 4MB is allowed") {
  TestFixtures::ScopedTempFile_t test_zip(".dsk.zip");
  const std::vector<uint8_t> floppy_data(143360, 0xA5);
  REQUIRE(create_test_zip(test_zip.c_str(), "disk.dsk", floppy_data.data(),
                          floppy_data.size()));

  ScopedExtractedFile_t out_file;
  const bool ok = disk_container_prepare_compressed_path(
      test_zip.c_str(), out_file.path, sizeof(out_file.path),
      disk_container::floppy_decompression_threshold, &out_file.is_temporary);

  CHECK(ok == true);
  CHECK(out_file.is_temporary == true);
}

TEST_CASE(
    "DiskCompression: [ZIP-2] All-zero Floppy up to 4MB is explicitly allowed "
    "despite extreme compression ratio") {
  TestFixtures::ScopedTempFile_t test_zip(".dsk.zip");
  // 3.5 MB of zeros compresses to ~3 KB (>1000:1 ratio)
  const std::vector<uint8_t> zeros(static_cast<size_t>(3.5 * 1024 * 1024),
                                   0x00);
  REQUIRE(create_test_zip(test_zip.c_str(), "blank.dsk", zeros.data(),
                          zeros.size()));

  ScopedExtractedFile_t out_file;
  const bool ok = disk_container_prepare_compressed_path(
      test_zip.c_str(), out_file.path, sizeof(out_file.path),
      disk_container::floppy_decompression_threshold, &out_file.is_temporary);

  CHECK(ok == true);
  CHECK(out_file.is_temporary == true);
}

TEST_CASE(
    "DiskCompression: [ZIP-3] All-zero Harddisk up to 32MB is explicitly "
    "allowed despite extreme compression ratio") {
  TestFixtures::ScopedTempFile_t test_zip(".po.zip");
  // 32 MB of zeros (standard ProDOS 32MB volume)
  const std::vector<uint8_t> zeros(32 * 1024 * 1024, 0x00);
  REQUIRE(create_test_zip(test_zip.c_str(), "volume.po", zeros.data(),
                          zeros.size()));

  ScopedExtractedFile_t out_file;
  const bool ok = disk_container_prepare_compressed_path(
      test_zip.c_str(), out_file.path, sizeof(out_file.path),
      disk_container::harddisk_decompression_threshold, &out_file.is_temporary);

  CHECK(ok == true);
  CHECK(out_file.is_temporary == true);
}

TEST_CASE(
    "DiskCompression: [ZIP-4] Floppy archive exceeding 4MB with ratio > 100:1 "
    "is blocked") {
  TestFixtures::ScopedTempFile_t test_zip(".dsk.zip");
  // 5 MB of zeros compresses to ~5 KB, which exceeds the 4 MB floppy gate and
  // exceeds 100:1 ratio
  const std::vector<uint8_t> zeros(5 * 1024 * 1024, 0x00);
  REQUIRE(create_test_zip(test_zip.c_str(), "bomb.dsk", zeros.data(),
                          zeros.size()));

  ScopedExtractedFile_t out_file;
  const bool ok = disk_container_prepare_compressed_path(
      test_zip.c_str(), out_file.path, sizeof(out_file.path),
      disk_container::floppy_decompression_threshold, &out_file.is_temporary);

  CHECK(ok == false);
}

TEST_CASE(
    "DiskCompression: [ZIP-5] Harddisk archive exceeding 32MB with ratio > "
    "100:1 is blocked") {
  TestFixtures::ScopedTempFile_t test_zip(".po.zip");
  // 34 MB of zeros exceeds the 32 MB harddisk gate and exceeds 100:1 ratio
  const std::vector<uint8_t> zeros(34 * 1024 * 1024, 0x00);
  REQUIRE(create_test_zip(test_zip.c_str(), "bomb_hd.po", zeros.data(),
                          zeros.size()));

  ScopedExtractedFile_t out_file;
  const bool ok = disk_container_prepare_compressed_path(
      test_zip.c_str(), out_file.path, sizeof(out_file.path),
      disk_container::harddisk_decompression_threshold, &out_file.is_temporary);

  CHECK(ok == false);
}

TEST_CASE(
    "DiskCompression: [GZ-1] Gzip decompression respects floppy vs harddisk "
    "thresholds") {
  TestFixtures::ScopedTempFile_t test_gz(".dsk.gz");
  // 5 MB of zeros: blocked for floppy (4 MB threshold), allowed for harddisk
  // (32 MB threshold)
  const std::vector<uint8_t> zeros(5 * 1024 * 1024, 0x00);
  REQUIRE(create_test_gz(test_gz.c_str(), zeros.data(), zeros.size()));

  // Floppy gate: 4MB -> blocked
  {
    ScopedExtractedFile_t floppy_out;
    const bool floppy_ok = disk_container_prepare_compressed_path(
        test_gz.c_str(), floppy_out.path, sizeof(floppy_out.path),
        disk_container::floppy_decompression_threshold,
        &floppy_out.is_temporary);
    CHECK(floppy_ok == false);
  }

  // Harddisk gate: 32MB -> allowed
  {
    ScopedExtractedFile_t hd_out;
    const bool hd_ok = disk_container_prepare_compressed_path(
        test_gz.c_str(), hd_out.path, sizeof(hd_out.path),
        disk_container::harddisk_decompression_threshold, &hd_out.is_temporary);
    CHECK(hd_ok == true);
    CHECK(hd_out.is_temporary == true);
  }
}
