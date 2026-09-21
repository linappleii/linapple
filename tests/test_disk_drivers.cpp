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
#include "apple2/peripherals/disk/DiskEncoding.h"
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

// The driver ABI speaks cells, so a case written in nibbles lays them on the
// medium and reads them back off the same way a driver's own callers do.
auto to_bits(const std::vector<uint8_t>& nibbles, std::vector<uint8_t>* bits)
    -> uint32_t {
  bits->assign(max_track_bits / 8, 0);
  uint32_t count = 0;
  REQUIRE(disk_encoding_nibbles_to_bits(
              nibbles.data(), static_cast<uint32_t>(nibbles.size()), nullptr,
              bits->data(), max_track_bits, &count) == disk_err_none);
  return count;
}

auto to_nibbles(const std::vector<uint8_t>& bits, uint32_t bit_count)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> nibbles(nibbles_per_track, 0);
  uint32_t count = 0;
  REQUIRE(disk_encoding_bits_to_nibbles(bits.data(), bit_count, nibbles.data(),
                                        nibbles_per_track,
                                        &count) == disk_err_none);
  nibbles.resize(count);
  return nibbles;
}

auto read_image_track(const std::string& path, size_t track, size_t track_bytes)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> bytes(track_bytes, 0);
  FILE* f = fopen(path.c_str(), "rb");
  REQUIRE(f != nullptr);
  REQUIRE(fseek(f, static_cast<long>(track * track_bytes), SEEK_SET) == 0);
  REQUIRE(fread(bytes.data(), 1, track_bytes, f) == track_bytes);
  fclose(f);
  return bytes;
}

}  // namespace

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
  REQUIRE(g_nib_driver.open(tmp_file.c_str(), 0, false, &instance) ==
          disk_err_none);

  // Every byte a Disk II can find again carries bit 7; a run of cells that
  // opens with a zero is not one the head could resynchronise on.
  std::vector<uint8_t> original(nibbles_per_track, 0);
  for (size_t i = 0; i < original.size(); ++i) {
    original[i] = static_cast<uint8_t>(0x80U | ((i + 1) & 0x7FU));
  }

  constexpr uint32_t quarter_track_5 = 20;
  std::vector<uint8_t> bits;
  const uint32_t written_bits = to_bits(original, &bits);
  CHECK(g_nib_driver.write_track_bits(instance, quarter_track_5, bits.data(),
                                      written_bits) == disk_err_none);

  std::vector<uint8_t> read_bits(max_track_bits / 8, 0);
  uint32_t read_bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(g_nib_driver.read_track_bits(
            instance, quarter_track_5, read_bits.data(), max_track_bits,
            &read_bit_count, &bit_timing) == disk_err_none);

  CHECK(read_bit_count == written_bits);
  CHECK(to_nibbles(read_bits, read_bit_count) == original);

  g_nib_driver.close(instance);

  // Verify persistence across re-open through the driver ABI (Seam 4)
  void* reopen_instance = nullptr;
  REQUIRE(g_nib_driver.open(tmp_file.c_str(), 0, false, &reopen_instance) ==
          disk_err_none);

  std::vector<uint8_t> persisted_bits(max_track_bits / 8, 0);
  uint32_t persisted_bit_count = 0;
  CHECK(g_nib_driver.read_track_bits(reopen_instance, quarter_track_5,
                                     persisted_bits.data(), max_track_bits,
                                     &persisted_bit_count,
                                     &bit_timing) == disk_err_none);

  CHECK(persisted_bit_count == written_bits);
  CHECK(to_nibbles(persisted_bits, persisted_bit_count) == original);

  g_nib_driver.close(reopen_instance);
}

TEST_CASE("DiskDrivers: [DRV-08] NB2 Track Round-trip") {
  ScopedTempFile_t tmp_file(".nb2");
  REQUIRE(g_nb2_driver.create(tmp_file.c_str()) == disk_err_none);

  void* instance = nullptr;
  REQUIRE(g_nb2_driver.open(tmp_file.c_str(), 0, false, &instance) ==
          disk_err_none);

  constexpr size_t nb2_nibbles_per_track = 6384;
  std::vector<uint8_t> original(nb2_nibbles_per_track, 0);
  for (size_t i = 0; i < original.size(); ++i) {
    original[i] = static_cast<uint8_t>(0x80U | ((i + 1) & 0x7FU));
  }

  constexpr uint32_t quarter_track_10 = 40;
  std::vector<uint8_t> bits;
  const uint32_t written_bits = to_bits(original, &bits);
  CHECK(g_nb2_driver.write_track_bits(instance, quarter_track_10, bits.data(),
                                      written_bits) == disk_err_none);

  std::vector<uint8_t> read_bits(max_track_bits / 8, 0);
  uint32_t read_bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(g_nb2_driver.read_track_bits(
            instance, quarter_track_10, read_bits.data(), max_track_bits,
            &read_bit_count, &bit_timing) == disk_err_none);

  CHECK(read_bit_count == written_bits);
  CHECK(to_nibbles(read_bits, read_bit_count) == original);

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
  CHECK(g_woz2_driver.open(tmp_file.c_str(), 0, false, &instance) ==
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

  create_woz_wp(tmp_file.c_str(), 1);
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, false, &instance) ==
          disk_err_none);
  CHECK(g_woz2_driver.is_write_protected(instance) == true);
  g_woz2_driver.close(instance);

  create_woz_wp(tmp_file.c_str(), 0);
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, false, &instance) ==
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
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 123;
  uint8_t bit_timing = 0;

  // TMAP 0xFF is surface the image never recorded. It is not a read failure:
  // the card gets no cells and hears the head amplifier instead.
  CHECK(g_woz2_driver.read_track_bits(instance, 0, bits.data(), max_track_bits,
                                      &bit_count,
                                      &bit_timing) == disk_err_none);
  CHECK(bit_count == 0);

  g_woz2_driver.close(instance);
}

TEST_CASE("DiskDrivers: [DRV-14] WOZ reports the cell time INFO measured") {
  ScopedTempFile_t tmp_file(".woz");
  auto create_woz_timing = [](const char* path, uint8_t timing) {
    FILE* f = fopen(path, "wb");
    REQUIRE(f != nullptr);
    uint8_t h[1536]{};
    memcpy(h, "WOZ2\xFF\n\r\n", 8);
    memcpy(h + 12, "INFO", 4);
    h[16] = 60;
    h[21] = 1;
    memcpy(h + 80, "TMAP", 4);
    h[84] = 160;
    memcpy(h + 248, "TRKS", 4);
    memset(h + 88, 0xFF, 160);
    // INFO chunk data starts at 20; optimal_bit_timing is its fortieth byte.
    h[20 + 39] = timing;
    fwrite(h, 1, 1536, f);
    fclose(f);
  };

  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  void* instance = nullptr;

  constexpr uint8_t fast_cell_time = 31;
  create_woz_timing(tmp_file.c_str(), fast_cell_time);
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, false, &instance) ==
          disk_err_none);
  CHECK(g_woz2_driver.read_track_bits(instance, 0, bits.data(), max_track_bits,
                                      &bit_count,
                                      &bit_timing) == disk_err_none);
  CHECK(bit_timing == fast_cell_time);
  g_woz2_driver.close(instance);

  create_woz_timing(tmp_file.c_str(), 0);
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, false, &instance) ==
          disk_err_none);
  CHECK(g_woz2_driver.read_track_bits(instance, 0, bits.data(), max_track_bits,
                                      &bit_count,
                                      &bit_timing) == disk_err_none);
  CHECK(bit_timing == disk_default_bit_timing);
  g_woz2_driver.close(instance);
}

TEST_CASE("DiskDrivers: [DRV-13] DO Track Round-trip") {
  ScopedTempFile_t tmp_do(".do");
  REQUIRE(g_do_driver.create(tmp_do.c_str()) == disk_err_none);

  void* inst = nullptr;
  REQUIRE(g_do_driver.open(tmp_do.c_str(), 0, false, &inst) == disk_err_none);

  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(g_do_driver.read_track_bits(inst, 0, bits.data(), max_track_bits,
                                    &bit_count, &bit_timing) == disk_err_none);
  // 5808 data nibbles at eight cells and 400 sync nibbles at ten: one
  // revolution in 197.8 ms at four microseconds a cell.
  constexpr uint32_t synthesised_track_bits = 50464;
  CHECK(bit_count == synthesised_track_bits);
  CHECK(bit_timing == disk_default_bit_timing);

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
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 123;
  uint8_t bit_timing = 0;
  CHECK(g_woz2_driver.read_track_bits(instance, 0, bits.data(), max_track_bits,
                                      &bit_count,
                                      &bit_timing) == disk_err_corrupt);

  CHECK(bit_count == 0);  // Rejects out of bounds trks_index

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
  REQUIRE(g_woz2_driver.open(tmp_file.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 123;
  uint8_t bit_timing = 0;
  CHECK(g_woz2_driver.read_track_bits(instance, 0, bits.data(), max_track_bits,
                                      &bit_count,
                                      &bit_timing) == disk_err_corrupt);

  CHECK(bit_count == 0);  // Rejects bit_count > block_count capacity

  g_woz2_driver.close(instance);
}

TEST_CASE("DiskDrivers: [SEC-03] DO Out of Bounds track") {
  ScopedTempFile_t tmp_do(".do");
  REQUIRE(g_do_driver.create(tmp_do.c_str()) == disk_err_none);

  void* inst = nullptr;
  REQUIRE(g_do_driver.open(tmp_do.c_str(), 0, false, &inst) == disk_err_none);

  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 123;
  uint8_t bit_timing = 0;

  constexpr uint32_t quarter_track_40 = 160;
  CHECK(g_do_driver.read_track_bits(inst, quarter_track_40, bits.data(),
                                    max_track_bits, &bit_count,
                                    &bit_timing) == disk_err_invalid_argument);
  CHECK(bit_count == 0);

  bit_count = 123;
  CHECK(g_do_driver.read_track_bits(inst, UINT32_MAX, bits.data(),
                                    max_track_bits, &bit_count,
                                    &bit_timing) == disk_err_invalid_argument);
  CHECK(bit_count == 0);

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
  CHECK(g_iie_driver.open(tmp_iie.c_str(), 0, false, &inst) != disk_err_none);
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
  CHECK(g_do_driver.open(tmp_unaligned.c_str(), 0, false, &inst) !=
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
    "DiskDrivers: [RET-2] Create valid nibble image and propagate creation "
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

TEST_CASE("DiskDrivers: [NIB-3] A truncated nibble track ends where it ends") {
  ScopedTempFile_t tmp_nib(".nib");

  FILE* f = fopen(tmp_nib.c_str(), "wb");
  REQUIRE(f != nullptr);
  std::vector<uint8_t> short_track(100, 0xAA);
  fwrite(short_track.data(), 1, short_track.size(), f);
  fclose(f);

  void* instance = nullptr;
  CHECK(g_nib_driver.open(tmp_nib.c_str(), 0, false, &instance) ==
        disk_err_none);
  REQUIRE(instance != nullptr);

  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(g_nib_driver.read_track_bits(instance, 0, bits.data(), max_track_bits,
                                     &bit_count, &bit_timing) == disk_err_none);

  CHECK(bit_count == 100 * 8);
  CHECK(to_nibbles(bits, bit_count) == short_track);

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

  void* instance = nullptr;
  DiskError_e err = g_iie_driver.open(tmp_iie.c_str(), 0, false, &instance);
  CHECK(err == disk_err_unsupported_format);
  CHECK(instance == nullptr);
}

TEST_CASE("DiskDrivers: [DRV-15] A track written back lands in the image") {
  ScopedTempFile_t tmp_do(".do");
  REQUIRE(g_do_driver.create(tmp_do.c_str()) == disk_err_none);

  constexpr size_t dos_track_bytes = 4096;
  const std::vector<uint8_t> pristine_track(dos_track_bytes, 0);

  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;

  void* inst = nullptr;
  REQUIRE(g_do_driver.open(tmp_do.c_str(), 0, false, &inst) == disk_err_none);
  REQUIRE(g_do_driver.read_track_bits(inst, 0, bits.data(), max_track_bits,
                                      &bit_count,
                                      &bit_timing) == disk_err_none);
  g_do_driver.close(inst);

  // Scribble over the image behind the driver's back, so a write that never
  // reaches the file cannot pass this case.
  {
    FILE* f = fopen(tmp_do.c_str(), "r+b");
    REQUIRE(f != nullptr);
    const std::vector<uint8_t> scribble(dos_track_bytes, 0x5A);
    REQUIRE(fwrite(scribble.data(), 1, scribble.size(), f) == scribble.size());
    fclose(f);
  }

  REQUIRE(g_do_driver.open(tmp_do.c_str(), 0, false, &inst) == disk_err_none);
  CHECK(g_do_driver.write_track_bits(inst, 0, bits.data(), bit_count) ==
        disk_err_none);
  g_do_driver.close(inst);

  CHECK(read_image_track(tmp_do.path(), 0, dos_track_bytes) == pristine_track);
}

TEST_CASE(
    "DiskDrivers: [DRV-16] A track short a sector never reaches the image") {
  ScopedTempFile_t tmp_do(".do");
  REQUIRE(g_do_driver.create(tmp_do.c_str()) == disk_err_none);

  constexpr size_t dos_track_bytes = 4096;

  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;

  void* inst = nullptr;
  REQUIRE(g_do_driver.open(tmp_do.c_str(), 0, false, &inst) == disk_err_none);
  REQUIRE(g_do_driver.read_track_bits(inst, 0, bits.data(), max_track_bits,
                                      &bit_count,
                                      &bit_timing) == disk_err_none);

  // Break the first data prologue: fifteen sectors still read, the sixteenth
  // has no data field the decoder will accept.
  std::vector<uint8_t> nibbles = to_nibbles(bits, bit_count);
  // Gap 1 is 48 nibbles, the address field 14 and its gap 6, so the data
  // prologue D5 AA AD sits at 68.
  constexpr size_t first_data_prologue = 70;
  REQUIRE(nibbles[first_data_prologue] == 0xAD);
  nibbles[first_data_prologue] = 0xAA;

  std::vector<uint8_t> broken_bits;
  const uint32_t broken_count = to_bits(nibbles, &broken_bits);

  const std::vector<uint8_t> before =
      read_image_track(tmp_do.path(), 0, dos_track_bytes);
  CHECK(g_do_driver.write_track_bits(inst, 0, broken_bits.data(),
                                     broken_count) == disk_err_corrupt);
  g_do_driver.close(inst);

  CHECK(read_image_track(tmp_do.path(), 0, dos_track_bytes) == before);
}
