#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>
#include <string>

#include "apple2/peripherals/disk/ftpparse.h"
#include "core/Util_Path.h"
#include "doctest.h"

TEST_CASE("FTPParse: UNIX Directory") {
  struct ftpparse fp;
  char buf[] = "drwxr-xr-x 2 user group 4096 Jan 1 12:00 mydir";
  int res = ftpparse(&fp, buf, strlen(buf));

  REQUIRE(res == 1);
  CHECK(fp.flagtrycwd == 1);
  CHECK(fp.flagtryretr == 0);
  CHECK(std::string(fp.name, fp.namelen) == "mydir");
}

TEST_CASE("FTPParse: UNIX File") {
  struct ftpparse fp;
  char buf[] = "-rw-r--r-- 1 user group 143360 Jan 1 12:00 disk.dsk";
  int res = ftpparse(&fp, buf, strlen(buf));

  REQUIRE(res == 1);
  CHECK(fp.flagtrycwd == 0);
  CHECK(fp.flagtryretr == 1);
  CHECK(std::string(fp.name, fp.namelen) == "disk.dsk");
  CHECK(fp.size == 143360);
}

TEST_CASE("FTPParse: UNIX Symbolic Link") {
  struct ftpparse fp;
  char buf[] = "lrwxrwxrwx 1 user group 7 Jan 1 12:00 mylink -> target_dir";
  int res = ftpparse(&fp, buf, strlen(buf));

  REQUIRE(res == 1);
  // Links should have both flags set as they could point to either
  CHECK(fp.flagtrycwd == 1);
  CHECK(fp.flagtryretr == 1);
  // Name should be trimmed
  CHECK(std::string(fp.name, fp.namelen) == "mylink");
}

TEST_CASE("FTPParse: UNIX Symbolic Link to File") {
  struct ftpparse fp;
  char buf[] =
      "lrwxrwxrwx 1 user group 10 Jan 1 12:00 linked_disk.dsk -> real_disk.dsk";
  int res = ftpparse(&fp, buf, strlen(buf));

  REQUIRE(res == 1);
  CHECK(fp.flagtrycwd == 1);
  CHECK(fp.flagtryretr == 1);
  CHECK(std::string(fp.name, fp.namelen) == "linked_disk.dsk");
}

TEST_CASE("FTP: Path traversal sanitization (FTP-1)") {
  CHECK(Path::sanitize_filename("valid_disk.dsk") == "valid_disk.dsk");
  CHECK(Path::sanitize_filename("../../../etc/passwd") == "passwd");
  CHECK(Path::sanitize_filename("..\\..\\windows\\system32\\calc.exe") == "calc.exe");
  CHECK(Path::sanitize_filename("..") == "");
  CHECK(Path::sanitize_filename(".") == "");
  CHECK(Path::sanitize_filename("") == "");
  CHECK(Path::sanitize_filename("dir/subdir/disk.po") == "disk.po");
  CHECK(Path::sanitize_filename("in\x01valid.dsk") == "");
}
