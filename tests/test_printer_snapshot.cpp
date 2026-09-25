// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

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
#include "apple2/peripherals/printer/PrinterCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/SaveStateManager.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

constexpr size_t frame_size = sizeof(PrinterSaveState_t);
constexpr size_t frame_latch_offset = offsetof(PrinterSaveState_t, data_latch);
using Frame_t = std::array<uint8_t, frame_size>;

// Version 1, struct_size 24, total_chars_printed and busy_cycles zero, the
// latch holding the last byte strobed, status_latch, is_online and is_busy
// zero. An older card reads the zero is_online as "offline", which nothing
// running on the machine can observe: the firmware never polls the card.
constexpr uint8_t saved_byte = 0x5A;
constexpr Frame_t frame_with_saved_byte = {
    {0x01, 0x00, 0x00, 0x00, 0x18,       0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00,       0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, saved_byte, 0x00, 0x00, 0x00}};
constexpr uint8_t later_byte = 0xA5;
constexpr uint8_t byte_after_load = 0x42;

// The two file lengths the reader tells apart, and the room each slot has.
constexpr size_t legacy_file_size = 132344;
constexpr size_t trailer_file_size = 134200;
constexpr size_t slot_capacity = 256;
static_assert(snapshot_size_fixed_body == legacy_file_size,
              "a fixed-body .aws is 132,344 bytes");
static_assert(sizeof(ApplewinSnapshot_t) == trailer_file_size,
              "an .aws with the slot trailer is 134,200 bytes");
static_assert(snapshot_slot_state_capacity == slot_capacity,
              "the trailer gives each slot 256 bytes");

constexpr uint16_t io_base_address = 0xC080;
constexpr int io_slot_shift = 4;
constexpr uint8_t rom_wait_plain = 0xC2;

auto card_address(int slot) -> uint16_t {
  return static_cast<uint16_t>(io_base_address + (slot << io_slot_shift));
}

// A write to any of the card's sixteen addresses, as STA $C080,Y makes it.
auto strobe(int slot, uint8_t byte) -> void {
  io_map_dispatch(0, card_address(slot), 1, byte, 0);
}

auto saved_frame(int slot) -> Frame_t {
  Frame_t frame{};
  size_t size = frame.size();
  peripheral_save_state(slot, frame.data(), &size);
  REQUIRE(size == frame_size);
  return frame;
}

auto latch(int slot) -> uint8_t {
  return saved_frame(slot).at(frame_latch_offset);
}

// The two bytes the 6502 fetches at the firmware's wait: B0 00 from the PROM,
// B0 FE while the card holds the machine.
auto wait_bytes(int slot) -> std::array<uint8_t, 2> {
  const uint16_t address =
      static_cast<uint16_t>(0xC000 + (slot << 8) + rom_wait_plain);
  return {{mem[address], mem[static_cast<uint16_t>(address + 1)]}};
}
constexpr std::array<uint8_t, 2> prom_wait_bytes = {{0xB0, 0x00}};

// Only the bytes on disk say what the frontend wrote; the in-memory snapshot
// the writer serialized is gone by the time the file exists.
auto read_file(const std::string& path) -> std::unique_ptr<ApplewinSnapshot_t> {
  struct stat on_disk{};
  REQUIRE(stat(path.c_str(), &on_disk) == 0);
  REQUIRE(static_cast<size_t>(on_disk.st_size) == trailer_file_size);

  auto snapshot = std::unique_ptr<ApplewinSnapshot_t>(new ApplewinSnapshot_t());
  std::ifstream in(path, std::ios::binary);
  in.read(reinterpret_cast<char*>(snapshot.get()), sizeof(ApplewinSnapshot_t));
  REQUIRE(in.good());
  return snapshot;
}

// The card is placed by the configuration and reached on the bus, the way a
// program running on the machine reaches it, and the file goes through the
// frontend's own writer and reader. The sink is installed first so the
// strobes are delivered rather than parking the card.
auto latch_survives_the_file(int slot) -> void {
  TestFixtures::ScopedByteSink_t sink;
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots.at(static_cast<size_t>(slot - 1)) = "Parallel Printer";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  peripheral_manager_init();
  linapple_register_peripherals();
  linapple_reset_hard();

  strobe(slot, saved_byte);
  REQUIRE(latch(slot) == saved_byte);
  REQUIRE(sink.bytes().size() == 1);
  CHECK(sink.bytes().front().slot == slot);
  CHECK(sink.bytes().front().byte == saved_byte);

  TestFixtures::ScopedTempFile_t file(".aws");
  save_state_set_filename(file.c_str());
  save_state_save();

  {
    const std::unique_ptr<ApplewinSnapshot_t> written = read_file(file.path());
    CHECK(written->hdr.tag == aw_ss_tag);
    CHECK(written->hdr.version == snapshot_version);
    CHECK(std::string(written->manifest.peripherals[slot].name) ==
          "Parallel Printer");

    const SsSlotState_t& entry =
        written->slot_trailer.slots[static_cast<size_t>(slot - 1)];
    REQUIRE(entry.length == frame_size);
    Frame_t in_file{};
    memcpy(in_file.data(), entry.data, frame_size);
    CHECK(in_file == frame_with_saved_byte);
  }

  strobe(slot, later_byte);
  REQUIRE(latch(slot) == later_byte);

  REQUIRE(save_state_load());
  CHECK(latch(slot) == saved_byte);
  CHECK(saved_frame(slot) == frame_with_saved_byte);

  // The loaded card is live: it shows the PROM, and its next strobe reaches
  // the sink with the slot it sits in.
  CHECK(wait_bytes(slot) == prom_wait_bytes);
  strobe(slot, byte_after_load);
  CHECK(latch(slot) == byte_after_load);
  REQUIRE(sink.bytes().size() == 3);
  CHECK(sink.bytes().back().slot == slot);
  CHECK(sink.bytes().back().byte == byte_after_load);
  CHECK(sink.dropped() == 0);
}

}  // namespace

TEST_CASE("Printer Snapshot: An .aws gives back the data latch in any slot") {
  SUBCASE("slot 1") { latch_survives_the_file(1); }
  SUBCASE("slot 2") { latch_survives_the_file(2); }
  SUBCASE("slot 7") { latch_survives_the_file(7); }
}

// tests/fixtures/minimal.aws was written before the slot trailer existed, by
// a machine with this card in slot 1; its 16-byte slot 1 region never held a
// printer frame. Its bytes are pinned by test-snapshot-fixture-pinned.
TEST_CASE(
    "Printer Snapshot: A fixed-body .aws from before the trailer carries "
    "nothing for the card and leaves it as it was") {
  TestFixtures::ScopedByteSink_t sink;
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots.at(0) = "Parallel Printer";
  description.slots.at(1) = "Super Serial Card";
  description.slots.at(3) = "Mockingboard";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  peripheral_manager_init();
  linapple_register_peripherals();
  linapple_reset_hard();

  const std::string path = TestFixtures::get_fixture_path("minimal.aws");
  REQUIRE(access(path.c_str(), R_OK) == 0);
  struct stat on_disk{};
  REQUIRE(stat(path.c_str(), &on_disk) == 0);
  REQUIRE(static_cast<size_t>(on_disk.st_size) == legacy_file_size);
  save_state_set_filename(path.c_str());

  SUBCASE("a card that has not printed stays at its reset state") {
    REQUIRE(latch(1) == 0);
    REQUIRE(save_state_load());
    CHECK(latch(1) == 0);
    CHECK(wait_bytes(1) == prom_wait_bytes);

    strobe(1, byte_after_load);
    CHECK(latch(1) == byte_after_load);
    REQUIRE(sink.bytes().size() == 1);
    CHECK(sink.bytes().front().slot == 1);
    CHECK(sink.bytes().front().byte == byte_after_load);
  }

  SUBCASE("a card that has printed keeps its latch, as RESET would leave it") {
    strobe(1, saved_byte);
    REQUIRE(latch(1) == saved_byte);
    REQUIRE(save_state_load());
    CHECK(latch(1) == saved_byte);
    CHECK(wait_bytes(1) == prom_wait_bytes);
  }
}
