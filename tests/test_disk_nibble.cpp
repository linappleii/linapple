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
#include "apple2/peripherals/disk/formats/NibDriver.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr uint32_t quarter_tracks_per_cylinder = 4;

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
