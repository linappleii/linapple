// SPDX-License-Identifier: GPL-2.0-only
#include <sys/resource.h>

#include <csignal>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/Nb2Driver.h"
#include "apple2/peripherals/disk/formats/NibDriver.h"
#include "apple2/peripherals/disk/formats/NibbleDiskImage.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

auto read_file(const std::string& path) -> std::vector<uint8_t> {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>());
}

struct TrackBits_t {
  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
};

auto read_track(const DiskFormatDriver_t& driver, void* instance,
                uint32_t quarter_track) -> TrackBits_t {
  TrackBits_t track;
  track.bits.assign(max_track_bits / 8, 0);
  REQUIRE(driver.read_track_bits(instance, quarter_track, track.bits.data(),
                                 max_track_bits, &track.bit_count,
                                 &track.bit_timing) == disk_err_none);
  return track;
}

// Caps the size any file this process writes may reach. Writes past the cap
// fail with EFBIG once SIGXFSZ is ignored, which is how a full device looks
// to stdio: the part that fits goes through and the flush of the rest fails.
class ScopedFileSizeLimit_t {
 public:
  explicit ScopedFileSizeLimit_t(rlim_t limit) {
    REQUIRE(getrlimit(RLIMIT_FSIZE, &previous_) == 0);
    previous_handler_ = signal(SIGXFSZ, SIG_IGN);
    rlimit capped = previous_;
    capped.rlim_cur = limit;
    REQUIRE(setrlimit(RLIMIT_FSIZE, &capped) == 0);
  }
  ~ScopedFileSizeLimit_t() {
    setrlimit(RLIMIT_FSIZE, &previous_);
    signal(SIGXFSZ, previous_handler_);
  }
  ScopedFileSizeLimit_t(const ScopedFileSizeLimit_t&) = delete;
  auto operator=(const ScopedFileSizeLimit_t&)
      -> ScopedFileSizeLimit_t& = delete;

 private:
  rlimit previous_{};
  void (*previous_handler_)(int) = SIG_DFL;
};

}  // namespace

TEST_CASE("DiskNibble: a 6656-nibble image opens through the loader") {
  auto image = TestFixtures::create_ephemeral("minimal.nib");
  disk_loader_reset();

  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(image.c_str(), &driver, &instance) == disk_err_none);
  REQUIRE(driver != nullptr);
  REQUIRE(instance != nullptr);
  CHECK(std::string(driver->name) == "NIB (6656-nibble)");
  driver->close(instance);
}

TEST_CASE("DiskNibble: [NIB-W1] a track write whose flush fails reports io") {
  auto image = TestFixtures::create_ephemeral("minimal.nib");
  void* instance = nullptr;
  REQUIRE(g_nib_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  const TrackBits_t track = read_track(g_nib_driver, instance, 0);
  REQUIRE(track.bit_count > 0);

  // Track 0's slot is bytes 0..6655; a 5,000-byte cap lets stdio's first
  // 4,096 bytes through and fails the flush of the remainder.
  {
    ScopedFileSizeLimit_t cap(5000);
    CHECK(g_nib_driver.write_track_bits(instance, 0, track.bits.data(),
                                        track.bit_count) == disk_err_io);
  }
  g_nib_driver.close(instance);

  REQUIRE(g_nib_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);
  CHECK(g_nib_driver.write_track_bits(instance, 0, track.bits.data(),
                                      track.bit_count) == disk_err_none);
  g_nib_driver.close(instance);
  CHECK(read_file(image.path()).size() == 232960);
}

namespace {

// A formatted track of sixteen patterned sectors as a NIB slot would hold
// it, and the cells the drive would see reading it.
struct SynthesisedTrack_t {
  std::vector<uint8_t> nibbles;
  TrackBits_t track;
};

auto synthesise_track(uint32_t cylinder) -> SynthesisedTrack_t {
  std::vector<uint8_t> sectors(16 * 256, 0);
  for (size_t i = 0; i < sectors.size(); ++i) {
    sectors[i] = static_cast<uint8_t>(0xA0 + (i / 256));
  }
  SynthesisedTrack_t out;
  out.nibbles.assign(nibbles_per_track, 0);
  std::vector<uint8_t> sync_mask(nibbles_per_track, 0);
  std::vector<uint8_t> scratch(disk_encoding_scratch_size, 0);
  uint32_t nibble_count = 0;
  REQUIRE(disk_encoding_nibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), cylinder,
              sectors.data(), out.nibbles.data(), sync_mask.data(),
              &nibble_count, scratch.data()) == disk_err_none);
  out.nibbles.resize(nibble_count);
  out.track.bits.assign(max_track_bits / 8, 0);
  REQUIRE(disk_encoding_nibbles_to_bits(out.nibbles.data(), nibble_count,
                                        sync_mask.data(), out.track.bits.data(),
                                        max_track_bits,
                                        &out.track.bit_count) == disk_err_none);
  return out;
}

auto to_nibbles(const TrackBits_t& track) -> std::vector<uint8_t> {
  std::vector<uint8_t> nibbles(nibbles_per_track, 0);
  uint32_t count = 0;
  REQUIRE(disk_encoding_bits_to_nibbles(track.bits.data(), track.bit_count,
                                        nibbles.data(), nibbles_per_track,
                                        &count) == disk_err_none);
  nibbles.resize(count);
  return nibbles;
}

}  // namespace

TEST_CASE(
    "DiskNibble: [NIB-B1] a write past the last slot is dropped and one "
    "on it lands") {
  auto image = TestFixtures::create_ephemeral("minimal.nib");
  const std::vector<uint8_t> before = read_file(image.path());
  REQUIRE(before.size() == 232960);

  void* instance = nullptr;
  REQUIRE(g_nib_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  const SynthesisedTrack_t beyond = synthesise_track(35);
  CHECK(g_nib_driver.write_track_bits(instance, 35 * 4,
                                      beyond.track.bits.data(),
                                      beyond.track.bit_count) == disk_err_none);
  CHECK(read_file(image.path()) == before);

  const SynthesisedTrack_t last = synthesise_track(34);
  REQUIRE(last.nibbles.size() == 6208);
  CHECK(g_nib_driver.write_track_bits(instance, 34 * 4, last.track.bits.data(),
                                      last.track.bit_count) == disk_err_none);
  g_nib_driver.close(instance);

  const std::vector<uint8_t> after = read_file(image.path());
  REQUIRE(after.size() == 232960);
  CHECK(std::vector<uint8_t>(after.begin() + (34 * 6656),
                             after.begin() + (34 * 6656) + 6208) ==
        last.nibbles);

  REQUIRE(g_nib_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);
  const std::vector<uint8_t> read_back =
      to_nibbles(read_track(g_nib_driver, instance, 34 * 4));
  REQUIRE(read_back.size() >= 6208);
  CHECK(std::vector<uint8_t>(read_back.begin(), read_back.begin() + 6208) ==
        last.nibbles);

  std::vector<uint8_t> bits(max_track_bits / 8, 0xEE);
  uint32_t bit_count = 123;
  uint8_t bit_timing = 0;
  CHECK(g_nib_driver.read_track_bits(instance, 35 * 4, bits.data(),
                                     max_track_bits, &bit_count,
                                     &bit_timing) == disk_err_none);
  CHECK(bit_count == 0);
  CHECK(bit_timing == disk_default_bit_timing);
  g_nib_driver.close(instance);
}

namespace {

auto write_nibbles(void* instance, uint32_t quarter_track,
                   const std::vector<uint8_t>& nibbles) -> DiskError_e {
  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 0;
  REQUIRE(disk_encoding_nibbles_to_bits(
              nibbles.data(), static_cast<uint32_t>(nibbles.size()), nullptr,
              bits.data(), max_track_bits, &bit_count) == disk_err_none);
  return g_nib_driver.write_track_bits(instance, quarter_track, bits.data(),
                                       bit_count);
}

}  // namespace

TEST_CASE("DiskNibble: [NIB-W2] a shorter track rewrites the whole slot") {
  auto image = TestFixtures::create_ephemeral("minimal.nib");
  void* instance = nullptr;
  REQUIRE(g_nib_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  // Every byte of the slot distinct from 0xFF and from what follows, so a
  // stale tail cannot pass for a pad.
  std::vector<uint8_t> ramp(6656, 0);
  for (size_t i = 0; i < ramp.size(); ++i) {
    ramp[i] = static_cast<uint8_t>(0x80 | (i % 127));
  }
  REQUIRE(write_nibbles(instance, 3 * 4, ramp) == disk_err_none);
  {
    const std::vector<uint8_t> file = read_file(image.path());
    CHECK(std::vector<uint8_t>(file.begin() + (3 * 6656),
                               file.begin() + (4 * 6656)) == ramp);
  }

  const SynthesisedTrack_t shorter = synthesise_track(3);
  REQUIRE(shorter.nibbles.size() == 6208);
  REQUIRE(
      g_nib_driver.write_track_bits(instance, 3 * 4, shorter.track.bits.data(),
                                    shorter.track.bit_count) == disk_err_none);

  const std::vector<uint8_t> file = read_file(image.path());
  REQUIRE(file.size() == 232960);
  const std::vector<uint8_t> slot(file.begin() + (3 * 6656),
                                  file.begin() + (4 * 6656));
  CHECK(std::vector<uint8_t>(slot.begin(), slot.begin() + 6208) ==
        shorter.nibbles);
  CHECK(std::vector<uint8_t>(slot.begin() + 6208, slot.end()) ==
        std::vector<uint8_t>(448, 0xFF));

  // Over-long: one nibble past the slot, and well past it.
  const std::vector<uint8_t> one_over(6657, 0xAA);
  CHECK(write_nibbles(instance, 3 * 4, one_over) == disk_err_unsupported);
  const std::vector<uint8_t> far_over(7000, 0xAA);
  CHECK(write_nibbles(instance, 3 * 4, far_over) == disk_err_unsupported);
  CHECK(read_file(image.path()) == file);

  g_nib_driver.close(instance);
}

TEST_CASE("DiskNibble: [NIB-O1] a track size the slot cannot hold is refused") {
  auto image = TestFixtures::create_ephemeral("minimal.nib");

  void* instance = reinterpret_cast<void*>(1);
  CHECK(nibble_disk_image_open(image.c_str(), 0, 0, false, &instance) ==
        disk_err_invalid_argument);
  CHECK(instance == nullptr);
  instance = reinterpret_cast<void*>(1);
  CHECK(nibble_disk_image_open(image.c_str(), 0, nibbles_per_track + 1, false,
                               &instance) == disk_err_invalid_argument);
  CHECK(instance == nullptr);

  REQUIRE(nibble_disk_image_open(image.c_str(), 0, nibbles_per_track, false,
                                 &instance) == disk_err_none);
  REQUIRE(instance != nullptr);
  nibble_disk_image_close(instance);
}

TEST_CASE("DiskNibble: [NIB-O2] a file with nothing recorded is corrupt") {
  auto empty = TestFixtures::create_ephemeral_blank("empty.nib", 0);
  void* instance = reinterpret_cast<void*>(1);
  CHECK(g_nib_driver.open(empty.c_str(), 0, false, &instance) ==
        disk_err_corrupt);
  CHECK(instance == nullptr);
  instance = reinterpret_cast<void*>(1);
  CHECK(g_nb2_driver.open(empty.c_str(), 0, false, &instance) ==
        disk_err_corrupt);
  CHECK(instance == nullptr);

  // A prefix that is the whole file leaves no track behind it either.
  auto prefix_only = TestFixtures::create_ephemeral_blank("prefix.nib", 128);
  CHECK(g_nib_driver.open(prefix_only.c_str(), 128, false, &instance) ==
        disk_err_corrupt);
  CHECK(instance == nullptr);
}

TEST_CASE("DiskNibble: [NIB-O3] a file past the family ceiling is refused") {
  auto oversize = TestFixtures::create_ephemeral_blank(
      "big.nib", static_cast<size_t>(nibble_image_max_bytes) + 1);
  void* instance = reinterpret_cast<void*>(1);
  CHECK(g_nib_driver.open(oversize.c_str(), 0, false, &instance) ==
        disk_err_unsupported);
  CHECK(instance == nullptr);
  instance = reinterpret_cast<void*>(1);
  CHECK(g_nb2_driver.open(oversize.c_str(), 0, false, &instance) ==
        disk_err_unsupported);
  CHECK(instance == nullptr);

  // The ceiling itself is the largest NIB there is, and behind a prefix the
  // prefix is not counted against it.
  auto at_ceiling = TestFixtures::create_ephemeral_blank(
      "full.nib", static_cast<size_t>(nibble_image_max_bytes) + 128);
  REQUIRE(g_nib_driver.open(at_ceiling.c_str(), 128, false, &instance) ==
          disk_err_none);
  g_nib_driver.close(instance);
  CHECK(g_nib_driver.open(at_ceiling.c_str(), 0, false, &instance) ==
        disk_err_unsupported);
}
