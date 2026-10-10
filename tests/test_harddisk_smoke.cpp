// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "HeadlessHarness.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

using TestConfig = TestFixtures::ScopedTestConfig;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;

auto harddisk_in_slot_7() -> TestConfig::Description {
  TestConfig::Description description;
  description.slots[card_slot - 1] = "Harddisk";
  return description;
}

auto status() -> HarddiskStatus {
  HarddiskStatus out{};
  size_t size = sizeof(out);
  REQUIRE(peripheral_query(card_slot, harddisk_query_status, &out, &size) ==
          peripheral_ok);
  return out;
}

// The machine a user gets from `linapple --hd1 a [--hd2 b]`.
struct Arguments {
  std::vector<std::string> words;
  std::vector<char*> pointers;

  explicit Arguments(const std::vector<std::string>& given) : words(given) {
    for (std::string& word : words) {
      pointers.push_back(&word.front());
    }
  }
  auto argc() const -> int { return static_cast<int>(pointers.size()); }
  auto argv() -> char** { return pointers.data(); }
};

}  // namespace

TEST_CASE("Harddisk smoke: the card is still in its slot after a boot") {
  TestConfig config(harddisk_in_slot_7());
  HeadlessHarness harness(config);
  harness.boot();
  harness.run_frames(2);

  CHECK(peripheral_present(card_slot, harddisk_id));
}

TEST_CASE(
    "Harddisk smoke: an image named on the command line is in drive 1 when "
    "the machine starts") {
  TestConfig config(harddisk_in_slot_7());
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  Arguments arguments({"linapple", "--hd1", image.path()});
  HeadlessHarness harness(config, arguments.argc(), arguments.argv());
  peripheral_manager_think(0);

  REQUIRE(peripheral_present(card_slot, harddisk_id));
  const HarddiskStatus loaded = status();
  CHECK(loaded.drive0_loaded == 1);
  CHECK(std::string(loaded.drive0_full_path) == image.path());
}

#ifdef LINAPPLE_PRODOS_IMAGE

#include <algorithm>
#include <cstdint>

#include "apple2/Memory.h"
#include "core/Util_Path.h"
#include "frontends/common/SaveStateManager.h"
#include "test_fixtures_core.h"

// ProDOS 2.4.2 is the user's own image, named at configure time and checked
// there against the digest these cases were written against; it is a 140 K
// disk serialized in DOS order.

namespace {

constexpr size_t block_size = 512;
constexpr size_t sector_size = 256;
constexpr size_t track_size = 16 * sector_size;
constexpr size_t dos_image_size = 143360;
constexpr uint32_t dos_image_blocks = 280;
constexpr uint32_t frames_per_stroke = 4;
constexpr uint32_t frame_cap = 900;
constexpr uint32_t boot_cap = 300;

// Bitsy Bye draws its frame first, the volume name and its catalog after;
// both bounds are observations of a boot under Disk Turbo, which only
// shortens them.
constexpr uint32_t frame_by_frame = 60;
constexpr uint32_t catalog_by_frame = 60;
constexpr const char* last_catalog_entry = "- PRODOS            !";

// The ProDOS 8 global page (ProDOS 8 Technical Reference Manual, 5.2.3):
// DEVADR holds one driver address per slot and drive, drive 2 entries
// after the drive 1 ones; DEVCNT is the number of devices minus one; DEVLST
// holds the unit numbers, drive in bit 7 and slot in bits 6-4, with the low
// nibble a device identification.
constexpr uint16_t devadr = 0xBF10;
constexpr uint16_t devcnt = 0xBF31;
constexpr uint16_t devlst = 0xBF32;
constexpr uint16_t slot_7_driver_entry = 0xC746;

constexpr const char* bitsy_bye_title = "S7,D1:/PRODOS.2.4.2";
constexpr const char* bitsy_bye_keys =
    "RETURN:SELECT   ESC:BACK   TAB,#:NEW VOL";
constexpr const char* rule = "----------------------------------------";
constexpr const char* catalog_footer = "BLOCKS FREE:";

// Block k of a track is the pair of DOS 3.3 sectors Fig. 3.14 of Beneath
// Apple ProDOS (pp. 3-16 to 3-18) gives, first-named first; typed in here so
// the order the card serves is checked against a second copy of the figure.
constexpr std::array<std::array<uint8_t, 2>, 8> fig_3_14 = {{{{0x0, 0xE}},
                                                               {{0xD, 0xC}},
                                                               {{0xB, 0xA}},
                                                               {{0x9, 0x8}},
                                                               {{0x7, 0x6}},
                                                               {{0x5, 0x4}},
                                                               {{0x3, 0x2}},
                                                               {{0x1, 0xF}}}};

auto read_file(const std::string& path) -> std::vector<uint8_t> {
  FilePtr file{fopen(path.c_str(), "rb"), fclose};
  REQUIRE(file != nullptr);
  const int64_t size = Path::file_size(file.get());
  REQUIRE(size >= 0);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    REQUIRE(fread(bytes.data(), 1, bytes.size(), file.get()) == bytes.size());
  }
  return bytes;
}

auto write_file(const std::string& path, const std::vector<uint8_t>& bytes)
    -> void {
  FilePtr file{fopen(path.c_str(), "wb"), fclose};
  REQUIRE(file != nullptr);
  REQUIRE(fwrite(bytes.data(), 1, bytes.size(), file.get()) == bytes.size());
}

auto prodos_order_of(const std::vector<uint8_t>& dos) -> std::vector<uint8_t> {
  REQUIRE(dos.size() == dos_image_size);
  std::vector<uint8_t> prodos(dos_image_size, 0);
  for (uint32_t block = 0; block < dos_image_blocks; ++block) {
    const size_t track = block >> 3;
    const std::array<uint8_t, 2>& sectors = fig_3_14.at(block & 7);
    for (size_t half = 0; half < 2; ++half) {
      const size_t from =
          (track * track_size) + (sectors.at(half) * sector_size);
      const size_t to = (block * block_size) + (half * sector_size);
      std::copy(dos.begin() + static_cast<ptrdiff_t>(from),
                dos.begin() + static_cast<ptrdiff_t>(from + sector_size),
                prodos.begin() + static_cast<ptrdiff_t>(to));
    }
  }
  return prodos;
}

// The user's image, re-ordered into a ProDOS-order copy and copied as it is,
// each under a scratch directory so nothing of the user's is written.
struct ProdosImages {
  TestFixtures::ScopedTempDir dir{"linapple_hdd_prodos_"};
  std::string po_drive_1;
  std::string po_drive_2;
  std::string dsk;

  ProdosImages() {
    const std::vector<uint8_t> dos = read_file(LINAPPLE_PRODOS_IMAGE);
    const std::vector<uint8_t> prodos = prodos_order_of(dos);
    po_drive_1 = dir.path() + "/prodos242-1.po";
    po_drive_2 = dir.path() + "/prodos242-2.po";
    dsk = dir.path() + "/prodos242.dsk";
    write_file(po_drive_1, prodos);
    write_file(po_drive_2, prodos);
    write_file(dsk, dos);
  }
};

auto main_byte(uint16_t addr) -> uint8_t {
  const uint8_t* byte = mem_get_main_ptr(addr);
  REQUIRE(byte != nullptr);
  return *byte;
}

auto main_word(uint16_t addr) -> uint16_t {
  return static_cast<uint16_t>(
      main_byte(addr) | (main_byte(static_cast<uint16_t>(addr + 1)) << 8));
}

auto device_entry(int slot, int drive) -> uint16_t {
  return main_word(static_cast<uint16_t>(devadr + (drive * 16) + (slot * 2)));
}

auto screen(const HeadlessHarness& harness) -> std::array<std::string, 24> {
  std::array<std::string, 24> rows;
  for (int row = 0; row < 24; ++row) {
    rows.at(static_cast<size_t>(row)) = harness.get_text_row(row);
  }
  return rows;
}

auto screen_has(const HeadlessHarness& harness, const std::string& text)
    -> bool {
  for (const std::string& row : screen(harness)) {
    if (row.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Runs a frame at a time until the text shows, and says how many it took; the
// cap is what ends a boot that never gets there.
auto frames_until(HeadlessHarness& harness, const std::string& text,
                  uint32_t cap = frame_cap) -> uint32_t {
  for (uint32_t frame = 0; frame < cap; ++frame) {
    if (screen_has(harness, text)) {
      return frame;
    }
    harness.run_frames(1);
  }
  return cap;
}

// Apple ASCII goes straight to the keyboard card; $0A is the down arrow. The
// drive's turbo is off while a key is held, since a key held a hundred
// machine frames repeats on a //e.
auto type_codes(HeadlessHarness& harness, const std::string& codes) -> void {
  const bool turbo = linapple_set_disk_turbo(false);
  for (const char c : codes) {
    linapple_set_key_state(static_cast<uint8_t>(c), true);
    harness.run_frames(frames_per_stroke);
    linapple_set_key_state(static_cast<uint8_t>(c), false);
    harness.run_frames(frames_per_stroke);
  }
  linapple_set_disk_turbo(turbo);
}

auto command(HeadlessHarness& harness, const std::string& text) -> void {
  type_codes(harness, text + "\r");
}

// Applesoft's HOME clears the screen, so what the next command prints is the
// only thing a wait can find.
auto clear_then(HeadlessHarness& harness, const std::string& text) -> void {
  command(harness, "HOME");
  command(harness, text);
}

// The machine the ProDOS legs run on: the card in slot 7 and Disk Turbo on,
// which the hard disk honours because it reports its activity.
auto prodos_machine() -> TestConfig::Description {
  TestConfig::Description description = harddisk_in_slot_7();
  description.extras.push_back({"Configuration", "Disk Turbo", "1"});
  return description;
}

auto check_bitsy_bye_tables() -> void {
  // Three devices, /RAM among them; the card's two units head DEVLST and both
  // use the entry at $Cn46.
  CHECK(main_byte(devcnt) == 0x02);
  CHECK(main_byte(devlst) == 0xFD);
  CHECK(main_byte(devlst + 1) == 0x7D);
  CHECK(device_entry(card_slot, 0) == slot_7_driver_entry);
  CHECK(device_entry(card_slot, 1) == slot_7_driver_entry);
}

// Bitsy Bye lists the volume with BASIC.SYSTEM fourth; three down arrows and
// RETURN run it, and its prompt is the last thing it prints.
auto enter_basic_system(HeadlessHarness& harness) -> void {
  type_codes(harness, "\x0a\x0a\x0a\r");
  REQUIRE(frames_until(harness, "PRODOS BASIC 1.6") < frame_cap);
  REQUIRE(frames_until(harness, "]") < frame_cap);
}

auto set_protect(int drive, bool on) -> void {
  HarddiskSetProtectCmd cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  cmd.write_protected = on ? 1 : 0;
  REQUIRE(peripheral_command(card_slot, harddisk_cmd_set_protect, &cmd,
                             sizeof(cmd)) == peripheral_ok);
  peripheral_manager_think(0);
}

auto eject(int drive) -> void {
  HarddiskEjectCmd cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  REQUIRE(peripheral_command(card_slot, harddisk_cmd_eject, &cmd,
                             sizeof(cmd)) == peripheral_ok);
  peripheral_manager_think(0);
}

// Boots the image in drive 1 to Bitsy Bye and hands back the screen.
auto boot_to_bitsy_bye(HeadlessHarness& harness)
    -> std::array<std::string, 24> {
  harness.boot();
  const uint32_t frame_at = frames_until(harness, rule, boot_cap);
  CHECK(frame_at <= frame_by_frame);
  const uint32_t catalog_at =
      frame_at + frames_until(harness, last_catalog_entry, boot_cap);
  CHECK(catalog_at <= catalog_by_frame);
  MESSAGE("Bitsy Bye: frame at " << frame_at << ", catalog at " << catalog_at);
  const std::array<std::string, 24> rows = screen(harness);
  CHECK(rows.at(0) == bitsy_bye_title);
  CHECK(rows.at(1) == rule);
  CHECK(rows.at(22) == rule);
  CHECK(rows.at(23) == bitsy_bye_keys);
  return rows;
}

// A session resumed at Bitsy Bye still reaches its volume through the driver
// entry the save holds, so BASIC.SYSTEM loads and catalogs drive 1.
auto resumed_session_catalogs(HeadlessHarness& harness) -> void {
  harness.run_frames(2);
  CHECK(screen_has(harness, bitsy_bye_title));
  CHECK(screen_has(harness, bitsy_bye_keys));
  check_bitsy_bye_tables();
  enter_basic_system(harness);
  command(harness, "CAT,S7,D1");
  REQUIRE(frames_until(harness, catalog_footer) < frame_cap);
  CHECK(screen_has(harness, "PRODOS          SYS"));
  CHECK(screen_has(harness, "BASIC.SYSTEM    SYS"));
}

}  // namespace

TEST_CASE(
    "Harddisk ProDOS: ProDOS 2.4.2 boots to Bitsy Bye through the firmware, "
    "installs two units on the entry at $C746, and BASIC.SYSTEM reports a "
    "protected drive 2 as WRITE PROTECTED and an ejected one as NO DEVICE "
    "CONNECTED") {
  ProdosImages images;
  TestConfig config(prodos_machine());
  Arguments arguments(
      {"linapple", "--hd1", images.po_drive_1, "--hd2", images.po_drive_2});
  HeadlessHarness harness(config, arguments.argc(), arguments.argv());
  peripheral_manager_think(0);
  REQUIRE(status().drive0_loaded == 1);
  REQUIRE(status().drive1_loaded == 1);

  boot_to_bitsy_bye(harness);
  check_bitsy_bye_tables();

  enter_basic_system(harness);
  command(harness, "CAT,S7,D2");
  REQUIRE(frames_until(harness, catalog_footer) < frame_cap);
  CHECK(screen_has(harness, "README          TXT"));

  // The copied volume is full, so a DELETE, a directory write, is the write
  // that reaches the driver; the MLI passes its $2B to BASIC.SYSTEM, whose
  // text for it is WRITE PROTECTED (ProDOS 8 Technical Reference Manual, 4.8).
  set_protect(1, true);
  clear_then(harness, "DELETE README,S7,D2");
  CHECK(frames_until(harness, "WRITE PROTECTED") < frame_cap);
  CHECK_FALSE(screen_has(harness, "I/O ERROR"));

  // An ejected drive answers STATUS with $28, NO DEVICE CONNECTED.
  eject(1);
  clear_then(harness, "CAT,S7,D2");
  CHECK(frames_until(harness, "NO DEVICE CONNECTED") < frame_cap);
  CHECK_FALSE(screen_has(harness, "I/O ERROR"));

  // Drive 1 is untouched by any of it.
  clear_then(harness, "CAT,S7,D1");
  CHECK(frames_until(harness, catalog_footer) < frame_cap);
  CHECK(screen_has(harness, "README          TXT"));
  CHECK(screen_has(harness, "BLOCKS USED:  280"));
  check_bitsy_bye_tables();
}

TEST_CASE(
    "Harddisk ProDOS: the same disk serialized in DOS order boots to the same "
    "Bitsy Bye screen through the card's own decoding") {
  ProdosImages images;
  std::array<std::string, 24> from_prodos_order;
  {
    TestConfig config(prodos_machine());
    Arguments arguments({"linapple", "--hd1", images.po_drive_1});
    HeadlessHarness harness(config, arguments.argc(), arguments.argv());
    peripheral_manager_think(0);
    REQUIRE(status().drive0_loaded == 1);
    from_prodos_order = boot_to_bitsy_bye(harness);
  }
  {
    TestConfig config(prodos_machine());
    Arguments arguments({"linapple", "--hd1", images.dsk});
    HeadlessHarness harness(config, arguments.argc(), arguments.argv());
    peripheral_manager_think(0);
    REQUIRE(status().drive0_loaded == 1);
    const std::array<std::string, 24> from_dos_order =
        boot_to_bitsy_bye(harness);
    for (size_t row = 0; row < from_dos_order.size(); ++row) {
      CAPTURE(row);
      CHECK(from_dos_order.at(row) == from_prodos_order.at(row));
    }
    check_bitsy_bye_tables();
  }
}

TEST_CASE(
    "Harddisk ProDOS: a session saved at Bitsy Bye resumes on another "
    "machine mounting the same image and catalogs through the driver") {
  ProdosImages images;
  TestFixtures::ScopedTempFile saved(".aws");
  {
    TestConfig config(prodos_machine());
    Arguments arguments({"linapple", "--hd1", images.po_drive_1});
    HeadlessHarness harness(config, arguments.argc(), arguments.argv());
    peripheral_manager_think(0);
    REQUIRE(status().drive0_loaded == 1);
    boot_to_bitsy_bye(harness);
    save_state_set_filename(saved.c_str());
    save_state_save();
  }
  {
    TestConfig config(prodos_machine());
    Arguments arguments({"linapple", "--hd1", images.po_drive_1});
    HeadlessHarness harness(config, arguments.argc(), arguments.argv());
    peripheral_manager_think(0);
    REQUIRE(status().drive0_loaded == 1);
    harness.boot();
    save_state_set_filename(saved.c_str());
    TestFixtures::ScopedLogCapture log;
    REQUIRE(save_state_load());
    CHECK(log.count_containing("resuming against drive 1") == 1);
    resumed_session_catalogs(harness);
  }
}

// A save of a running ProDOS session whose slot-7 trailer entry is empty: its
// DEVADR entries hold $C746 and the card, refused the 16-byte region, stays
// at reset while the host mounts the image. LINAPPLE_PRODOS_SESSION_AWS names
// the file; without it the case has nothing to load and says so.
TEST_CASE(
    "Harddisk ProDOS: a session saved with an empty slot-7 trailer entry "
    "resumes at Bitsy Bye, calls the driver where its DEVADR entries point, "
    "and catalogs") {
  const char* session = std::getenv("LINAPPLE_PRODOS_SESSION_AWS");
  if (session == nullptr || session[0] == '\0') {
    MESSAGE("LINAPPLE_PRODOS_SESSION_AWS is not set; no such save to load");
    return;
  }
  ProdosImages images;
  TestConfig config(prodos_machine());
  Arguments arguments({"linapple", "--hd1", images.po_drive_1});
  HeadlessHarness harness(config, arguments.argc(), arguments.argv());
  peripheral_manager_think(0);
  REQUIRE(status().drive0_loaded == 1);
  harness.boot();

  save_state_set_filename(session);
  TestFixtures::ScopedLogCapture log;
  REQUIRE(save_state_load());
  CHECK(log.count_containing("Slot 7: Harddisk refused the 16-byte "
                             "fixed-body region and stays at reset") == 1);
  CHECK(log.count_containing("resuming against") == 0);
  CHECK(status().drive0_loaded == 1);
  resumed_session_catalogs(harness);
}

#endif
