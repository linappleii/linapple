// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>

#include "core/services/ftp/FtpParser.h"
#include "core/services/ftp/FtpTypes.h"
#include "doctest.h"

TEST_CASE("FTPParser: UNIX Standard File") {
  const char* line =
      "-rw-r--r--   1 1000     1000       143360 Sep 19 12:00 "
      "apple_dos.dsk\r\n";
  FtpFileEntry_t entry{};
  const bool ok = ftp_parse_line(line, std::strlen(line), entry);

  CHECK(ok);
  CHECK(entry.name == "apple_dos.dsk");
  CHECK(entry.type == FtpEntryType_t::file);
  CHECK(entry.size == 143360);
  CHECK(entry.mtime > 0);
}

TEST_CASE("FTPParser: UNIX Directory") {
  SUBCASE("Standard unix") {
    const char* line =
        "drwxr-xr-x   5 root     wheel          512 May 10 08:30 utilities\n";
    FtpFileEntry_t entry{};
    const bool ok = ftp_parse_line(line, std::strlen(line), entry);

    CHECK(ok);
    CHECK(entry.name == "utilities");
    CHECK(entry.type == FtpEntryType_t::directory);
    CHECK(entry.size == 512);
    CHECK(entry.mtime > 0);
  }

  SUBCASE("Asimov ProFTPD setgid directory") {
    const char* line =
        "drwxrwsr-x  75 apple2   apple2      45056 Jul 24  2025 action\r\n";
    FtpFileEntry_t entry{};
    const bool ok = ftp_parse_line(line, std::strlen(line), entry);

    CHECK(ok);
    CHECK(entry.name == "action");
    CHECK(entry.type == FtpEntryType_t::directory);
    CHECK(entry.size == 45056);
    CHECK(entry.mtime > 0);
  }
}

TEST_CASE("FTPParser: UNIX Symbolic Link") {
  const char* line =
      "lrwxrwxrwx   1 ftp      ftp            11 Sep 19 12:00 latest.dsk -> "
      "apple_dos.dsk\n";
  FtpFileEntry_t entry{};
  const bool ok = ftp_parse_line(line, std::strlen(line), entry);

  CHECK(ok);
  CHECK(entry.name == "latest.dsk");
  CHECK(entry.type == FtpEntryType_t::symlink);
}

TEST_CASE("FTPParser: EPLF Format") {
  const char* line = "+i8388621.29609,m825718503,r,s280\tftp.html\r\n";
  FtpFileEntry_t entry{};
  const bool ok = ftp_parse_line(line, std::strlen(line), entry);

  CHECK(ok);
  CHECK(entry.name == "ftp.html");
  CHECK(entry.type == FtpEntryType_t::file);
  CHECK(entry.size == 280);
  CHECK(entry.mtime == 825718503);
}

TEST_CASE("FTPParser: Windows NT Format") {
  SUBCASE("Directory") {
    const char* line = "05-18-00  01:00PM       <DIR>          games\r\n";
    FtpFileEntry_t entry{};
    const bool ok = ftp_parse_line(line, std::strlen(line), entry);

    CHECK(ok);
    CHECK(entry.name == "games");
    CHECK(entry.type == FtpEntryType_t::directory);
  }

  SUBCASE("File") {
    const char* line = "05-18-00  01:00PM               143360 master.dsk\r\n";
    FtpFileEntry_t entry{};
    const bool ok = ftp_parse_line(line, std::strlen(line), entry);

    CHECK(ok);
    CHECK(entry.name == "master.dsk");
    CHECK(entry.type == FtpEntryType_t::file);
    CHECK(entry.size == 143360);
  }
}

TEST_CASE("FTPParser: 64-bit Large File Sizes") {
  const char* line =
      "-rw-r--r--   1 ftp      ftp     5368709120 Sep 19 12:00 large_hdd.2mg\n";
  FtpFileEntry_t entry{};
  const bool ok = ftp_parse_line(line, std::strlen(line), entry);

  CHECK(ok);
  CHECK(entry.name == "large_hdd.2mg");
  CHECK(entry.type == FtpEntryType_t::file);
  CHECK(entry.size == 5368709120ULL);
}

TEST_CASE("FTPParser: Malformed and Truncated Inputs") {
  FtpFileEntry_t entry{};

  CHECK_FALSE(ftp_parse_line(nullptr, 0, entry));

  const char* empty_line = "";
  CHECK_FALSE(ftp_parse_line(empty_line, 0, entry));

  const char* garbage_line = "This is total garbage and not an FTP listing!";
  CHECK_FALSE(ftp_parse_line(garbage_line, std::strlen(garbage_line), entry));

  const char* truncated_unix = "-rw-r--r-- 1";
  CHECK_FALSE(
      ftp_parse_line(truncated_unix, std::strlen(truncated_unix), entry));
}
