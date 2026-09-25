// SPDX-License-Identifier: GPL-2.0-only
#include <dirent.h>

#include "doctest.h"

TEST_CASE("FTPClient: Parameter Validation") {
  FtpClient_t client;
  TestFixtures::ScopedTempDir_t temp_dir("linapple_ftp_val_");
  REQUIRE_FALSE(temp_dir.path().empty());

  SUBCASE("Empty remote URL") {
    const FtpStatus_t status =
        client.download_file("", temp_dir.path(), "disk.dsk");
    CHECK(status == FtpStatus_t::invalid_param);
  }

  SUBCASE("Empty local cache dir") {
    const FtpStatus_t status =
        client.download_file("ftp://example.com/disk.dsk", "", "disk.dsk");
    CHECK(status == FtpStatus_t::invalid_param);
  }

  SUBCASE("Empty filename") {
    const FtpStatus_t status =
        client.download_file("ftp://example.com/disk.dsk", temp_dir.path(), "");
    CHECK(status == FtpStatus_t::invalid_param);
  }

  SUBCASE("Empty directory URL for listing") {
    std::vector<FtpFileEntry_t> entries;
    const FtpStatus_t status = client.fetch_directory_listing("", entries);
    CHECK(status == FtpStatus_t::invalid_param);
  }
}

TEST_CASE("FTPClient: Path Traversal Defense") {
  FtpClient_t client;
  TestFixtures::ScopedTempDir_t temp_dir("linapple_ftp_trav_");
  REQUIRE_FALSE(temp_dir.path().empty());

  SUBCASE("Parent directory traversal via ..") {
    const FtpStatus_t status = client.download_file(
        "ftp://example.com/disk.dsk", temp_dir.path(), "../escaped.dsk");
    CHECK(status == FtpStatus_t::path_traversal_rejected);
  }

  SUBCASE("Absolute path parameter") {
    const FtpStatus_t status = client.download_file(
        "ftp://example.com/disk.dsk", temp_dir.path(), "/tmp/evil.dsk");
    CHECK(status == FtpStatus_t::path_traversal_rejected);
  }

  SUBCASE("Nested slash path traversal") {
    const FtpStatus_t status = client.download_file(
        "ftp://example.com/disk.dsk", temp_dir.path(), "subdir/../file.dsk");
    CHECK(status == FtpStatus_t::path_traversal_rejected);
  }

  SUBCASE("Backslash traversal") {
    const FtpStatus_t status = client.download_file(
        "ftp://example.com/disk.dsk", temp_dir.path(), "..\\evil.dsk");
    CHECK(status == FtpStatus_t::path_traversal_rejected);
  }

  SUBCASE("Backslash with no parent reference") {
    const FtpStatus_t status = client.download_file(
        "ftp://example.com/disk.dsk", temp_dir.path(), "sub\\evil.dsk");
    CHECK(status == FtpStatus_t::path_traversal_rejected);
  }

  SUBCASE("Parent reference in the cache directory") {
    const FtpStatus_t status = client.download_file(
        "ftp://example.com/disk.dsk", temp_dir.path() + "/../", "disk.dsk");
    CHECK(status == FtpStatus_t::path_traversal_rejected);
  }

  SUBCASE("Hidden dotfile rejection") {
    const FtpStatus_t status = client.download_file("ftp://example.com/.bashrc",
                                                    temp_dir.path(), ".bashrc");
    CHECK(status == FtpStatus_t::path_traversal_rejected);
  }

  SUBCASE("Control character rejection") {
    const FtpStatus_t status = client.download_file(
        "ftp://example.com/bad.dsk", temp_dir.path(), "in\x01valid.dsk");
    CHECK(status == FtpStatus_t::path_traversal_rejected);
  }

  SUBCASE("ASCII delete character rejection") {
    const FtpStatus_t status = client.download_file(
        "ftp://example.com/bad.dsk", temp_dir.path(), "del\x7f.dsk");
    CHECK(status == FtpStatus_t::path_traversal_rejected);
  }
}

TEST_CASE("FTPClient: Atomic Download Staging and Cleanup on Failure") {
  FtpClient_t client;
  TestFixtures::ScopedTempDir_t temp_dir("linapple_ftp_stage_");
  REQUIRE_FALSE(temp_dir.path().empty());

  // Using an unsupported scheme (http) ensures libcurl rejects the transfer
  // immediately via CURLOPT_PROTOCOLS_STR ("ftp,ftps") without attempting a
  // live network socket connect, triggering the RAII staging guard cleanup
  // deterministically and offline.
  const FtpStatus_t status = client.download_file("http://localhost/test.dsk",
                                                  temp_dir.path(), "test.dsk");
  CHECK(status == FtpStatus_t::transfer_failed);

  // Verify that neither the target file nor any orphan staging (.part) files
  // remain.
  DIR* dir = opendir(temp_dir.c_str());
  REQUIRE(dir != nullptr);
  size_t file_count = 0;
  struct dirent* entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    if (std::strcmp(entry->d_name, ".") != 0 &&
        std::strcmp(entry->d_name, "..") != 0) {
      ++file_count;
    }
  }
  closedir(dir);
  CHECK(file_count == 0);
}

TEST_CASE("FTPClient: URL Encoding and Slash Collapsing") {
  FtpClient_t client;
  TestFixtures::ScopedTempDir_t temp_dir("linapple_ftp_spaces_");
  REQUIRE_FALSE(temp_dir.path().empty());

  // Verify that a URL containing unencoded spaces and consecutive slashes is
  // properly encoded and processed by curl rather than failing with
  // CURLE_URL_MALFORMAT.
  const FtpStatus_t status = client.download_file(
      "ftp://127.0.0.1:1//driving/test_drive//Test Drive - 1.dsk",
      temp_dir.path(), "Test Drive - 1.dsk");

  // Port 1 is closed, so connect_error is expected, but crucially NOT
  // invalid_param
  CHECK(status != FtpStatus_t::invalid_param);
  CHECK(
      (status == FtpStatus_t::connect_error || status == FtpStatus_t::timeout));
}
