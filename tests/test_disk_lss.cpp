// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/disk/DiskCommands.h"
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
constexpr uint16_t io_q6_set = 0xC0ED;
constexpr uint16_t io_q7_clear = 0xC0EE;
constexpr uint16_t io_q7_set = 0xC0EF;

// The medium the cases read: a ten-cell self-sync run, then three data
// nibbles of eight cells each, then silence for the rest of the track.
constexpr uint8_t sync_cells = 10;
constexpr uint8_t first_nibble = 0xD5;
constexpr uint8_t second_nibble = 0xAA;
constexpr uint8_t third_nibble = 0x96;
constexpr uint32_t synthetic_cell_count = 4096;

// Arrival cycles, derived by hand from the P6's read loop rather than from
// the card. A pulse sends the sequencer to state 14 and out through states
// 0 and 2, where the shift happens; that is the fourth clock of the cell,
// and the loop then free-runs eight clocks to the cell after it. So the
// shift for cell k lands on sequencer step 8k + 3 at the default timing of
// 32 units, and the 6502 can see it on the cycle after that step, which is
// step / 2 + 1. The self-sync byte ends on cell 7, giving step 59 and cycle
// 30; $D5 ends on cell 17, step 139, cycle 70; $AA on cell 25, step 203,
// cycle 102; $96 on cell 33, step 267, cycle 134.
constexpr uint32_t arrival_sync_32 = 30;
constexpr uint32_t arrival_first_32 = 70;
constexpr uint32_t arrival_second_32 = 102;
constexpr uint32_t arrival_third_32 = 134;

// A cell of 31 or 33 units is 8k * timing / 4 steps instead of 8k, so the
// same cells land earlier or later by the amounts the same arithmetic gives.
constexpr uint32_t arrival_first_31 = 68;
constexpr uint32_t arrival_second_31 = 99;
constexpr uint32_t arrival_third_31 = 130;
constexpr uint32_t arrival_first_33 = 72;
constexpr uint32_t arrival_second_33 = 105;
constexpr uint32_t arrival_third_33 = 138;

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

struct SyntheticMedium_t {
  std::vector<uint8_t> cells;
  uint8_t bit_timing = disk_default_bit_timing;
};

SyntheticMedium_t g_medium;

auto set_cell(std::vector<uint8_t>* packed, uint32_t index, bool one) -> void {
  const auto mask = static_cast<uint8_t>(0x80U >> (index & 7U));
  if (one) {
    (*packed)[index >> 3U] |= mask;
  } else {
    (*packed)[index >> 3U] &= static_cast<uint8_t>(~mask);
  }
}

auto build_medium(uint8_t bit_timing) -> void {
  g_medium.bit_timing = bit_timing;
  g_medium.cells.assign(synthetic_cell_count / 8, 0);

  uint32_t index = 0;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    set_cell(&g_medium.cells, index++, true);
  }
  index = sync_cells;
  for (const uint8_t nibble : {first_nibble, second_nibble, third_nibble}) {
    for (int bit = 7; bit >= 0; --bit) {
      set_cell(&g_medium.cells, index++,
               ((nibble >> static_cast<unsigned>(bit)) & 1U) != 0);
    }
  }
}

auto synthetic_probe(const uint8_t*, size_t, uint32_t, const char*)
    -> DiskProbe_e {
  return disk_probe_definite;
}

auto synthetic_open(const char*, uint32_t, bool, void** out_instance)
    -> DiskError_e {
  *out_instance = &g_medium;
  return disk_err_none;
}

auto synthetic_close(void*) -> void {}

auto synthetic_is_write_protected(void*) -> bool { return false; }

auto synthetic_read(void*, uint32_t, uint8_t* bits, uint32_t max_bits,
                    uint32_t* out_bit_count, uint8_t* out_bit_timing)
    -> DiskError_e {
  if (synthetic_cell_count > max_bits) {
    return disk_err_unsupported;
  }
  for (size_t byte = 0; byte < g_medium.cells.size(); ++byte) {
    bits[byte] = g_medium.cells[byte];
  }
  *out_bit_count = synthetic_cell_count;
  *out_bit_timing = g_medium.bit_timing;
  return disk_err_none;
}

auto synthetic_write(void*, uint32_t, const uint8_t* bits, uint32_t bit_count)
    -> DiskError_e {
  if (bit_count != synthetic_cell_count) {
    return disk_err_unsupported;
  }
  for (size_t byte = 0; byte < g_medium.cells.size(); ++byte) {
    g_medium.cells[byte] = bits[byte];
  }
  return disk_err_none;
}

auto synthetic_driver() -> const DiskFormatDriver_t* {
  static const DiskFormatDriver_t driver = {disk_format_abi_version,
                                            disk_driver_cap_write,
                                            "AAA Synthetic Bit Stream",
                                            nullptr,
                                            synthetic_probe,
                                            synthetic_open,
                                            synthetic_close,
                                            synthetic_is_write_protected,
                                            synthetic_read,
                                            synthetic_write,
                                            nullptr};
  return &driver;
}

struct ScopedMediumFile_t {
  char path[64] = "/tmp/linapple_lss_XXXXXX";

  ScopedMediumFile_t() {
    const int fd = mkstemp(path);
    if (fd >= 0) {
      const std::vector<uint8_t> junk(333, 0x17);
      const ssize_t written = write(fd, junk.data(), junk.size());
      static_cast<void>(written);
      close(fd);
    }
  }
  ~ScopedMediumFile_t() { unlink(path); }

  ScopedMediumFile_t(const ScopedMediumFile_t&) = delete;
  auto operator=(const ScopedMediumFile_t&) -> ScopedMediumFile_t& = delete;
};

class LssHarness_t {
 public:
  explicit LssHarness_t(uint8_t bit_timing) {
    build_medium(bit_timing);
    machine_.load();
    linapple_init();
    peripheral_manager_init();
    linapple_register_peripherals();
    disk_loader_register(synthetic_driver());

    DiskInsertCmd_t cmd{};
    cmd.drive = disk_drive_0;
    util_safe_strcpy(cmd.path, file_.path, disk_insert_path_max);
    peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
    peripheral_manager_think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    peripheral_query(slot_6, disk_query_status, &status, &size);
    REQUIRE(status.drive0_loaded == 1);

    // Start the drive on cycle zero of the slice, which is also cell zero of
    // the medium: every arrival below counts from here.
    io_map_dispatch(0, io_motor_on, 0, 0, 0);
    io_map_dispatch(0, io_q7_clear, 0, 0, 0);
    io_map_dispatch(0, io_q6_clear, 0, 0, 0);
  }

  ~LssHarness_t() {
    linapple_shutdown();
    disk_loader_reset();
  }

  LssHarness_t(const LssHarness_t&) = delete;
  auto operator=(const LssHarness_t&) -> LssHarness_t& = delete;

  auto read_at(uint16_t address, uint32_t cycle) -> uint8_t {
    return io_map_dispatch(0, address, 0, 0, cycle);
  }

  auto write_at(uint16_t address, uint32_t cycle, uint8_t value) -> void {
    io_map_dispatch(0, address, 1, value, cycle);
  }

  auto close_slice(uint32_t cycle) -> void { peripheral_manager_think(cycle); }

 private:
  TestConfig_t machine_{TestConfig_t::disk_ii_only()};
  ScopedMediumFile_t file_;
};

// Reads the data register once a cycle and records every cycle on which a
// finished byte first appears.
struct Arrival_t {
  uint32_t cycle;
  uint8_t value;
};

auto trace_register(LssHarness_t* harness, uint32_t last_cycle)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> trace(last_cycle + 1, 0);
  for (uint32_t cycle = 1; cycle <= last_cycle; ++cycle) {
    trace[cycle] = harness->read_at(io_q6_clear, cycle);
  }
  return trace;
}

auto arrivals_in(const std::vector<uint8_t>& trace) -> std::vector<Arrival_t> {
  std::vector<Arrival_t> arrivals;
  bool register_is_clear = true;
  for (uint32_t cycle = 1; cycle < trace.size(); ++cycle) {
    if ((trace[cycle] & 0x80U) == 0) {
      register_is_clear = true;
      continue;
    }
    if (register_is_clear) {
      arrivals.push_back({cycle, trace[cycle]});
      register_is_clear = false;
    }
  }
  return arrivals;
}

}  // namespace

TEST_CASE("DiskLSS: [LSS-01] Bytes arrive on the cycles the P6 loop gives") {
  LssHarness_t harness(disk_default_bit_timing);
  const std::vector<uint8_t> trace = trace_register(&harness, 200);
  const std::vector<Arrival_t> arrivals = arrivals_in(trace);

  REQUIRE(arrivals.size() >= 4);
  CHECK(arrivals[0].cycle == arrival_sync_32);
  CHECK(arrivals[0].value == 0xFF);
  CHECK(arrivals[1].cycle == arrival_first_32);
  CHECK(arrivals[1].value == first_nibble);
  CHECK(arrivals[2].cycle == arrival_second_32);
  CHECK(arrivals[2].value == second_nibble);
  CHECK(arrivals[3].cycle == arrival_third_32);
  CHECK(arrivals[3].value == third_nibble);

  // The sequencer clears the register and starts the next byte before it
  // arrives, so the cycle in front of every arrival reads as unfinished.
  CHECK(trace[arrival_sync_32 - 1] < 0x80);
  CHECK(trace[arrival_first_32 - 1] < 0x80);
  CHECK(trace[arrival_second_32 - 1] < 0x80);
  CHECK(trace[arrival_third_32 - 1] < 0x80);
}

TEST_CASE("DiskLSS: [LSS-02] A shorter or longer cell shifts the arrivals") {
  {
    LssHarness_t harness(31);
    const std::vector<Arrival_t> arrivals =
        arrivals_in(trace_register(&harness, 200));
    REQUIRE(arrivals.size() >= 4);
    CHECK(arrivals[1].cycle == arrival_first_31);
    CHECK(arrivals[1].value == first_nibble);
    CHECK(arrivals[2].cycle == arrival_second_31);
    CHECK(arrivals[3].cycle == arrival_third_31);
  }
  {
    LssHarness_t harness(33);
    const std::vector<Arrival_t> arrivals =
        arrivals_in(trace_register(&harness, 200));
    REQUIRE(arrivals.size() >= 4);
    CHECK(arrivals[1].cycle == arrival_first_33);
    CHECK(arrivals[1].value == first_nibble);
    CHECK(arrivals[2].cycle == arrival_second_33);
    CHECK(arrivals[3].cycle == arrival_third_33);
  }
}

TEST_CASE("DiskLSS: [LSS-03] Shift-write lays the register down cell by cell") {
  LssHarness_t harness(disk_default_bit_timing);

  // Park the head at the index hole so the written bytes start at cell zero,
  // then write them the way RWTS does: load the register, drop back to
  // shift-write and give the medium the eight cells the byte needs.
  DiskSavedState_t state{};
  size_t size = sizeof(state);
  peripheral_save_state(slot_6, &state, &size);
  state.drives[0].current_byte_pos = 0;
  peripheral_load_state(slot_6, &state, sizeof(state));
  io_map_dispatch(0, io_motor_on, 0, 0, 0);

  const uint8_t payload[] = {0xFF,         0xFF,          0xFF,
                             first_nibble, second_nibble, third_nibble};
  uint32_t cycle = 1;
  harness.read_at(io_q7_set, cycle);
  for (const uint8_t byte : payload) {
    harness.write_at(io_q6_set, cycle, byte);
    cycle += 4;
    harness.read_at(io_q6_clear, cycle);
    cycle += 28;
  }
  harness.read_at(io_q7_clear, cycle);
  harness.close_slice(cycle);

  // The medium the driver is handed back carries what was written, so the
  // card saw it as cells rather than as a byte buffer it kept to itself.
  DiskSavedState_t after{};
  size = sizeof(after);
  peripheral_save_state(slot_6, &after, &size);
  CHECK(after.drives[0].is_dirty == 1);
  REQUIRE(after.drives[0].nibble_count >= 6);

  // The leading sync bytes absorb the splice, where the write head has to
  // pick up the polarity it left the last revolution on; the three nibbles
  // behind them come back exactly as they went out.
  bool found = false;
  for (int index = 0; index + 2 < after.drives[0].nibble_count; ++index) {
    if (after.drives[0].track_buffer[index] == first_nibble &&
        after.drives[0].track_buffer[index + 1] == second_nibble &&
        after.drives[0].track_buffer[index + 2] == third_nibble) {
      found = true;
      break;
    }
  }
  CHECK(found);
}

namespace {

// Bit 7 of every byte the sequencer hands over is the leading one that
// finished it, so only the other seven cells are an unbiased sample of what
// the read amplifier produced.
constexpr int sampled_cells_per_byte = 7;
constexpr int weak_sample_target = 1000;
constexpr double weak_density_low = 0.20;
constexpr double weak_density_high = 0.40;

auto read_weak_bytes(LssHarness_t* harness, uint32_t first_cycle,
                     uint32_t last_cycle) -> std::vector<uint8_t> {
  std::vector<uint8_t> bytes;
  bool register_is_clear = true;
  for (uint32_t cycle = first_cycle; cycle <= last_cycle; ++cycle) {
    const uint8_t value = harness->read_at(io_q6_clear, cycle);
    if ((value & 0x80U) == 0) {
      register_is_clear = true;
      continue;
    }
    if (register_is_clear) {
      bytes.push_back(value);
      register_is_clear = false;
    }
  }
  return bytes;
}

auto count_ones(const std::vector<uint8_t>& bytes) -> int {
  int ones = 0;
  for (const uint8_t value : bytes) {
    for (int bit = 0; bit < sampled_cells_per_byte; ++bit) {
      ones += (value >> static_cast<unsigned>(bit)) & 1;
    }
  }
  return ones;
}

}  // namespace

TEST_CASE("DiskLSS: [LSS-04] A four-zero run reads as amplifier noise") {
  // Enough cycles to carry well past a thousand cells of blank medium.
  constexpr uint32_t last_cycle = 9000;

  std::vector<uint8_t> first_pass;
  std::vector<uint8_t> repeat_pass;
  {
    LssHarness_t harness(disk_default_bit_timing);
    first_pass = read_weak_bytes(&harness, 1, last_cycle);
  }
  {
    LssHarness_t harness(disk_default_bit_timing);
    repeat_pass = read_weak_bytes(&harness, 1, last_cycle);
  }

  // The medium after the three nibbles is blank, so everything past them is
  // the amplifier talking.
  REQUIRE(first_pass.size() * sampled_cells_per_byte > weak_sample_target);

  // Same card, same medium, same noise: the sequence is pinned.
  CHECK(first_pass == repeat_pass);

  // Blank medium alone would hand over nothing at all, and a stuck generator
  // would hand over the same byte every time.
  bool saw_two_values = false;
  for (size_t index = 1; index < first_pass.size(); ++index) {
    if (first_pass[index] != first_pass[0]) {
      saw_two_values = true;
      break;
    }
  }
  CHECK(saw_two_values);

  const int ones = count_ones(first_pass);
  const auto sampled =
      static_cast<int>(first_pass.size()) * sampled_cells_per_byte;
  const double density = static_cast<double>(ones) / sampled;
  CHECK(density > weak_density_low);
  CHECK(density < weak_density_high);
}
