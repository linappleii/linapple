// SPDX-License-Identifier: GPL-2.0-only
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
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

auto write_nibbles(const DiskFormatDriver_t& driver, void* instance,
                   uint32_t quarter_track, const std::vector<uint8_t>& nibbles)
    -> DiskError_e {
  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 0;
  REQUIRE(disk_encoding_nibbles_to_bits(
              nibbles.data(), static_cast<uint32_t>(nibbles.size()), nullptr,
              bits.data(), max_track_bits, &bit_count) == disk_err_none);
  return driver.write_track_bits(instance, quarter_track, bits.data(),
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
  REQUIRE(write_nibbles(g_nib_driver, instance, 3 * 4, ramp) == disk_err_none);
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

  const std::vector<uint8_t> one_over(6657, 0xAA);
  CHECK(write_nibbles(g_nib_driver, instance, 3 * 4, one_over) ==
        disk_err_unsupported);
  const std::vector<uint8_t> far_over(7000, 0xAA);
  CHECK(write_nibbles(g_nib_driver, instance, 3 * 4, far_over) ==
        disk_err_unsupported);
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

namespace {

constexpr size_t probe_window = 80 * 1024;
constexpr uint32_t nib_bytes = 35 * 6656;
constexpr uint32_t nb2_bytes = 35 * 6384;

auto write_file(const std::string& path, const std::vector<uint8_t>& bytes)
    -> void {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  REQUIRE(out.is_open());
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  REQUIRE(out.good());
}

// minimal-track.woz stretched with zeros to exactly a nibble image's length.
auto woz_padded_to(size_t size) -> std::vector<uint8_t> {
  std::vector<uint8_t> bytes =
      read_file(TestFixtures::get_fixture_path("minimal-track.woz"));
  REQUIRE(bytes.size() == 2048);
  bytes.resize(size, 0);
  return bytes;
}

struct LoaderChoice_t {
  std::string driver_name;
  DiskError_e error = disk_err_none;
};

auto loader_choice(const std::string& path) -> LoaderChoice_t {
  disk_loader_reset();
  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  LoaderChoice_t choice;
  choice.error = disk_loader_open(path.c_str(), &driver, &instance);
  REQUIRE(driver != nullptr);
  choice.driver_name = driver->name;
  if (instance != nullptr) {
    driver->close(instance);
  }
  return choice;
}

}  // namespace

TEST_CASE(
    "DiskNibble: [NIB-P1] a WOZ of a nibble image's length goes to WOZ 2 "
    "whatever it is named") {
  struct Case_t {
    const DiskFormatDriver_t* driver;
    uint32_t bytes;
    const char* ext;
  };
  const Case_t cases[] = {{&g_nib_driver, nib_bytes, ".nib"},
                          {&g_nb2_driver, nb2_bytes, ".nb2"}};
  for (const Case_t& c : cases) {
    CAPTURE(c.ext);
    const std::vector<uint8_t> woz = woz_padded_to(c.bytes);

    CHECK(c.driver->probe(woz.data(), probe_window, c.bytes, ".woz") ==
          disk_probe_no);
    CHECK(c.driver->probe(woz.data(), probe_window, c.bytes, c.ext) ==
          disk_probe_no);
    CHECK(c.driver->probe(woz.data(), 4, c.bytes, c.ext) == disk_probe_no);
    // Three bytes of window cannot show the magic, so the length alone speaks.
    CHECK(c.driver->probe(woz.data(), 3, c.bytes, c.ext) ==
          disk_probe_definite);

    auto as_woz = TestFixtures::create_ephemeral_blank("padded.woz", 0);
    write_file(as_woz.path(), woz);
    const LoaderChoice_t by_woz_name = loader_choice(as_woz.path());
    CHECK(by_woz_name.driver_name == "WOZ 2");
    CHECK(by_woz_name.error == disk_err_none);

    auto as_nibble =
        TestFixtures::create_ephemeral_blank(std::string("padded") + c.ext, 0);
    write_file(as_nibble.path(), woz);
    const LoaderChoice_t by_nibble_name = loader_choice(as_nibble.path());
    CHECK(by_nibble_name.driver_name == "WOZ 2");
    CHECK(by_nibble_name.error == disk_err_none);
  }
}

TEST_CASE(
    "DiskNibble: [NIB-P2] the extension turns a size match from possible "
    "into definite") {
  struct Case_t {
    const DiskFormatDriver_t* driver;
    const char* fixture;
    uint32_t bytes;
    const char* ext;
    const char* name;
  };
  const Case_t cases[] = {
      {&g_nib_driver, "minimal.nib", nib_bytes, ".nib", "NIB (6656-nibble)"},
      {&g_nb2_driver, "minimal.nb2", nb2_bytes, ".nb2", "NB2 (6384-nibble)"}};
  for (const Case_t& c : cases) {
    CAPTURE(c.ext);
    const std::vector<uint8_t> image =
        read_file(TestFixtures::get_fixture_path(c.fixture));
    REQUIRE(image.size() == c.bytes);

    CHECK(c.driver->probe(image.data(), probe_window, c.bytes, c.ext) ==
          disk_probe_definite);
    CHECK(c.driver->probe(image.data(), probe_window, c.bytes, ".dsk") ==
          disk_probe_possible);
    CHECK(c.driver->probe(image.data(), probe_window, c.bytes, "") ==
          disk_probe_possible);
    CHECK(c.driver->probe(image.data(), probe_window, c.bytes, nullptr) ==
          disk_probe_possible);
    CHECK(c.driver->probe(image.data(), probe_window, c.bytes + 1, c.ext) ==
          disk_probe_no);
    CHECK(c.driver->probe(image.data(), probe_window, c.bytes - 1, c.ext) ==
          disk_probe_no);

    auto as_own = TestFixtures::create_ephemeral(c.fixture);
    const LoaderChoice_t by_own_name = loader_choice(as_own.path());
    CHECK(by_own_name.driver_name == c.name);
    CHECK(by_own_name.error == disk_err_none);

    // Nothing else claims a file of this length, so the one "possible" still
    // wins the loader's vote.
    auto as_dsk = TestFixtures::create_ephemeral_blank("renamed.dsk", 0);
    write_file(as_dsk.path(), image);
    const LoaderChoice_t by_dsk_name = loader_choice(as_dsk.path());
    CHECK(by_dsk_name.driver_name == c.name);
    CHECK(by_dsk_name.error == disk_err_none);
  }
}

TEST_CASE("DiskNibble: [NIB-P3] each nibble length is claimed by one driver") {
  const std::vector<uint8_t> nib =
      read_file(TestFixtures::get_fixture_path("minimal.nib"));
  CHECK(g_nb2_driver.probe(nib.data(), probe_window, nib_bytes, ".nb2") ==
        disk_probe_no);
  CHECK(g_nib_driver.probe(nib.data(), probe_window, nb2_bytes, ".nib") ==
        disk_probe_no);
}

namespace {

// Sixteen zero sectors read back from one slot of a created image.
auto slot_denibblizes_to_zero(const std::vector<uint8_t>& image, uint32_t track,
                              uint32_t track_nibbles) -> bool {
  std::vector<uint8_t> sectors(16 * 256, 0xEE);
  std::vector<uint8_t> scratch(disk_encoding_scratch_size, 0);
  const size_t at = static_cast<size_t>(track) * track_nibbles;
  REQUIRE(disk_encoding_denibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), track,
              image.data() + at, track_nibbles, sectors.data(),
              scratch.data()) == disk_err_none);
  return sectors == std::vector<uint8_t>(16 * 256, 0);
}

}  // namespace

TEST_CASE("DiskNibble: [NIB-C1] a created image is a formatted blank") {
  struct Case_t {
    const DiskFormatDriver_t* driver;
    const char* fixture;
    const char* file;
    uint32_t track_nibbles;
  };
  const Case_t cases[] = {{&g_nib_driver, "minimal.nib", "blank.nib", 6656},
                          {&g_nb2_driver, "minimal.nb2", "blank.nb2", 6384}};
  for (const Case_t& c : cases) {
    CAPTURE(c.fixture);
    const std::vector<uint8_t> golden =
        read_file(TestFixtures::get_fixture_path(c.fixture));
    REQUIRE(golden.size() == 35 * static_cast<size_t>(c.track_nibbles));

    TestFixtures::ScopedTempDir_t dir("linapple_nibble_create_");
    const std::string by_loader = dir.path() + "/" + c.file;
    disk_loader_reset();
    REQUIRE(disk_loader_create(by_loader.c_str(), c.driver->name) ==
            disk_err_none);
    CHECK(read_file(by_loader) == golden);

    const std::string direct = dir.path() + "/direct-" + c.file;
    REQUIRE(c.driver->create(direct.c_str()) == disk_err_none);
    const std::vector<uint8_t> created = read_file(direct);
    CHECK(created == golden);

    for (uint32_t track = 0; track < 35; ++track) {
      CHECK(slot_denibblizes_to_zero(created, track, c.track_nibbles));
    }
    CHECK(std::vector<uint8_t>(created.begin() + 6208,
                               created.begin() + c.track_nibbles) ==
          std::vector<uint8_t>(c.track_nibbles - 6208, 0xFF));

    const LoaderChoice_t choice = loader_choice(direct);
    CHECK(choice.driver_name == c.driver->name);
    CHECK(choice.error == disk_err_none);
  }
}

TEST_CASE("DiskNibble: [NIB-C2] create refuses a slot the track will not fit") {
  TestFixtures::ScopedTempDir_t dir("linapple_nibble_create_");
  const std::string path = dir.path() + "/short.nib";
  CHECK(nibble_disk_image_create(path.c_str(), 6207) == disk_err_unsupported);
  CHECK(access(path.c_str(), F_OK) != 0);
  CHECK(nibble_disk_image_create(path.c_str(), 0) == disk_err_invalid_argument);
  CHECK(nibble_disk_image_create(path.c_str(), nibbles_per_track + 1) ==
        disk_err_invalid_argument);
  CHECK(nibble_disk_image_create(nullptr, nibbles_per_track) ==
        disk_err_invalid_argument);
  CHECK(access(path.c_str(), F_OK) != 0);

  REQUIRE(nibble_disk_image_create(path.c_str(), 6208) == disk_err_none);
  CHECK(read_file(path).size() == 35 * 6208);
  CHECK(nibble_disk_image_create(path.c_str(), 6208) == disk_err_io);
}

namespace {

struct NibbleCase_t {
  const DiskFormatDriver_t* driver;
  const char* fixture;
  uint32_t track_nibbles;
};

constexpr NibbleCase_t nibble_cases[] = {{&g_nib_driver, "minimal.nib", 6656},
                                         {&g_nb2_driver, "minimal.nb2", 6384}};

// Every byte carries bit 7 and none is 0xFF, so the stream has no run the
// sync inference could stretch and the slot's bytes are the stream's.
auto ramp_of(size_t count) -> std::vector<uint8_t> {
  std::vector<uint8_t> ramp(count, 0);
  for (size_t i = 0; i < count; ++i) {
    ramp[i] = static_cast<uint8_t>(0x80 | (i % 127));
  }
  return ramp;
}

auto slot_of(const std::vector<uint8_t>& file, uint32_t track,
             uint32_t track_nibbles) -> std::vector<uint8_t> {
  const size_t at = static_cast<size_t>(track) * track_nibbles;
  REQUIRE(file.size() >= at + track_nibbles);
  return std::vector<uint8_t>(
      file.begin() + static_cast<std::ptrdiff_t>(at),
      file.begin() + static_cast<std::ptrdiff_t>(at + track_nibbles));
}

}  // namespace

TEST_CASE(
    "DiskNibble: [NIB-R1] a read-only open is write protected and refuses a "
    "write") {
  for (const NibbleCase_t& c : nibble_cases) {
    CAPTURE(c.fixture);
    auto image = TestFixtures::create_ephemeral(c.fixture);
    const std::vector<uint8_t> before = read_file(image.path());

    void* instance = nullptr;
    REQUIRE(c.driver->open(image.c_str(), 0, true, &instance) == disk_err_none);
    CHECK(c.driver->is_write_protected(instance));

    const TrackBits_t track = read_track(*c.driver, instance, 0);
    REQUIRE(track.bit_count > 0);
    CHECK(c.driver->write_track_bits(instance, 0, track.bits.data(),
                                     track.bit_count) ==
          disk_err_write_protected);
    c.driver->close(instance);
    CHECK(read_file(image.path()) == before);

    // The flag is the caller's, not the file's: the same file opened for
    // writing is not protected.
    REQUIRE(c.driver->open(image.c_str(), 0, false, &instance) ==
            disk_err_none);
    CHECK_FALSE(c.driver->is_write_protected(instance));
    c.driver->close(instance);
  }
}

TEST_CASE(
    "DiskNibble: [NIB-N1] a null argument to a track call is refused through "
    "both drivers") {
  for (const NibbleCase_t& c : nibble_cases) {
    CAPTURE(c.fixture);
    auto image = TestFixtures::create_ephemeral(c.fixture);
    const std::vector<uint8_t> before = read_file(image.path());

    void* instance = nullptr;
    REQUIRE(c.driver->open(image.c_str(), 0, false, &instance) ==
            disk_err_none);

    std::vector<uint8_t> bits(max_track_bits / 8, 0xC3);
    uint32_t bit_count = 123;
    uint8_t bit_timing = 7;
    CHECK(c.driver->read_track_bits(nullptr, 0, bits.data(), max_track_bits,
                                    &bit_count,
                                    &bit_timing) == disk_err_invalid_argument);
    CHECK(c.driver->read_track_bits(instance, 0, nullptr, max_track_bits,
                                    &bit_count,
                                    &bit_timing) == disk_err_invalid_argument);
    CHECK(c.driver->read_track_bits(instance, 0, bits.data(), max_track_bits,
                                    nullptr,
                                    &bit_timing) == disk_err_invalid_argument);
    CHECK(c.driver->read_track_bits(instance, 0, bits.data(), max_track_bits,
                                    &bit_count,
                                    nullptr) == disk_err_invalid_argument);
    CHECK(bit_count == 123);
    CHECK(bit_timing == 7);
    CHECK(bits == std::vector<uint8_t>(max_track_bits / 8, 0xC3));

    CHECK(c.driver->write_track_bits(nullptr, 0, bits.data(), 8) ==
          disk_err_invalid_argument);
    CHECK(c.driver->write_track_bits(instance, 0, nullptr, 8) ==
          disk_err_invalid_argument);
    CHECK(read_file(image.path()) == before);

    CHECK(c.driver->is_write_protected(nullptr));
    c.driver->close(nullptr);
    c.driver->close(instance);
  }
}

TEST_CASE("DiskNibble: [NIB-E1] a stream short of one nibble erases the slot") {
  auto image = TestFixtures::create_ephemeral("minimal.nib");
  void* instance = nullptr;
  REQUIRE(g_nib_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  const std::vector<uint8_t> ramp = ramp_of(6656);
  const std::vector<uint8_t> erased(6656, 0xFF);
  const uint8_t cells[] = {0xFF};
  const uint32_t stream_lengths[] = {0, 7};
  for (const uint32_t bit_count : stream_lengths) {
    CAPTURE(bit_count);
    REQUIRE(write_nibbles(g_nib_driver, instance, 2 * 4, ramp) ==
            disk_err_none);
    const std::vector<uint8_t> before = read_file(image.path());
    REQUIRE(slot_of(before, 2, 6656) == ramp);

    // Nothing decodes, so the whole-slot rewrite is all pad: the surface a
    // bulk eraser leaves, not the dead zeros of an unformatted image.
    CHECK(g_nib_driver.write_track_bits(instance, 2 * 4, cells, bit_count) ==
          disk_err_none);
    const std::vector<uint8_t> after = read_file(image.path());
    REQUIRE(after.size() == 232960);
    CHECK(slot_of(after, 2, 6656) == erased);
    CHECK(std::vector<uint8_t>(after.begin(), after.begin() + (2 * 6656)) ==
          std::vector<uint8_t>(before.begin(), before.begin() + (2 * 6656)));
    CHECK(std::vector<uint8_t>(after.begin() + (3 * 6656), after.end()) ==
          std::vector<uint8_t>(before.begin() + (3 * 6656), before.end()));

    // A run of 0xFF with no prologue behind it is data to the inference, so
    // the erased slot reads as 6,656 eight-cell nibbles.
    const TrackBits_t read_back = read_track(g_nib_driver, instance, 2 * 4);
    CHECK(read_back.bit_count == 53248);
    CHECK(to_nibbles(read_back) == erased);
  }
  g_nib_driver.close(instance);
}

TEST_CASE(
    "DiskNibble: [NIB-X1] the NB2 slot takes exactly 6,384 nibbles and not "
    "one more") {
  auto image = TestFixtures::create_ephemeral("minimal.nb2");
  void* instance = nullptr;
  REQUIRE(g_nb2_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  const std::vector<uint8_t> full = ramp_of(6384);
  REQUIRE(write_nibbles(g_nb2_driver, instance, 5 * 4, full) == disk_err_none);
  const std::vector<uint8_t> file = read_file(image.path());
  REQUIRE(file.size() == 223440);
  CHECK(slot_of(file, 5, 6384) == full);
  CHECK(to_nibbles(read_track(g_nb2_driver, instance, 5 * 4)) == full);

  CHECK(write_nibbles(g_nb2_driver, instance, 5 * 4, ramp_of(6385)) ==
        disk_err_unsupported);
  CHECK(write_nibbles(g_nb2_driver, instance, 5 * 4, ramp_of(6656)) ==
        disk_err_unsupported);
  CHECK(read_file(image.path()) == file);
  g_nb2_driver.close(instance);
}

TEST_CASE(
    "DiskNibble: [NIB-M1] a buffer narrower than the track refuses it whole") {
  auto image = TestFixtures::create_ephemeral("minimal.nib");
  void* instance = nullptr;
  REQUIRE(g_nib_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  const std::vector<uint8_t> untouched(max_track_bits / 8, 0xC3);
  std::vector<uint8_t> bits = untouched;
  uint32_t bit_count = 123;
  uint8_t bit_timing = 0;

  // The formatted blank's track 0: 6,656 nibbles at eight cells, plus two
  // more for each of the 384 gap bytes the inference reads as self-sync.
  constexpr uint32_t track_cells = 54016;
  const uint32_t too_narrow[] = {53247, track_cells - 1};
  for (const uint32_t max_bits : too_narrow) {
    CAPTURE(max_bits);
    bit_count = 123;
    CHECK(g_nib_driver.read_track_bits(instance, 0, bits.data(), max_bits,
                                       &bit_count,
                                       &bit_timing) == disk_err_unsupported);
    CHECK(bit_count == 0);
    CHECK(bits == untouched);
  }

  CHECK(g_nib_driver.read_track_bits(instance, 0, bits.data(), track_cells,
                                     &bit_count, &bit_timing) == disk_err_none);
  CHECK(bit_count == track_cells);
  CHECK(bit_timing == disk_default_bit_timing);
  g_nib_driver.close(instance);
}

TEST_CASE(
    "DiskNibble: [NIB-Q1] every quarter track of a cylinder is its one whole "
    "track") {
  for (const NibbleCase_t& c : nibble_cases) {
    CAPTURE(c.fixture);
    auto image = TestFixtures::create_ephemeral(c.fixture);
    void* instance = nullptr;
    REQUIRE(c.driver->open(image.c_str(), 0, false, &instance) ==
            disk_err_none);

    const TrackBits_t cylinder_0 = read_track(*c.driver, instance, 0);
    REQUIRE(cylinder_0.bit_count > 0);
    for (uint32_t quarter = 1; quarter < quarter_tracks_per_cylinder;
         ++quarter) {
      CAPTURE(quarter);
      const TrackBits_t same = read_track(*c.driver, instance, quarter);
      CHECK(same.bit_count == cylinder_0.bit_count);
      CHECK(same.bits == cylinder_0.bits);
    }
    // The next cylinder's address fields carry its own track number, so the
    // mapping is by cylinder and not one track for the whole disk.
    const TrackBits_t cylinder_1 =
        read_track(*c.driver, instance, quarter_tracks_per_cylinder);
    CHECK(cylinder_1.bits != cylinder_0.bits);

    const std::vector<uint8_t> before = read_file(image.path());
    const std::vector<uint8_t> ramp = ramp_of(c.track_nibbles);
    REQUIRE(write_nibbles(*c.driver, instance,
                          (7 * quarter_tracks_per_cylinder) + 3,
                          ramp) == disk_err_none);
    const std::vector<uint8_t> after = read_file(image.path());
    REQUIRE(after.size() == before.size());
    CHECK(slot_of(after, 7, c.track_nibbles) == ramp);
    CHECK(slot_of(after, 6, c.track_nibbles) ==
          slot_of(before, 6, c.track_nibbles));
    CHECK(slot_of(after, 8, c.track_nibbles) ==
          slot_of(before, 8, c.track_nibbles));
    CHECK(to_nibbles(read_track(*c.driver, instance,
                                7 * quarter_tracks_per_cylinder)) == ramp);
    c.driver->close(instance);
  }
}

TEST_CASE(
    "DiskNibble: [NIB-B2] every track past the last slot is blank surface") {
  for (const NibbleCase_t& c : nibble_cases) {
    CAPTURE(c.fixture);
    auto image = TestFixtures::create_ephemeral(c.fixture);
    const std::vector<uint8_t> before = read_file(image.path());
    void* instance = nullptr;
    REQUIRE(c.driver->open(image.c_str(), 0, false, &instance) ==
            disk_err_none);

    const SynthesisedTrack_t beyond = synthesise_track(36);
    // Tracks 36..39 are within the drive's reach and 40 onward only a
    // caller's; neither may grow the file.
    const uint32_t quarter_tracks[] = {36 * 4, 39 * 4, (39 * 4) + 3, 40 * 4,
                                       UINT32_MAX};
    for (const uint32_t quarter_track : quarter_tracks) {
      CAPTURE(quarter_track);
      std::vector<uint8_t> bits(max_track_bits / 8, 0xEE);
      uint32_t bit_count = 123;
      uint8_t bit_timing = 0;
      CHECK(c.driver->read_track_bits(instance, quarter_track, bits.data(),
                                      max_track_bits, &bit_count,
                                      &bit_timing) == disk_err_none);
      CHECK(bit_count == 0);
      CHECK(bit_timing == disk_default_bit_timing);
      CHECK(bits == std::vector<uint8_t>(max_track_bits / 8, 0xEE));

      CHECK(c.driver->write_track_bits(
                instance, quarter_track, beyond.track.bits.data(),
                beyond.track.bit_count) == disk_err_none);
    }
    c.driver->close(instance);
    CHECK(read_file(image.path()) == before);
  }
}
