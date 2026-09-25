// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <memory>
#include <string>

#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/clock/ClockCardCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/SaveStateManager.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

constexpr size_t latch_count = CLOCKCARD_LATCH_COUNT;
using Latches_t = std::array<uint8_t, latch_count>;

// date -u -d @1773325800 -> Thu Mar 12 14:30:00 UTC 2026, handed over already
// local: month 03, weekday 04 (Sunday = 0), day 12, hour 14, minute 30, one
// BCD digit per latch.
constexpr HostLocalTime_t frozen_thursday = {1773325800, 0,  2026, 3, 12,
                                             4,          14, 30,   0};
constexpr Latches_t thursday_latches = {0, 3, 0, 4, 1, 2, 1, 4, 3, 0};

// date -u -d @1795910340 -> Sat Nov 28 23:59:00 UTC 2026
constexpr HostLocalTime_t frozen_saturday = {1795910340, 0,  2026, 11, 28,
                                             6,          23, 59,   0};
constexpr Latches_t saturday_latches = {1, 1, 0, 6, 2, 8, 2, 3, 5, 9};

// Slot n's sixteen I/O registers start at $C080 + n * $10; the ten latches
// come first and the strobe is the last register of the sixteen.
constexpr uint16_t io_base_address = 0xC080;
constexpr int io_slot_shift = 4;
constexpr uint16_t strobe_offset = 0x0F;

constexpr size_t frame_size = sizeof(ClockCardSaveState_t);
constexpr size_t frame_latches_offset = offsetof(ClockCardSaveState_t, latches);

auto first_latch_address(int slot) -> uint16_t {
  return static_cast<uint16_t>(io_base_address + (slot << io_slot_shift));
}

auto strobe(int slot) -> void {
  io_map_dispatch(
      0, static_cast<uint16_t>(first_latch_address(slot) + strobe_offset), 0, 0,
      0);
}

auto read_latches(int slot) -> Latches_t {
  Latches_t latches{};
  for (size_t i = 0; i < latch_count; ++i) {
    latches.at(i) = io_map_dispatch(
        0, static_cast<uint16_t>(first_latch_address(slot) + i), 0, 0, 0);
  }
  return latches;
}

// Verify raw file contents on disk independently of in-memory state.
auto read_file(const std::string& path) -> std::unique_ptr<Snapshot_t> {
  struct stat on_disk{};
  REQUIRE(stat(path.c_str(), &on_disk) == 0);
  REQUIRE(static_cast<size_t>(on_disk.st_size) == sizeof(Snapshot_t));

  auto snapshot = std::unique_ptr<Snapshot_t>(new Snapshot_t());
  std::ifstream in(path, std::ios::binary);
  in.read(reinterpret_cast<char*>(snapshot.get()), sizeof(Snapshot_t));
  REQUIRE(in.good());
  return snapshot;
}

// End-to-end test: verify clock card save and restore via snapshot API.
auto latches_survive_the_file(int slot) -> void {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots.at(static_cast<size_t>(slot - 1)) = "Clock Card";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  peripheral_manager_init();
  linapple_register_peripherals();
  TestFixtures::ScopedLocalTimeProvider_t clock(frozen_thursday);

  strobe(slot);
  REQUIRE(read_latches(slot) == thursday_latches);

  TestFixtures::ScopedTempFile_t file(".aws");
  save_state_set_filename(file.c_str());
  save_state_save();

  {
    const std::unique_ptr<Snapshot_t> written = read_file(file.path());
    CHECK(written->hdr.tag == aw_ss_tag);
    CHECK(written->hdr.version == snapshot_version);
    CHECK(std::string(written->manifest.peripherals[slot].name) ==
          "Clock Card");

    const SsSlotState_t& entry =
        written->slot_trailer.slots[static_cast<size_t>(slot - 1)];
    CHECK(entry.length == frame_size);
    CHECK(memcmp(entry.data + frame_latches_offset, thursday_latches.data(),
                 latch_count) == 0);
  }

  clock.set(frozen_saturday);
  strobe(slot);
  REQUIRE(read_latches(slot) == saturday_latches);
  const unsigned strobes_before_load = clock.calls();

  REQUIRE(save_state_load());

  CHECK(read_latches(slot) == thursday_latches);
  CHECK(clock.calls() == strobes_before_load);
}

}  // namespace

TEST_CASE("Clock Snapshot: An .aws gives back the latched time in any slot") {
  SUBCASE("slot 1") { latches_survive_the_file(1); }
  SUBCASE("slot 4") { latches_survive_the_file(4); }
}
