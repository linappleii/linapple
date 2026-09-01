#include <stdio.h>

#include <cstdint>

#include "apple2/peripherals/disk/DiskFormatDriver.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdio>
#include <cstring>
#include <vector>

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/formats/DoDriver.h"
#include "apple2/peripherals/disk/formats/IieDriver.h"
#include "apple2/peripherals/disk/formats/Nb2Driver.h"
#include "apple2/peripherals/disk/formats/NibDriver.h"
#include "apple2/peripherals/disk/formats/PoDriver.h"
#include "apple2/peripherals/disk/formats/Woz2Driver.h"
#include "doctest.h"

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
  const char* tmp_file = "test_roundtrip.nib";
  g_nib_driver.create(tmp_file);

  void* instance = nullptr;
  bool os_ro = false;
  REQUIRE(g_nib_driver.open(tmp_file, 0, 1, &os_ro, &instance) ==
          disk_err_none);

  uint8_t original_track[6656];
  for (int i = 0; i < 6656; ++i) original_track[i] = (i + 1) & 0xFF;

  g_nib_driver.write_track(instance, 5, 0, original_track, 6656);

  uint8_t read_track[6656];
  int read_count = 0;
  g_nib_driver.read_track(instance, 5, 0, read_track, &read_count);

  CHECK(read_count == 6656);
  CHECK(memcmp(original_track, read_track, 6656) == 0);

  g_nib_driver.close(instance);

  // Verbatim check: read directly from file at offset
  FILE* f = fopen(tmp_file, "rb");
  fseek(f, 5 * 6656, SEEK_SET);
  uint8_t file_bytes[6656];
  size_t read_bytes = fread(file_bytes, 1, 6656, f);
  (void)read_bytes;
  fclose(f);
  CHECK(memcmp(original_track, file_bytes, 6656) == 0);

  remove(tmp_file);
}

TEST_CASE("DiskDrivers: [DRV-08] NB2 Track Round-trip") {
  const char* tmp_file = "test_roundtrip.nb2";
  g_nb2_driver.create(tmp_file);

  void* instance = nullptr;
  bool os_ro = false;
  REQUIRE(g_nb2_driver.open(tmp_file, 0, 1, &os_ro, &instance) ==
          disk_err_none);

  uint8_t original_track[6384];
  for (int i = 0; i < 6384; ++i) original_track[i] = (i + 1) & 0xFF;

  g_nb2_driver.write_track(instance, 10, 0, original_track, 6384);

  uint8_t read_track[6656];  // Buffer is always hardware-sized
  int read_count = 0;
  g_nb2_driver.read_track(instance, 10, 0, read_track, &read_count);

  CHECK(read_count == 6384);
  CHECK(memcmp(original_track, read_track, 6384) == 0);

  g_nb2_driver.close(instance);
  remove(tmp_file);
}

TEST_CASE("DiskDrivers: [DRV-09] WOZ 2 Driver Probing") {
  uint8_t header[1536]{};
  memcpy(header, "WOZ2\xFF\n\r\n", 8);

  CHECK(g_woz2_driver.probe(header, 1536, 1536, ".woz") == disk_probe_definite);

  CHECK(g_woz2_driver.probe(header, 1536, 1535, ".woz") == disk_probe_no);
}

TEST_CASE("DiskDrivers: [DRV-10] WOZ 3.5\" Rejection") {
  const char* tmp_file = "test_35.woz";
  FILE* f = fopen(tmp_file, "wb");
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
  CHECK(g_woz2_driver.open(tmp_file, 0, 1, &os_ro, &instance) ==
        disk_err_unsupported_format);

  remove(tmp_file);
}

TEST_CASE("DiskDrivers: [DRV-11] WOZ Write Protect") {
  const char* tmp_file = "test_wp.woz";
  auto create_woz_wp = [](const char* path, uint8_t wp_byte) {
    FILE* f = fopen(path, "wb");
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

  create_woz_wp(tmp_file, 1);
  REQUIRE(g_woz2_driver.open(tmp_file, 0, 1, &os_ro, &instance) ==
          disk_err_none);
  CHECK(g_woz2_driver.is_write_protected(instance) == true);
  g_woz2_driver.close(instance);

  create_woz_wp(tmp_file, 0);
  REQUIRE(g_woz2_driver.open(tmp_file, 0, 1, &os_ro, &instance) ==
          disk_err_none);
  CHECK(g_woz2_driver.is_write_protected(instance) == false);
  g_woz2_driver.close(instance);

  remove(tmp_file);
}

TEST_CASE("DiskDrivers: [DRV-12] WOZ Unrecorded Track") {
  const char* tmp_file = "test_unrec.woz";
  FILE* f = fopen(tmp_file, "wb");
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
  REQUIRE(g_woz2_driver.open(tmp_file, 0, 1, &os_ro, &instance) ==
          disk_err_none);

  uint8_t buffer[6656];
  int count = 0;
  g_woz2_driver.read_track(instance, 0, 0, buffer, &count);

  CHECK(count == 6656);
  // Should be random/sync data, at least verify it didn't fail

  g_woz2_driver.close(instance);
  remove(tmp_file);
}

TEST_CASE("DiskDrivers: [DRV-13] DO Track Round-trip (Fast)") {
  const char* tmp_do = "test_fast.do";
  g_do_driver.create(tmp_do);

  void* inst = nullptr;
  bool ro = false;
  enhancedisk = true;
  REQUIRE(g_do_driver.open(tmp_do, 0, 1, &ro, &inst) == disk_err_none);

  uint8_t buf[6656];
  int count = 0;
  g_do_driver.read_track(inst, 0, 0, buf, &count);
  CHECK(count == 6656);

  g_do_driver.close(inst);
  remove(tmp_do);
}

TEST_CASE("DiskDrivers: [DRV-14] DO Track Round-trip (Skewed)") {
  const char* tmp_do = "test_slow.do";
  g_do_driver.create(tmp_do);

  void* inst = nullptr;
  bool ro = false;
  enhancedisk = false;
  REQUIRE(g_do_driver.open(tmp_do, 0, 0, &ro, &inst) == disk_err_none);

  uint8_t buf[6656];
  int count = 0;
  g_do_driver.read_track(inst, 0, 0, buf, &count);
  CHECK(count == 6656);

  g_do_driver.close(inst);
  remove(tmp_do);
}

TEST_CASE("DiskDrivers: [SEC-01] WOZ Malicious trks_index") {
  const char* tmp_file = "malicious_trks.woz";
  FILE* f = fopen(tmp_file, "wb");
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
  REQUIRE(g_woz2_driver.open(tmp_file, 0, 1, &os_ro, &instance) ==
          disk_err_none);

  uint8_t buffer[6656];
  int count = 123;
  g_woz2_driver.read_track(instance, 0, 0, buffer, &count);

  CHECK(count == 0);  // Rejects out of bounds trks_index

  g_woz2_driver.close(instance);
  remove(tmp_file);
}

TEST_CASE("DiskDrivers: [SEC-02] WOZ Malicious bit_count") {
  const char* tmp_file = "malicious_bits.woz";
  FILE* f = fopen(tmp_file, "wb");
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
  REQUIRE(g_woz2_driver.open(tmp_file, 0, 1, &os_ro, &instance) ==
          disk_err_none);

  uint8_t buffer[6656];
  int count = 123;
  g_woz2_driver.read_track(instance, 0, 0, buffer, &count);

  CHECK(count == 0);  // Rejects bit_count > block_count capacity

  g_woz2_driver.close(instance);
  remove(tmp_file);
}

TEST_CASE("DiskDrivers: [SEC-03] DO Out of Bounds track") {
  const char* tmp_do = "test_oob.do";
  g_do_driver.create(tmp_do);

  void* inst = nullptr;
  bool ro = false;
  REQUIRE(g_do_driver.open(tmp_do, 0, 1, &ro, &inst) == disk_err_none);

  uint8_t buf[6656];
  int count = 123;

  g_do_driver.read_track(inst, -1, 0, buf, &count);
  CHECK(count == 0);
  g_do_driver.read_track(inst, 40, 0, buf, &count);
  CHECK(count == 0);

  g_do_driver.close(inst);
  remove(tmp_do);
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
  const char* tmp_iie = "truncated.iie";
  {
    FILE* f = fopen(tmp_iie, "wb");
    REQUIRE(f != nullptr);
    uint8_t short_hdr[10] = {0};
    fwrite(short_hdr, 1, sizeof(short_hdr), f);
    fclose(f);
  }

  void* inst = nullptr;
  bool ro = false;
  CHECK(g_iie_driver.open(tmp_iie, 0, 0, &ro, &inst) != disk_err_none);
  CHECK(inst == nullptr);

  remove(tmp_iie);
}

TEST_CASE("DiskDrivers: [DSK-2] Reject unaligned sector disk image") {
  const char* tmp_unaligned = "unaligned.dsk";
  {
    FILE* f = fopen(tmp_unaligned, "wb");
    REQUIRE(f != nullptr);
    uint8_t unaligned_data[5000] = {0};  // Not a multiple of 256
    fwrite(unaligned_data, 1, sizeof(unaligned_data), f);
    fclose(f);
  }

  void* inst = nullptr;
  bool ro = false;
  CHECK(g_do_driver.open(tmp_unaligned, 0, 0, &ro, &inst) != disk_err_none);
  CHECK(inst == nullptr);

  remove(tmp_unaligned);
}

TEST_CASE(
    "DiskDrivers: [RET-1] Create valid sector disk and propagate creation "
    "failure") {
  const char* tmp_new = "test_create.dsk";
  remove(tmp_new);

  REQUIRE(g_do_driver.create != nullptr);
  CHECK(g_do_driver.create(tmp_new) == disk_err_none);

  FILE* f = fopen(tmp_new, "rb");
  REQUIRE(f != nullptr);
  fseek(f, 0, SEEK_END);
  CHECK(ftell(f) == 143360);
  fclose(f);
  remove(tmp_new);

  // Unwritable / invalid path fails cleanly and returns error
  CHECK(g_do_driver.create("/nonexistent_dir_12345/test.dsk") == disk_err_io);
}

TEST_CASE(
    "DiskDrivers: [RET-2] Create valid bitstream disk and propagate creation "
    "failure") {
  const char* tmp_nib = "test_create.nib";
  remove(tmp_nib);

  REQUIRE(g_nib_driver.create != nullptr);
  CHECK(g_nib_driver.create(tmp_nib) == disk_err_none);

  FILE* f = fopen(tmp_nib, "rb");
  REQUIRE(f != nullptr);
  fseek(f, 0, SEEK_END);
  CHECK(ftell(f) == 232960);
  fclose(f);
  remove(tmp_nib);

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
