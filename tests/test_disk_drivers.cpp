// SPDX-License-Identifier: GPL-2.0-only
#include <stdlib.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "apple2/peripherals/disk/DiskFormatDriver.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/formats/DoDriver.h"
#include "apple2/peripherals/disk/formats/IieDriver.h"
#include "apple2/peripherals/disk/formats/Nb2Driver.h"
#include "apple2/peripherals/disk/formats/NibDriver.h"
#include "apple2/peripherals/disk/formats/PoDriver.h"
#include "apple2/peripherals/disk/formats/Woz2Driver.h"
#include "doctest.h"

namespace {

class ScopedTempFile_t {
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
    }
  }

 private:
  std::string path_;
};

}  // namespace

// Mock for enhancedisk
bool enhancedisk = true;

TEST_CASE("DiskDrivers: [DRV-01] DO Driver Probing") {
  std::vector<uint8_t> buffer(143360, 0);
  // Track 17 (0x11000), VTOC is sector 0? No, probe checks sectors 1-15.
  // Sector 0 is VTOC, sectors 1-15 are catalog.
  // The loop checks: header[VTOC_OFFSET + 2 + (loop * PAGE_SIZE)] == loop - 1
  for (int loop = 1; loop <= 15; ++loop) {
    buffer[0x11000 + 2 + (loop * 0x100)] = static_cast<uint8_t>(loop - 1);
  }

  CHECK(g_do_driver.probe(buffer.data(), buffer.size(), 143360, ".do") ==
        disk_probe_definite);
}

TEST_CASE("DiskDrivers: [DRV-02] PO Driver Probing") {
  std::vector<uint8_t> buffer(143360, 0);
  // ProDOS directory block 2 check (Track 0, Block 2 = Sectors 4,5)
  // block 2 starts at 1024. PAGE_SIZE = 256.
  // header + (2 * 512) + 256 = 1024 + 256 = 1280.
  // prev = 1280, next = 1282.
  buffer[1280] = 0;
  buffer[1281] = 0;  // prev = 0
  buffer[1282] = 3;
  buffer[1283] = 0;  // next = 3

  CHECK(g_po_driver.probe(buffer.data(), buffer.size(), 143360, ".po") ==
        disk_probe_definite);
}

TEST_CASE("DiskDrivers: [DRV-02B] Extension Hint Discrimination") {
  std::vector<uint8_t> blank_buffer(143360, 0);

  // When given .po hint on an unindexed/raw 140k image:
  // PO driver should claim 'possible', but DO driver must NOT claim 'possible'
  CHECK(g_po_driver.probe(blank_buffer.data(), blank_buffer.size(), 143360,
                          ".po") == disk_probe_possible);
  CHECK(g_do_driver.probe(blank_buffer.data(), blank_buffer.size(), 143360,
                          ".po") == disk_probe_no);

  // When given .do / .dsk hint on an unindexed/raw 140k image:
  CHECK(g_do_driver.probe(blank_buffer.data(), blank_buffer.size(), 143360,
                          ".dsk") == disk_probe_possible);
  CHECK(g_po_driver.probe(blank_buffer.data(), blank_buffer.size(), 143360,
                          ".dsk") == disk_probe_no);
}

TEST_CASE("DiskDrivers: [DRV-03] IIE Driver Probing") {
  uint8_t header[88]{};
  memcpy(header, "SIMSYSTEM_IIE", 13);
  header[13] = 2;  // Variant

  CHECK(g_iie_driver.probe(header, 88, 143360, ".iie") == disk_probe_definite);

  header[0] = 'X';
  CHECK(g_iie_driver.probe(header, 88, 143360, ".iie") == disk_probe_no);
}

TEST_CASE("DiskDrivers: [DRV-04] WOZ 2 Driver Probing") {
  uint8_t header[1536]{};
  memcpy(header, "WOZ2\xFF\n\r\n", 8);

  CHECK(g_woz2_driver.probe(header, 1536, 1536, ".woz") == disk_probe_definite);

  header[0] = 'X';
  CHECK(g_woz2_driver.probe(header, 1536, 1536, ".woz") == disk_probe_no);
}

TEST_CASE("DiskDrivers: [DRV-05] NIB Driver Probing") {
  std::vector<uint8_t> buffer(232960, 0);
  CHECK(g_nib_driver.probe(buffer.data(), buffer.size(), 232960, ".nib") ==
        disk_probe_definite);
}

TEST_CASE("DiskDrivers: [DRV-06] NB2 Driver Probing") {
  std::vector<uint8_t> buffer(223440, 0);
  CHECK(g_nb2_driver.probe(buffer.data(), buffer.size(), 223440, ".nb2") ==
        disk_probe_definite);
}

TEST_CASE("DiskDrivers: [DRV-07] NIB Track Round-trip") {
  ScopedTempFile_t tmp_file(".nib");
  REQUIRE(g_nib_driver.create(tmp_file.c_str()) == disk_err_none);

  void* instance = nullptr;
  bool os_ro = false;
  REQUIRE(g_nib_driver.open(tmp_file.c_str(), 0, 1, &os_ro, &instance) ==
          disk_err_none);

  uint8_t original_track[6656];
  for (int i = 0; i < 6656; ++i) {
    original_track[i] = static_cast<uint8_t>((i + 1) & 0xFF);
  }

  g_nib_driver.write_track(instance, 5, 0, original_track, 6656);

  uint8_t read_track[6656];
  int read_count = 0;
  g_nib_driver.read_track(instance, 5, 0, read_track, &read_count);

  CHECK(read_count == 6656);
  CHECK(memcmp(original_track, read_track, 6656) == 0);

  g_nib_driver.close(instance);

  // Verify persistence across re-open through the driver ABI (Seam 4)
  void* reopen_instance = nullptr;
  bool reopen_ro = false;
  REQUIRE(g_nib_driver.open(tmp_file.c_str(), 0, 1, &reopen_ro,
                            &reopen_instance) == disk_err_none);

  uint8_t persisted_track[6656];
  int persisted_count = 0;
  g_nib_driver.read_track(reopen_instance, 5, 0, persisted_track,
                          &persisted_count);

  CHECK(persisted_count == 6656);
  CHECK(memcmp(original_track, persisted_track, 6656) == 0);

  g_nib_driver.close(reopen_instance);
}

TEST_CASE("DiskDrivers: [DRV-08] NB2 Track Round-trip") {
  ScopedTempFile_t tmp_file(".nb2");
  REQUIRE(g_nb2_driver.create(tmp_file.c_str()) == disk_err_none);

  void* instance = nullptr;
  bool os_ro = false;
  REQUIRE(g_nb2_driver.open(tmp_file.c_str(), 0, 1, &os_ro, &instance) ==
          disk_err_none);

  uint8_t original_track[6384];
  for (int i = 0; i < 6384; ++i) {
    original_track[i] = static_cast<uint8_t>((i + 1) & 0xFF);
  }

  g_nb2_driver.write_track(instance, 10, 0, original_track, 6384);

  uint8_t read_track[6656];  // Buffer is always hardware-sized
  int read_count = 0;
  g_nb2_driver.read_track(instance, 10, 0, read_track, &read_count);

  CHECK(read_count == 6384);
  CHECK(memcmp(original_track, read_track, 6384) == 0);

  g_nb2_driver.close(instance);
}

TEST_CASE("DiskDrivers: [DRV-09] WOZ 2 Driver Probing") {
  uint8_t header[1536]{};
  memcpy(header, "WOZ2\xFF\n\r\n", 8);

  CHECK(g_woz2_driver.probe(header, 1536, 1536, ".woz") == disk_probe_definite);

  CHECK(g_woz2_driver.probe(header, 1536, 1535, ".woz") == disk_probe_no);
}

TEST_CASE("DiskDrivers: [DRV-10] WOZ 3.5\" Rejection") {
  ScopedTempFile_t tmp_file(".woz");
  FILE* f = fopen(tmp_file.c_str(), "wb");
  REQUIRE(f != nullptr);
  uint8_t header[1536]{};
  memcpy(header, "WOZ2\xFF\n\r\n", 8);
  memcpy(header + 12, "INFO", 4);
  header[16] = 60;  // INFO chunk size
  memcpy(header + 80, "TMAP", 4);
  header[84] = 160;
  memcpy(header + 248, "TRKS", 4);
  header[252] = 1;
  header[21] = 2;  // 3.5" disk type
  fwrite(header, 1, 1536, f);
  fclose(f);

  void* instance = nullptr;
  bool os_ro = false;
  CHECK(g_woz2_driver.open(tmp_file.c_str(), 0, 1, &os_ro, &instance) ==
        disk_err_unsupported_format);
}

TEST_CASE("DiskDrivers: [DRV-11] WOZ Write Protect") {
  ScopedTempFile_t tmp_file(".woz");
  auto create_woz_wp = [](const char* path, uint8_t wp_byte) {
    FILE* f = fopen(path, "wb");
    REQUIRE(f != nullptr);
    uint8_t h[1536]{};
    memcpy(h, "WOZ2\xFF\n\r\n", 8);
    memcpy(h + 12, "INFO", 4);
    h[16] = 60;
    memcpy(h + 80, "TMAP", 4);
    h[84] = 160;
    memcpy(h + 248, "TRKS", 4);
    h[252] = 1;
    h[21] = 1;        // 5.25"
    h[22] = wp_byte;  // write protect
    fwrite(h, 1, 1536, f);
    fclose(f);
  };

  void* instance = nullptr;
  bool os_ro = false;

  create_woz_wp(tmp_file.c_str(), 1);
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, 1, &os_ro, &instance) ==
          disk_err_none);
  CHECK(g_woz2_driver.is_write_protected(instance) == true);
  g_woz2_driver.close(instance);

  create_woz_wp(tmp_file.c_str(), 0);
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, 1, &os_ro, &instance) ==
          disk_err_none);
  CHECK(g_woz2_driver.is_write_protected(instance) == false);
  g_woz2_driver.close(instance);
}

TEST_CASE("DiskDrivers: [DRV-12] WOZ Unrecorded Track") {
  ScopedTempFile_t tmp_file(".woz");
  FILE* f = fopen(tmp_file.c_str(), "wb");
  REQUIRE(f != nullptr);
  uint8_t h[1536]{};
  memcpy(h, "WOZ2\xFF\n\r\n", 8);
  memcpy(h + 12, "INFO", 4);
  h[16] = 60;
  h[21] = 1;
  memcpy(h + 80, "TMAP", 4);
  h[84] = 160;
  memcpy(h + 248, "TRKS", 4);
  memset(h + 88, 0xFF, 160);  // TMAP: all unrecorded
  fwrite(h, 1, 1536, f);
  fclose(f);

  void* instance = nullptr;
  bool os_ro = false;
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, 1, &os_ro, &instance) ==
          disk_err_none);

  uint8_t buffer[6656];
  int count = 0;
  g_woz2_driver.read_track(instance, 0, 0, buffer, &count);

  CHECK(count == 6656);
  // Should be random/sync data, at least verify it didn't fail

  g_woz2_driver.close(instance);
}

TEST_CASE("DiskDrivers: [DRV-13] DO Track Round-trip (Fast)") {
  ScopedTempFile_t tmp_do(".do");
  REQUIRE(g_do_driver.create(tmp_do.c_str()) == disk_err_none);

  void* inst = nullptr;
  bool ro = false;
  enhancedisk = true;
  REQUIRE(g_do_driver.open(tmp_do.c_str(), 0, 1, &ro, &inst) == disk_err_none);

  uint8_t buf[6656];
  int count = 0;
  g_do_driver.read_track(inst, 0, 0, buf, &count);
  CHECK(count == 6656);

  g_do_driver.close(inst);
}

TEST_CASE("DiskDrivers: [DRV-14] DO Track Round-trip (Skewed)") {
  ScopedTempFile_t tmp_do(".do");
  REQUIRE(g_do_driver.create(tmp_do.c_str()) == disk_err_none);

  void* inst = nullptr;
  bool ro = false;
  enhancedisk = false;
  REQUIRE(g_do_driver.open(tmp_do.c_str(), 0, 0, &ro, &inst) == disk_err_none);

  uint8_t buf[6656];
  int count = 0;
  g_do_driver.read_track(inst, 0, 0, buf, &count);
  CHECK(count == 6656);

  g_do_driver.close(inst);
}

TEST_CASE("DiskDrivers: [SEC-01] WOZ Malicious trks_index") {
  ScopedTempFile_t tmp_file(".woz");
  FILE* f = fopen(tmp_file.c_str(), "wb");
  REQUIRE(f != nullptr);
  uint8_t h[1536]{};
  memcpy(h, "WOZ2\xFF\n\r\n", 8);
  memcpy(h + 12, "INFO", 4);
  h[16] = 60;
  h[21] = 1;
  memcpy(h + 80, "TMAP", 4);
  h[84] = 160;
  memcpy(h + 248, "TRKS", 4);
  // TMAP starts at offset 88. Set track 0 to use trks_index 160 (out of bounds)
  h[88] = 160;
  fwrite(h, 1, 1536, f);
  fclose(f);

  void* instance = nullptr;
  bool os_ro = false;
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, 1, &os_ro, &instance) ==
          disk_err_none);

  uint8_t buffer[6656];
  int count = 123;
  g_woz2_driver.read_track(instance, 0, 0, buffer, &count);

  CHECK(count == 0);  // Rejects out of bounds trks_index

  g_woz2_driver.close(instance);
}

TEST_CASE("DiskDrivers: [SEC-02] WOZ Malicious bit_count") {
  ScopedTempFile_t tmp_file(".woz");
  FILE* f = fopen(tmp_file.c_str(), "wb");
  REQUIRE(f != nullptr);
  uint8_t h[1536]{};
  memcpy(h, "WOZ2\xFF\n\r\n", 8);
  memcpy(h + 12, "INFO", 4);
  h[16] = 60;
  h[21] = 1;
  memcpy(h + 80, "TMAP", 4);
  h[84] = 160;
  memcpy(h + 248, "TRKS", 4);
  h[88] = 0;  // Track 0 uses trks_index 0
  // TRKS entry 0 starts at 256.
  // starting_block = 3 (offset 1536), block_count = 1 (512 bytes)
  h[256] = 3;
  h[257] = 0;
  h[258] = 1;
  h[259] = 0;
  // bit_count = 512*8 + 1 (too many for 1 block)
  uint32_t bad_bits = 512 * 8 + 1;
  memcpy(h + 260, &bad_bits, 4);
  fwrite(h, 1, 1536, f);

  // Also need to provide at least some data in the file
  uint8_t zero[512] = {0};
  fseek(f, 1536 + 512, SEEK_SET);
  fwrite(zero, 1, 512, f);
  fclose(f);

  void* instance = nullptr;
  bool os_ro = false;
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, 1, &os_ro, &instance) ==
          disk_err_none);

  uint8_t buffer[6656];
  int count = 123;
  g_woz2_driver.read_track(instance, 0, 0, buffer, &count);

  CHECK(count == 0);  // Rejects bit_count > block_count capacity

  g_woz2_driver.close(instance);
}

TEST_CASE("DiskDrivers: [SEC-03] DO Out of Bounds track") {
  ScopedTempFile_t tmp_do(".do");
  REQUIRE(g_do_driver.create(tmp_do.c_str()) == disk_err_none);

  void* inst = nullptr;
  bool ro = false;
  REQUIRE(g_do_driver.open(tmp_do.c_str(), 0, 1, &ro, &inst) == disk_err_none);

  uint8_t buf[6656];
  int count = 123;

  g_do_driver.read_track(inst, -1, 0, buf, &count);
  CHECK(count == 0);
  g_do_driver.read_track(inst, 40, 0, buf, &count);
  CHECK(count == 0);

  g_do_driver.close(inst);
}

TEST_CASE("DiskDrivers: [DRV-08] Driver Supported Extensions") {
  REQUIRE(g_do_driver.supported_exts != nullptr);
  CHECK(strcmp(g_do_driver.supported_exts[0], "do") == 0);
  CHECK(strcmp(g_do_driver.supported_exts[1], "dsk") == 0);
  CHECK(g_do_driver.supported_exts[2] == nullptr);

  REQUIRE(g_po_driver.supported_exts != nullptr);
  CHECK(strcmp(g_po_driver.supported_exts[0], "po") == 0);
  CHECK(g_po_driver.supported_exts[1] == nullptr);

  REQUIRE(g_nib_driver.supported_exts != nullptr);
  CHECK(strcmp(g_nib_driver.supported_exts[0], "nib") == 0);
  CHECK(g_nib_driver.supported_exts[1] == nullptr);

  REQUIRE(g_nb2_driver.supported_exts != nullptr);
  CHECK(strcmp(g_nb2_driver.supported_exts[0], "nb2") == 0);
  CHECK(g_nb2_driver.supported_exts[1] == nullptr);

  REQUIRE(g_woz2_driver.supported_exts != nullptr);
  CHECK(strcmp(g_woz2_driver.supported_exts[0], "woz") == 0);
  CHECK(g_woz2_driver.supported_exts[1] == nullptr);

  REQUIRE(g_iie_driver.supported_exts != nullptr);
  CHECK(strcmp(g_iie_driver.supported_exts[0], "iie") == 0);
  CHECK(g_iie_driver.supported_exts[1] == nullptr);
}

TEST_CASE("DiskDrivers: [IIE-1] Reject truncated IIE disk image") {
  ScopedTempFile_t tmp_iie(".iie");
  {
    FILE* f = fopen(tmp_iie.c_str(), "wb");
    REQUIRE(f != nullptr);
    uint8_t short_hdr[10] = {0};
    fwrite(short_hdr, 1, sizeof(short_hdr), f);
    fclose(f);
  }

  void* inst = nullptr;
  bool ro = false;
  CHECK(g_iie_driver.open(tmp_iie.c_str(), 0, 0, &ro, &inst) != disk_err_none);
  CHECK(inst == nullptr);
}

TEST_CASE("DiskDrivers: [DSK-2] Reject unaligned sector disk image") {
  ScopedTempFile_t tmp_unaligned(".dsk");
  {
    FILE* f = fopen(tmp_unaligned.c_str(), "wb");
    REQUIRE(f != nullptr);
    uint8_t unaligned_data[5000] = {0};  // Not a multiple of 256
    fwrite(unaligned_data, 1, sizeof(unaligned_data), f);
    fclose(f);
  }

  void* inst = nullptr;
  bool ro = false;
  CHECK(g_do_driver.open(tmp_unaligned.c_str(), 0, 0, &ro, &inst) !=
        disk_err_none);
  CHECK(inst == nullptr);
}

TEST_CASE(
    "DiskDrivers: [RET-1] Create valid sector disk and propagate creation "
    "failure") {
  ScopedTempFile_t tmp_new(".dsk");
  tmp_new.unlink_file();

  REQUIRE(g_do_driver.create != nullptr);
  CHECK(g_do_driver.create(tmp_new.c_str()) == disk_err_none);

  FILE* f = fopen(tmp_new.c_str(), "rb");
  REQUIRE(f != nullptr);
  fseek(f, 0, SEEK_END);
  CHECK(ftell(f) == 143360);
  fclose(f);

  // Unwritable / invalid path fails cleanly and returns error
  CHECK(g_do_driver.create("/nonexistent_dir_12345/test.dsk") == disk_err_io);
}

TEST_CASE(
    "DiskDrivers: [RET-2] Create valid bitstream disk and propagate creation "
    "failure") {
  ScopedTempFile_t tmp_nib(".nib");
  tmp_nib.unlink_file();

  REQUIRE(g_nib_driver.create != nullptr);
  CHECK(g_nib_driver.create(tmp_nib.c_str()) == disk_err_none);

  FILE* f = fopen(tmp_nib.c_str(), "rb");
  REQUIRE(f != nullptr);
  fseek(f, 0, SEEK_END);
  CHECK(ftell(f) == 232960);
  fclose(f);

  // Unwritable / invalid path fails cleanly and returns error
  CHECK(g_nib_driver.create("/nonexistent_dir_12345/test.nib") == disk_err_io);
}

TEST_CASE(
    "DiskDrivers: [DSK-2] DOS 3.3 VTOC signature probe within 80KB probe "
    "window") {
  std::vector<uint8_t> buffer(80 * 1024, 0);
  // Construct valid DOS 3.3 catalog chain on Track 17 (0x11000)
  for (int loop = 1; loop <= 15; ++loop) {
    buffer[0x11000 + 2 + (loop * 0x100)] = static_cast<uint8_t>(loop - 1);
  }

  // Definitively recognized as DOS order even with ambiguous or missing
  // extension hint
  CHECK(g_do_driver.probe(buffer.data(), buffer.size(), 143360, "") ==
        disk_probe_definite);
  CHECK(g_do_driver.probe(buffer.data(), buffer.size(), 143360, ".dsk") ==
        disk_probe_definite);
  // PO driver only sees possible based on size, but not definite
  CHECK(g_po_driver.probe(buffer.data(), buffer.size(), 143360, "") ==
        disk_probe_possible);
  CHECK(g_po_driver.probe(buffer.data(), buffer.size(), 143360, ".do") ==
        disk_probe_no);
}

TEST_CASE(
    "DiskDrivers: [NIB-3] Truncated bitstream track pre-fills buffer with "
    "0xFF") {
  ScopedTempFile_t tmp_nib(".nib");

  FILE* f = fopen(tmp_nib.c_str(), "wb");
  REQUIRE(f != nullptr);
  std::vector<uint8_t> short_track(100, 0xAA);
  fwrite(short_track.data(), 1, short_track.size(), f);
  fclose(f);

  bool is_readonly = false;
  void* instance = nullptr;
  CHECK(g_nib_driver.open(tmp_nib.c_str(), 0, 0, &is_readonly, &instance) ==
        disk_err_none);
  REQUIRE(instance != nullptr);

  std::vector<uint8_t> track_buf(nibbles_per_track, 0x55);
  int out_nibbles = 0;
  g_nib_driver.read_track(instance, 0, 0, track_buf.data(), &out_nibbles);

  CHECK(out_nibbles == 100);
  for (size_t i = 0; i < 100; ++i) {
    CHECK(track_buf[i] == 0xAA);
  }
  for (size_t i = 100; i < nibbles_per_track; ++i) {
    CHECK(track_buf[i] == 0xFF);
  }

  g_nib_driver.close(instance);
}

TEST_CASE("DiskDrivers: [IIE-14] IIE Driver invalid variant rejection") {
  ScopedTempFile_t tmp_iie(".iie");

  FILE* f = fopen(tmp_iie.c_str(), "wb");
  REQUIRE(f != nullptr);
  std::vector<uint8_t> header(88, 0);
  memcpy(header.data(), "SIMSYSTEM_IIE", 13);
  header[13] = 99;  // Invalid variant > variant_max_total
  fwrite(header.data(), 1, header.size(), f);
  fclose(f);

  bool is_readonly = false;
  void* instance = nullptr;
  DiskError_e err =
      g_iie_driver.open(tmp_iie.c_str(), 0, 0, &is_readonly, &instance);
  CHECK(err == disk_err_unsupported_format);
  CHECK(instance == nullptr);
}
