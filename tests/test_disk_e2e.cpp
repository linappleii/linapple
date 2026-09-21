// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "core/LinAppleCore.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr int slot_6 = 6;
constexpr uint16_t io_motor_on = 0xC0E9;
constexpr uint16_t io_q6_clear = 0xC0EC;
constexpr uint16_t io_q7_clear = 0xC0EE;

constexpr size_t sector_size = 256;
constexpr size_t track_data_size = sectors_per_track * sector_size;
constexpr uint32_t track_nibble_count = 6208;
constexpr uint32_t track_cell_count = 50464;
constexpr uint32_t track_cycle_count = track_cell_count * 4;

// A byte finishes on the fourth sequencer clock of its last cell and the
// 6502 sees it on the cycle after that clock, so a byte whose last cell is
// cell k arrives on cycle 4k + 2 at the default timing of 32 units. Gap 1 is
// 48 self-sync nibbles of ten cells, so its bytes end on cells 10j + 7 and
// arrive on cycles 40j + 30; the address field's fourteen data nibbles then
// run eight cells each from cell 480.
constexpr uint32_t first_gap1_arrival = 30;
constexpr uint32_t last_gap1_arrival = 1910;
constexpr uint32_t addr_prologue_arrival = 1950;
constexpr uint32_t addr_volume_arrival = 2046;
constexpr uint32_t addr_epilogue_arrival = 2302;
constexpr uint32_t addr_field_last_arrival = 2366;

// Gap 2 is six more self-sync nibbles and the data prologue three data ones,
// which puts the first of the 343 data nibbles on cells 676 to 683.
constexpr uint32_t first_data_arrival = 2734;
constexpr uint32_t data_field_nibbles = 343;
constexpr uint32_t data_nibble_cycles = 32;
constexpr uint32_t first_data_nibble_index = 71;

constexpr uint8_t expected_volume = 0xFE;

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

struct Medium_t {
  std::vector<uint8_t> cells;
  uint32_t cell_count = 0;
  uint8_t bit_timing = disk_default_bit_timing;
};

Medium_t g_medium;

auto set_cell(std::vector<uint8_t>* packed, uint32_t index, bool one) -> void {
  const auto mask = static_cast<uint8_t>(0x80U >> (index & 7U));
  if (one) {
    (*packed)[index >> 3U] |= mask;
  } else {
    (*packed)[index >> 3U] &= static_cast<uint8_t>(~mask);
  }
}

auto medium_probe(const uint8_t*, size_t, uint32_t, const char*)
    -> DiskProbe_e {
  return disk_probe_definite;
}

auto medium_open(const char*, uint32_t, bool, void** out_instance)
    -> DiskError_e {
  *out_instance = &g_medium;
  return disk_err_none;
}

auto medium_close(void*) -> void {}

auto medium_is_write_protected(void*) -> bool { return true; }

auto medium_read(void*, uint32_t, uint8_t* bits, uint32_t max_bits,
                 uint32_t* out_bit_count, uint8_t* out_bit_timing)
    -> DiskError_e {
  if (g_medium.cell_count > max_bits) {
    return disk_err_unsupported;
  }
  for (size_t byte = 0; byte < g_medium.cells.size(); ++byte) {
    bits[byte] = g_medium.cells[byte];
  }
  *out_bit_count = g_medium.cell_count;
  *out_bit_timing = g_medium.bit_timing;
  return disk_err_none;
}

auto medium_driver() -> const DiskFormatDriver_t* {
  static const DiskFormatDriver_t driver = {disk_format_abi_version,
                                            0,
                                            "AAA Synthetic Medium",
                                            nullptr,
                                            medium_probe,
                                            medium_open,
                                            medium_close,
                                            medium_is_write_protected,
                                            medium_read,
                                            nullptr,
                                            nullptr};
  return &driver;
}

struct ScopedMediumFile_t {
  char path[64] = "/tmp/linapple_e2e_XXXXXX";

  ScopedMediumFile_t() {
    const int fd = mkstemp(path);
    if (fd >= 0) {
      const std::vector<uint8_t> junk(64, 0x2B);
      const ssize_t written = write(fd, junk.data(), junk.size());
      static_cast<void>(written);
      close(fd);
    }
  }
  ~ScopedMediumFile_t() { unlink(path); }

  ScopedMediumFile_t(const ScopedMediumFile_t&) = delete;
  auto operator=(const ScopedMediumFile_t&) -> ScopedMediumFile_t& = delete;
};

class CardHarness_t {
 public:
  CardHarness_t() {
    machine_.load();
    linapple_init();
    peripheral_manager_init();
    linapple_register_peripherals();
    disk_loader_register(medium_driver());

    DiskInsertCmd_t cmd{};
    cmd.drive = disk_drive_0;
    util_safe_strcpy(cmd.path, file_.path, disk_insert_path_max);
    peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
    peripheral_manager_think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    peripheral_query(slot_6, disk_query_status, &status, &size);
    REQUIRE(status.drive0_loaded == 1);

    // Cycle zero of the slice is cell zero of the medium: every arrival
    // cycle below counts from here.
    io_map_dispatch(0, io_motor_on, 0, 0, 0);
    io_map_dispatch(0, io_q7_clear, 0, 0, 0);
    io_map_dispatch(0, io_q6_clear, 0, 0, 0);
  }

  ~CardHarness_t() {
    linapple_shutdown();
    disk_loader_reset();
  }

  CardHarness_t(const CardHarness_t&) = delete;
  auto operator=(const CardHarness_t&) -> CardHarness_t& = delete;

  auto poll(uint32_t cycle) -> uint8_t {
    return io_map_dispatch(0, io_q6_clear, 0, 0, cycle);
  }

 private:
  TestConfig_t machine_{TestConfig_t::disk_ii_only()};
  ScopedMediumFile_t file_;
};

struct Arrival_t {
  uint32_t cycle;
  uint8_t value;
};

// The RWTS read loop: poll the data register until bit 7 comes back, take
// the byte, and wait for the register to drop before taking the next one.
auto collect_arrivals(CardHarness_t* harness, uint32_t first_cycle,
                      uint32_t last_cycle, uint32_t step)
    -> std::vector<Arrival_t> {
  std::vector<Arrival_t> arrivals;
  bool register_is_clear = true;
  for (uint32_t cycle = first_cycle; cycle <= last_cycle; cycle += step) {
    const uint8_t value = harness->poll(cycle);
    if ((value & 0x80U) == 0) {
      register_is_clear = true;
      continue;
    }
    if (register_is_clear) {
      arrivals.push_back({cycle, value});
      register_is_clear = false;
    }
  }
  return arrivals;
}

auto decode_4and4(uint8_t high, uint8_t low) -> uint8_t {
  return static_cast<uint8_t>(((high << 1U) | 1U) & low);
}

using SectorImage_t = std::array<uint8_t, track_data_size>;
using NibbleTrack_t = std::array<uint8_t, nibbles_per_track>;

// Each sector gets its own ramp, so a sector read out of the wrong slot or a
// field taken one nibble out of step changes the bytes that come back.
auto make_sector_image() -> SectorImage_t {
  SectorImage_t image{};
  for (size_t sector = 0; sector < sectors_per_track; ++sector) {
    for (size_t offset = 0; offset < sector_size; ++offset) {
      image[(sector * sector_size) + offset] =
          static_cast<uint8_t>((offset * (sector + 3)) + (sector * 7));
    }
  }
  return image;
}

struct SynthesisedTrack_t {
  NibbleTrack_t nibbles{};
  NibbleTrack_t sync_mask{};
  uint32_t count = 0;
};

auto synthesise_track_0(const SectorImage_t& sectors) -> SynthesisedTrack_t {
  SynthesisedTrack_t out;
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  REQUIRE(disk_encoding_nibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), 0,
              sectors.data(), out.nibbles.data(), out.sync_mask.data(),
              &out.count, scratch.data()) == disk_err_none);
  REQUIRE(out.count == track_nibble_count);
  return out;
}

auto install_track(const SynthesisedTrack_t& track) -> void {
  g_medium.cells.assign(max_track_bits / 8, 0);
  uint32_t cell_count = 0;
  REQUIRE(disk_encoding_nibbles_to_bits(track.nibbles.data(), track.count,
                                        track.sync_mask.data(),
                                        g_medium.cells.data(), max_track_bits,
                                        &cell_count) == disk_err_none);
  REQUIRE(cell_count == track_cell_count);
  g_medium.cell_count = cell_count;
  g_medium.bit_timing = disk_default_bit_timing;
  g_medium.cells.resize(track_cell_count / 8);
}

}  // namespace

TEST_CASE("DiskE2E: [E2E-01] The first address field arrives behind gap 1") {
  install_track(synthesise_track_0(make_sector_image()));
  CardHarness_t harness;

  const std::vector<Arrival_t> arrivals =
      collect_arrivals(&harness, 1, addr_field_last_arrival + 1, 1);
  REQUIRE(arrivals.size() == 62);

  for (size_t index = 0; index < 48; ++index) {
    CHECK(arrivals[index].value == 0xFF);
  }
  CHECK(arrivals[0].cycle == first_gap1_arrival);
  CHECK(arrivals[47].cycle == last_gap1_arrival);

  CHECK(arrivals[48].cycle == addr_prologue_arrival);
  CHECK(arrivals[48].value == 0xD5);
  CHECK(arrivals[49].value == 0xAA);
  CHECK(arrivals[50].value == 0x96);

  CHECK(arrivals[51].cycle == addr_volume_arrival);
  CHECK(decode_4and4(arrivals[51].value, arrivals[52].value) ==
        expected_volume);
  CHECK(decode_4and4(arrivals[53].value, arrivals[54].value) == 0);
  CHECK(decode_4and4(arrivals[55].value, arrivals[56].value) == 0);
  CHECK(decode_4and4(arrivals[57].value, arrivals[58].value) ==
        expected_volume);

  CHECK(arrivals[59].cycle == addr_epilogue_arrival);
  CHECK(arrivals[59].value == 0xDE);
  CHECK(arrivals[60].value == 0xAA);
  CHECK(arrivals[61].cycle == addr_field_last_arrival);
  CHECK(arrivals[61].value == 0xEB);
}

TEST_CASE("DiskE2E: [E2E-02] The data field answers a 32-cycle poll exactly") {
  const SynthesisedTrack_t track = synthesise_track_0(make_sector_image());
  install_track(track);
  CardHarness_t harness;

  // Inside a data field every nibble is eight cells, so the poll that took
  // the first one lands on each of the others thirty-two cycles apart.
  std::vector<uint8_t> field;
  field.reserve(data_field_nibbles);
  for (uint32_t index = 0; index < data_field_nibbles; ++index) {
    field.push_back(
        harness.poll(first_data_arrival + (index * data_nibble_cycles)));
  }

  for (uint32_t index = 0; index < data_field_nibbles; ++index) {
    CHECK(field[index] == track.nibbles[first_data_nibble_index + index]);
  }
}

TEST_CASE("DiskE2E: [E2E-03] A revolution decodes to the sixteen sectors") {
  const SectorImage_t original = make_sector_image();
  install_track(synthesise_track_0(original));
  CardHarness_t harness;

  // Four cycles a poll: a byte holds the register for far longer than that,
  // so no arrival is missed and a whole revolution stays inside the budget.
  const std::vector<Arrival_t> arrivals =
      collect_arrivals(&harness, 1, track_cycle_count, 4);
  REQUIRE(arrivals.size() == track_nibble_count);

  NibbleTrack_t read_back{};
  for (size_t index = 0; index < arrivals.size(); ++index) {
    read_back[index] = arrivals[index].value;
  }

  SectorImage_t decoded{};
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  REQUIRE(disk_encoding_denibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), 0,
              read_back.data(), track_nibble_count, decoded.data(),
              scratch.data()) == disk_err_none);
  CHECK(decoded == original);
}

namespace {

// A medium of solid flux with one blank stretch cut into it. Three blank
// cells the read amplifier still tracks; past the fourth it has nothing to
// lock onto and hands the sequencer its own noise, which is the weak bit.
constexpr uint32_t weak_cell_count = 800;
constexpr uint32_t weak_gap_first_cell = 400;
constexpr uint32_t weak_gap_cells = 32;
constexpr uint32_t weak_lap_cycles = weak_cell_count * 4;

// Solid flux hands over a byte every eight cells, so byte j ends on cell
// 8j + 7 and arrives on cycle 32j + 30.
constexpr uint32_t solid_bytes_before_gap = weak_gap_first_cell / 8;
constexpr uint32_t first_solid_arrival = 30;
constexpr uint32_t solid_arrival_step = 32;
constexpr uint32_t last_solid_arrival =
    first_solid_arrival + ((solid_bytes_before_gap - 1) * solid_arrival_step);

// The flattened path skips blank cells rather than filling them, so the
// same medium is 50 recorded bytes before the gap and 46 after it.
constexpr uint32_t flat_nibble_count = 96;

auto install_weak_medium() -> void {
  g_medium.cells.assign(weak_cell_count / 8, 0xFF);
  for (uint32_t cell = weak_gap_first_cell;
       cell < weak_gap_first_cell + weak_gap_cells; ++cell) {
    set_cell(&g_medium.cells, cell, false);
  }
  g_medium.cell_count = weak_cell_count;
  g_medium.bit_timing = disk_default_bit_timing;
}

auto values_of(const std::vector<Arrival_t>& arrivals) -> std::vector<uint8_t> {
  std::vector<uint8_t> values;
  values.reserve(arrivals.size());
  for (const Arrival_t& arrival : arrivals) {
    values.push_back(arrival.value);
  }
  return values;
}

}  // namespace

TEST_CASE("DiskE2E: [E2E-04] A blank stretch reads differently each lap") {
  install_weak_medium();
  CardHarness_t harness;

  const std::vector<Arrival_t> solid =
      collect_arrivals(&harness, 1, last_solid_arrival, 1);
  REQUIRE(solid.size() == solid_bytes_before_gap);
  for (uint32_t index = 0; index < solid_bytes_before_gap; ++index) {
    CHECK(solid[index].value == 0xFF);
    CHECK(solid[index].cycle ==
          first_solid_arrival + (index * solid_arrival_step));
  }

  // The same four hundred cells, once on this lap and once on the next.
  // Twenty-nine unreadable ones is far more entropy than two passes can
  // agree on, so the same surface answers differently the second time past.
  const std::vector<Arrival_t> first_pass =
      collect_arrivals(&harness, last_solid_arrival + 1, weak_lap_cycles, 1);
  const std::vector<Arrival_t> second_pass =
      collect_arrivals(&harness, weak_lap_cycles + last_solid_arrival + 1,
                       2 * weak_lap_cycles, 1);
  CHECK(values_of(first_pass) != values_of(second_pass));
}

TEST_CASE("DiskE2E: [E2E-05] The flattened path reads a blank stretch twice") {
  install_weak_medium();

  NibbleTrack_t first_pass{};
  NibbleTrack_t second_pass{};
  uint32_t first_count = 0;
  uint32_t second_count = 0;
  REQUIRE(disk_encoding_bits_to_nibbles(g_medium.cells.data(), weak_cell_count,
                                        first_pass.data(), nibbles_per_track,
                                        &first_count) == disk_err_none);
  REQUIRE(disk_encoding_bits_to_nibbles(g_medium.cells.data(), weak_cell_count,
                                        second_pass.data(), nibbles_per_track,
                                        &second_count) == disk_err_none);

  CHECK(first_count == flat_nibble_count);
  CHECK(second_count == flat_nibble_count);
  CHECK(first_pass == second_pass);
  for (uint32_t index = 0; index < flat_nibble_count; ++index) {
    CHECK(first_pass[index] == 0xFF);
  }
}
