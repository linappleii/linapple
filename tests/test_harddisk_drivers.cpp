// SPDX-License-Identifier: GPL-2.0-only
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
#include "apple2/peripherals/harddisk/formats/BlockDiskImage.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;
constexpr size_t block_size = 512;
constexpr size_t half_block = 256;
constexpr size_t sector_size = 256;
constexpr size_t track_size = 16 * sector_size;
constexpr size_t dos_image_size = 143360;
constexpr uint32_t dos_image_blocks = 280;
constexpr uint32_t hdv_blocks = 16;
constexpr uint32_t volume_directory_key_block = 2;
constexpr uint32_t max_prodos_blocks = 65535;
constexpr const char* missing_path = "/nonexistent/linapple-missing.hdv";

// ProDOS 8 Technical Reference Manual, 6.3.2.
constexpr uint8_t prodos_status = 0x00;
constexpr uint8_t prodos_read = 0x01;
constexpr uint8_t prodos_write = 0x02;
constexpr uint8_t prodos_ok = 0x00;
constexpr uint8_t prodos_io_error = 0x27;
constexpr uint8_t prodos_write_protected = 0x2B;

constexpr uint16_t io_base = 0xC080 + (card_slot << 4);
constexpr uint16_t reg_command = io_base + 0;
constexpr uint16_t reg_unit = io_base + 1;
constexpr uint16_t reg_block_low = io_base + 2;
constexpr uint16_t reg_block_high = io_base + 3;
constexpr uint16_t reg_data = io_base + 4;
constexpr uint16_t reg_count_low = io_base + 5;
constexpr uint16_t reg_count_high = io_base + 6;

// Block k of a track is the pair of DOS 3.3 sectors Fig. 3.14 of Beneath
// Apple ProDOS gives, first-named first.
constexpr std::array<std::array<uint8_t, 2>, 8> k_fig_3_14 = {{{{0x0, 0xE}},
                                                               {{0xD, 0xC}},
                                                               {{0xB, 0xA}},
                                                               {{0x9, 0x8}},
                                                               {{0x7, 0x6}},
                                                               {{0x5, 0x4}},
                                                               {{0x3, 0x2}},
                                                               {{0x1, 0xF}}}};

auto harddisk_in_slot_7() -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Harddisk";
  return description;
}

auto settle() -> void { peripheral_manager_think(0); }

auto insert(int drive, const std::string& path, bool write_protected = false)
    -> void {
  HarddiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  cmd.write_protected = write_protected ? 1 : 0;
  std::strncpy(cmd.path, path.c_str(), sizeof(cmd.path) - 1);
  REQUIRE(peripheral_command(card_slot, harddisk_cmd_insert, &cmd,
                             sizeof(cmd)) == peripheral_ok);
  settle();
}

auto eject(int drive) -> void {
  HarddiskEjectCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  REQUIRE(peripheral_command(card_slot, harddisk_cmd_eject, &cmd,
                             sizeof(cmd)) == peripheral_ok);
  settle();
}

auto status() -> HarddiskStatus_t {
  HarddiskStatus_t out{};
  size_t size = sizeof(out);
  REQUIRE(peripheral_query(card_slot, harddisk_query_status, &out, &size) ==
          peripheral_ok);
  return out;
}

auto peek(uint16_t addr) -> uint8_t {
  return io_map_dispatch(0, addr, 0, 0, 0);
}

auto poke(uint16_t addr, uint8_t value) -> void {
  io_map_dispatch(0, addr, 1, value, 0);
}

auto unit_for(int drive) -> uint8_t {
  return static_cast<uint8_t>((card_slot << 4) | (drive != 0 ? 0x80 : 0));
}

// The controller driven through its registers, as the firmware drives it.
auto card_status(int drive) -> uint8_t {
  poke(reg_unit, unit_for(drive));
  poke(reg_command, prodos_status);
  return peek(reg_command);
}

auto card_block_count() -> uint32_t {
  return peek(reg_count_low) |
         (static_cast<uint32_t>(peek(reg_count_high)) << 8);
}

struct CardRead_t {
  uint8_t result;
  std::array<uint8_t, block_size> bytes;
};

auto card_read(int drive, uint32_t block) -> CardRead_t {
  CardRead_t out{};
  poke(reg_unit, unit_for(drive));
  poke(reg_block_low, static_cast<uint8_t>(block & 0xFF));
  poke(reg_block_high, static_cast<uint8_t>(block >> 8));
  poke(reg_command, prodos_read);
  out.result = peek(reg_command);
  if (out.result == prodos_ok) {
    for (auto& byte : out.bytes) {
      byte = peek(reg_data);
    }
  }
  return out;
}

auto card_write(int drive, uint32_t block,
                const std::array<uint8_t, block_size>& bytes) -> uint8_t {
  poke(reg_unit, unit_for(drive));
  poke(reg_block_low, static_cast<uint8_t>(block & 0xFF));
  poke(reg_block_high, static_cast<uint8_t>(block >> 8));
  for (const uint8_t byte : bytes) {
    poke(reg_data, byte);
  }
  poke(reg_command, prodos_write);
  return peek(reg_command);
}

// A buffer whose last byte sits against pages no access may touch, so a read
// past it faults in every build instead of landing in live memory. The guard
// spans a whole 140 K image, because the catalog track a short header lacks
// lies 73 KB past its start.
class GuardedBuffer_t {
 public:
  explicit GuardedBuffer_t(size_t size) : size_(size) {
    const auto page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const size_t data_bytes = ((size + page - 1) / page) * page;
    const size_t guard_bytes = ((dos_image_size + page - 1) / page) * page;
    mapped_ = data_bytes + guard_bytes;
    void* region = mmap(nullptr, mapped_, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    REQUIRE(region != MAP_FAILED);
    base_ = static_cast<uint8_t*>(region);
    REQUIRE(mprotect(base_ + data_bytes, guard_bytes, PROT_NONE) == 0);
    data_ = base_ + data_bytes - size;
  }
  ~GuardedBuffer_t() { munmap(base_, mapped_); }
  GuardedBuffer_t(const GuardedBuffer_t&) = delete;
  auto operator=(const GuardedBuffer_t&) -> GuardedBuffer_t& = delete;
  GuardedBuffer_t(GuardedBuffer_t&&) = delete;
  auto operator=(GuardedBuffer_t&&) -> GuardedBuffer_t& = delete;

  auto data() -> uint8_t* { return data_; }
  auto size() const -> size_t { return size_; }

 private:
  uint8_t* base_ = nullptr;
  uint8_t* data_ = nullptr;
  size_t mapped_ = 0;
  size_t size_;
};

auto read_file(const std::string& path) -> std::vector<uint8_t> {
  FilePtr_t file{fopen(path.c_str(), "rb"), fclose};
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
  FilePtr_t file{fopen(path.c_str(), "wb"), fclose};
  REQUIRE(file != nullptr);
  if (!bytes.empty()) {
    REQUIRE(fwrite(bytes.data(), 1, bytes.size(), file.get()) == bytes.size());
  }
}

// A fixture under a name of the test's choosing, since the name is part of
// what the probes decide by.
auto copy_as(const TestFixtures::ScopedTempDir_t& dir,
             const std::string& fixture, const std::string& name)
    -> std::string {
  const std::string target = dir.path() + "/" + name;
  write_file(target, read_file(TestFixtures::get_fixture_path(fixture)));
  return target;
}

auto zero_file(const TestFixtures::ScopedTempDir_t& dir,
               const std::string& name, size_t size) -> std::string {
  const std::string target = dir.path() + "/" + name;
  write_file(target, std::vector<uint8_t>(size, 0));
  return target;
}

auto pattern_block(uint8_t seed) -> std::array<uint8_t, block_size> {
  std::array<uint8_t, block_size> bytes{};
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes.at(i) = static_cast<uint8_t>((i + seed) ^ (i >> 8));
  }
  return bytes;
}

auto all_equal(const std::array<uint8_t, block_size>& bytes, uint8_t value)
    -> bool {
  for (const uint8_t byte : bytes) {
    if (byte != value) {
      return false;
    }
  }
  return true;
}

auto slice(const std::vector<uint8_t>& bytes, size_t offset, size_t count)
    -> std::vector<uint8_t> {
  REQUIRE(offset + count <= bytes.size());
  return std::vector<uint8_t>(
      bytes.begin() + static_cast<ptrdiff_t>(offset),
      bytes.begin() + static_cast<ptrdiff_t>(offset + count));
}

auto is_key_block(const std::array<uint8_t, block_size>& bytes) -> bool {
  // The volume directory header: storage type and name length $F7, then the
  // name (ProDOS 8 Technical Reference Manual, B.2.2).
  return bytes.at(4) == 0xF7 && std::memcmp(&bytes.at(5), "MINIMAL", 7) == 0;
}

// Where block 1 lands in the file tells the order the card decoded: sectors
// D and C of track 0 in DOS order, bytes 512-1023 in ProDOS order.
auto write_block_1_lands_in_dos_order(const std::string& path) -> bool {
  const std::array<uint8_t, block_size> pattern = pattern_block(0x40);
  REQUIRE(card_write(0, 1, pattern) == prodos_ok);
  const std::vector<uint8_t> file = read_file(path);
  const std::vector<uint8_t> first(pattern.begin(),
                                   pattern.begin() + half_block);
  const std::vector<uint8_t> second(pattern.begin() + half_block,
                                    pattern.end());
  const bool dos = slice(file, 0xD * sector_size, half_block) == first &&
                   slice(file, 0xC * sector_size, half_block) == second;
  const bool prodos = slice(file, block_size, block_size) ==
                      std::vector<uint8_t>(pattern.begin(), pattern.end());
  REQUIRE(dos != prodos);
  return dos;
}

struct OpenImage_t {
  const HarddiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  HarddiskError_e error = harddisk_err_none;

  OpenImage_t() = default;
  explicit OpenImage_t(const std::string& path) {
    error = harddisk_loader_open(path.c_str(), &driver, &instance);
  }
  ~OpenImage_t() {
    if (driver != nullptr && instance != nullptr) {
      driver->close(instance);
    }
  }
  OpenImage_t(const OpenImage_t&) = delete;
  auto operator=(const OpenImage_t&) -> OpenImage_t& = delete;
  OpenImage_t(OpenImage_t&&) = delete;
  auto operator=(OpenImage_t&&) -> OpenImage_t& = delete;

  auto blocks() const -> uint32_t { return driver->get_total_blocks(instance); }
  auto read(uint32_t block) const -> std::array<uint8_t, block_size> {
    std::array<uint8_t, block_size> bytes{};
    REQUIRE(driver->read_block(instance, block, bytes.data()) ==
            harddisk_err_none);
    return bytes;
  }
};

auto drain_notes() -> std::vector<std::string> {
  std::vector<std::string> notes;
  harddisk_loader_drain_rejections(
      [](void* context, const char*, const char* reason) {
        static_cast<std::vector<std::string>*>(context)->emplace_back(reason);
      },
      &notes);
  return notes;
}

class ScopedFileMode_t {
 public:
  ScopedFileMode_t(std::string path, mode_t new_mode, mode_t restore_mode)
      : path_(std::move(path)), restore_mode_(restore_mode) {
    chmod(path_.c_str(), new_mode);
  }
  ~ScopedFileMode_t() { chmod(path_.c_str(), restore_mode_); }
  ScopedFileMode_t(const ScopedFileMode_t&) = delete;
  auto operator=(const ScopedFileMode_t&) -> ScopedFileMode_t& = delete;
  ScopedFileMode_t(ScopedFileMode_t&&) = delete;
  auto operator=(ScopedFileMode_t&&) -> ScopedFileMode_t& = delete;

 private:
  std::string path_;
  mode_t restore_mode_;
};

// A host with every member the card asks for and nothing behind any of them,
// for a case that needs the card's answer and not a machine.
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
// Justification: Log is variadic in the HostInterface_t ABI.
auto silent_log(void*, PeripheralLogLevel_t, const char*, ...) -> void {}
// NOLINTEND(cppcoreguidelines-pro-type-vararg)
auto silent_register_io(int, PeripheralIOHandler, PeripheralIOHandler,
                        PeripheralIOHandler, PeripheralIOHandler) -> void {}
auto silent_register_cx_rom(int, const uint8_t*) -> void {}
auto silent_notify_status(int) -> void {}
auto silent_notify_activity(int, bool) -> void {}
auto silent_floating_bus(uint32_t) -> uint8_t { return 0; }

auto silent_host() -> HostInterface_t {
  HostInterface_t host{};
  host.Log = silent_log;
  host.RegisterIO = silent_register_io;
  host.RegisterCxROM = silent_register_cx_rom;
  host.NotifyStatusChanged = silent_notify_status;
  host.NotifyActivityChanged = silent_notify_activity;
  host.ReadFloatingBus = silent_floating_bus;
  return host;
}

}  // namespace

TEST_CASE(
    "Harddisk drivers: a ProDOS-order image opens through the loader and its "
    "volume directory key block reads back through the driver") {
  const OpenImage_t image(TestFixtures::get_fixture_path("minimal.po"));
  REQUIRE(image.error == harddisk_err_none);
  REQUIRE(image.driver != nullptr);
  REQUIRE(image.instance != nullptr);
  CHECK(std::string(image.driver->name) == "ProDOS Order");
  CHECK(image.blocks() == dos_image_blocks);
  CHECK(is_key_block(image.read(volume_directory_key_block)));
}

TEST_CASE(
    "Harddisk drivers: a 140 K image is decoded by its contents where they "
    "read coherently, by its name otherwise, and never refused for them") {
  TestFixtures::ScopedTempDir_t dir("linapple_hdd_drivers_");
  TestConfig_t config(harddisk_in_slot_7());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(peripheral_present(card_slot, harddisk_id));
  TestFixtures::ScopedLogCapture_t log;

  SUBCASE("a DOS-order ProDOS volume is served through the sector map") {
    const std::string path =
        TestFixtures::get_fixture_path("minimal-prodos.dsk");
    insert(0, path);
    REQUIRE(status().drive0_loaded == 1);
    CHECK(is_key_block(card_read(0, volume_directory_key_block).bytes));
    const CardRead_t block0 = card_read(0, 0);
    REQUIRE(block0.result == prodos_ok);
    const std::vector<uint8_t> file = read_file(path);
    CHECK(std::vector<uint8_t>(block0.bytes.begin(),
                               block0.bytes.begin() + half_block) ==
          slice(file, 0, half_block));
    CHECK(card_status(0) == prodos_ok);
    CHECK(card_block_count() == dos_image_blocks);
  }

  SUBCASE("a ProDOS-order volume under its own name is served linearly") {
    const std::string path = TestFixtures::get_fixture_path("minimal.po");
    insert(0, path);
    REQUIRE(status().drive0_loaded == 1);
    const CardRead_t block2 = card_read(0, volume_directory_key_block);
    REQUIRE(block2.result == prodos_ok);
    CHECK(is_key_block(block2.bytes));
    CHECK(std::vector<uint8_t>(block2.bytes.begin(), block2.bytes.end()) ==
          slice(read_file(path), 2 * block_size, block_size));
    CHECK(log.count_containing("would name it truly") == 0);
  }

  SUBCASE("a ProDOS-order volume named .dsk is still served linearly") {
    const std::string path = copy_as(dir, "minimal.po", "renamed.dsk");
    insert(0, path);
    REQUIRE(status().drive0_loaded == 1);
    CHECK(is_key_block(card_read(0, volume_directory_key_block).bytes));
    CHECK(log.count_containing("would name it truly") == 1);
    CHECK(log.count_containing("renamed.po") == 1);
  }

  SUBCASE(
      "a DOS-order volume named .po is served through the map and the "
      "rename that would make the name true is logged once") {
    const std::string path = copy_as(dir, "minimal-prodos.dsk", "renamed.po");
    insert(0, path);
    REQUIRE(status().drive0_loaded == 1);
    CHECK(is_key_block(card_read(0, volume_directory_key_block).bytes));
    CHECK(log.count_containing("would name it truly") == 1);
    CHECK(log.count_containing("renamed.do") == 1);
  }

  SUBCASE("a blank 140 K image follows its name: .dsk DOS, .po ProDOS") {
    const std::string dsk = zero_file(dir, "blank.dsk", dos_image_size);
    insert(0, dsk);
    REQUIRE(status().drive0_loaded == 1);
    CHECK(write_block_1_lands_in_dos_order(dsk));

    const std::string po = zero_file(dir, "blank.po", dos_image_size);
    insert(0, po);
    REQUIRE(status().drive0_loaded == 1);
    CHECK_FALSE(write_block_1_lands_in_dos_order(po));
  }

  SUBCASE("a 140 K .hdv is DOS order only when its contents say so") {
    const std::string blank = zero_file(dir, "blank.hdv", dos_image_size);
    insert(0, blank);
    REQUIRE(status().drive0_loaded == 1);
    CHECK_FALSE(write_block_1_lands_in_dos_order(blank));

    const std::string dos = copy_as(dir, "minimal-prodos.dsk", "volume.hdv");
    insert(0, dos);
    REQUIRE(status().drive0_loaded == 1);
    CHECK(is_key_block(card_read(0, volume_directory_key_block).bytes));
  }

  SUBCASE("a DOS 3.3 volume is served, in DOS order, not refused") {
    const auto scratch = TestFixtures::create_ephemeral("minimal-dos33.dsk");
    insert(0, scratch.path());
    REQUIRE(status().drive0_loaded == 1);
    CHECK(status().drive0_last_error == harddisk_err_none);
    CHECK(card_block_count() == 0);
    CHECK(card_status(0) == prodos_ok);
    CHECK(card_block_count() == dos_image_blocks);
    CHECK(write_block_1_lands_in_dos_order(scratch.path()));
  }

  SUBCASE("an image coherent in neither order is DOS order by its name") {
    const auto scratch = TestFixtures::create_ephemeral("minimal.dsk");
    insert(0, scratch.path());
    REQUIRE(status().drive0_loaded == 1);
    CHECK(write_block_1_lands_in_dos_order(scratch.path()));
  }

  SUBCASE(
      "a .do of the one DOS-order size is served; any other size is "
      "not a hard disk image") {
    insert(0, zero_file(dir, "blank.do", dos_image_size));
    CHECK(status().drive0_loaded == 1);
    insert(0, zero_file(dir, "odd.do", 144000));
    CHECK(status().drive0_loaded == 0);
    CHECK(status().drive0_last_error == harddisk_err_invalid_format);
  }

  SUBCASE("a 512-byte .hdv of letters is a disk of one block") {
    const std::string path = dir.path() + "/letters.hdv";
    write_file(path, std::vector<uint8_t>(block_size, 'A'));
    insert(0, path);
    REQUIRE(status().drive0_loaded == 1);
    CHECK(card_status(0) == prodos_ok);
    CHECK(card_block_count() == 1);
    const CardRead_t block0 = card_read(0, 0);
    CHECK(block0.result == prodos_ok);
    CHECK(all_equal(block0.bytes, 'A'));
    CHECK(card_read(0, 1).result == prodos_io_error);
  }
}

TEST_CASE(
    "Harddisk drivers: a 2MG is served by its header's fields, its chunks "
    "never served as blocks, and a malformed header refused") {
  TestFixtures::ScopedTempDir_t dir("linapple_hdd_2mg_");
  TestConfig_t config(harddisk_in_slot_7());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(peripheral_present(card_slot, harddisk_id));

  SUBCASE("format 1 serves the blocks data_length names") {
    insert(0, TestFixtures::get_fixture_path("minimal-block.2mg"));
    REQUIRE(status().drive0_loaded == 1);
    CHECK(card_status(0) == prodos_ok);
    CHECK(card_block_count() == hdv_blocks);
    CHECK(all_equal(card_read(0, 1).bytes, 0x01));
    CHECK(all_equal(card_read(0, 15).bytes, 0x0F));
  }

  SUBCASE("a comment chunk is out of reach of READ and WRITE") {
    const auto scratch =
        TestFixtures::create_ephemeral("minimal-block-comment.2mg");
    const std::vector<uint8_t> before = read_file(scratch.path());
    insert(0, scratch.path());
    REQUIRE(status().drive0_loaded == 1);
    CHECK(card_status(0) == prodos_ok);
    CHECK(card_block_count() == hdv_blocks);
    CHECK(card_read(0, hdv_blocks).result == prodos_io_error);
    CHECK(card_write(0, hdv_blocks, pattern_block(0)) == prodos_io_error);
    eject(0);
    CHECK(read_file(scratch.path()) == before);
  }

  SUBCASE("a header claiming fewer blocks than the file holds serves those") {
    insert(0, TestFixtures::get_fixture_path("minimal-block-oversize.2mg"));
    REQUIRE(status().drive0_loaded == 1);
    CHECK(card_status(0) == prodos_ok);
    CHECK(card_block_count() == 8);
    CHECK(card_read(0, 7).result == prodos_ok);
    CHECK(card_read(0, 8).result == prodos_io_error);
  }

  SUBCASE(
      "a short header, a disagreeing length and a lost magic are "
      "refused") {
    for (const char* name :
         {"minimal-block-short-header.2mg", "minimal-block-disagree.2mg",
          "minimal-block-nomagic.2mg"}) {
      CAPTURE(name);
      insert(0, TestFixtures::get_fixture_path(name));
      CHECK(status().drive0_loaded == 0);
      CHECK(status().drive0_last_error == harddisk_err_invalid_format);
    }
  }

  SUBCASE("a nibble 2MG holds no blocks") {
    insert(0, TestFixtures::get_fixture_path("minimal-block-format2.2mg"));
    CHECK(status().drive0_loaded == 0);
    CHECK(status().drive0_last_error == harddisk_err_not_block_image);
  }

  SUBCASE("a DOS-order 2MG is decoded through the sector map") {
    insert(0, TestFixtures::get_fixture_path("minimal-block-format0.2mg"));
    REQUIRE(status().drive0_loaded == 1);
    CHECK(card_status(0) == prodos_ok);
    CHECK(card_block_count() == dos_image_blocks);
    CHECK(is_key_block(card_read(0, volume_directory_key_block).bytes));
  }

  SUBCASE("a MacBinary wrapper and the header are both stepped over") {
    insert(0, TestFixtures::get_fixture_path("minimal-macbinary.2mg"));
    REQUIRE(status().drive0_loaded == 1);
    CHECK(card_status(0) == prodos_ok);
    CHECK(card_block_count() == hdv_blocks);
    CHECK(all_equal(card_read(0, 1).bytes, 0x01));
  }

  SUBCASE("a zero data offset means the data follows the header") {
    std::vector<uint8_t> bytes =
        read_file(TestFixtures::get_fixture_path("minimal-block.2mg"));
    for (size_t i = 0x18; i < 0x1C; ++i) {
      bytes.at(i) = 0;
    }
    const std::string path = dir.path() + "/offset0.2mg";
    write_file(path, bytes);
    insert(0, path);
    REQUIRE(status().drive0_loaded == 1);
    CHECK(card_status(0) == prodos_ok);
    CHECK(card_block_count() == hdv_blocks);
    CHECK(all_equal(card_read(0, 1).bytes, 0x01));
  }

  SUBCASE("a locked 2MG is protected") {
    insert(0, TestFixtures::get_fixture_path("minimal-block-locked.2mg"));
    REQUIRE(status().drive0_loaded == 1);
    CHECK(status().drive0_write_protected == 1);
    CHECK(card_write(0, 1, pattern_block(0)) == prodos_write_protected);
    CHECK(card_read(0, 1).result == prodos_ok);
  }
}

TEST_CASE(
    "Harddisk drivers: nibble and flux images, an empty file, a missing path "
    "and an image past what ProDOS can count are each answered by name") {
  TestFixtures::ScopedTempDir_t dir("linapple_hdd_refuse_");
  TestConfig_t config(harddisk_in_slot_7());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(peripheral_present(card_slot, harddisk_id));
  TestFixtures::ScopedLogCapture_t log;

  for (const char* name : {"minimal.nib", "minimal.nb2", "minimal.woz"}) {
    CAPTURE(name);
    insert(0, TestFixtures::get_fixture_path(name));
    CHECK(status().drive0_loaded == 0);
    CHECK(status().drive0_last_error == harddisk_err_not_block_image);
    const OpenImage_t direct(TestFixtures::get_fixture_path(name));
    CHECK(direct.error == harddisk_err_not_block_image);
    CHECK(direct.instance == nullptr);
    REQUIRE(direct.driver != nullptr);
    CHECK(std::string(direct.driver->name) == "Nibble image");
  }

  insert(0, copy_as(dir, "minimal.woz", "flux.hdv"));
  CHECK(status().drive0_loaded == 0);
  CHECK(status().drive0_last_error == harddisk_err_not_block_image);

  insert(0, TestFixtures::get_fixture_path("minimal.txt"));
  CHECK(status().drive0_loaded == 0);
  CHECK(status().drive0_last_error == harddisk_err_invalid_format);

  insert(0, missing_path);
  CHECK(status().drive0_loaded == 0);
  CHECK(status().drive0_last_error == harddisk_err_not_found);

  const auto sparse = TestFixtures::create_ephemeral_blank(
      "huge.hdv", static_cast<size_t>(max_prodos_blocks + 1) * block_size);
  insert(0, sparse.path());
  REQUIRE(status().drive0_loaded == 1);
  CHECK(card_status(0) == prodos_ok);
  CHECK(card_block_count() == max_prodos_blocks);
  CHECK(card_read(0, max_prodos_blocks).result == prodos_ok);
  CHECK(log.count_containing("ProDOS can address 65535") == 1);
  const OpenImage_t direct(sparse.path());
  REQUIRE(direct.error == harddisk_err_none);
  CHECK(direct.blocks() == max_prodos_blocks + 1);
}

TEST_CASE(
    "Harddisk drivers: the extension list is the registry's name order and "
    "then the container's, the same before, with and after a card") {
  const std::string expected =
      "2mg;2img;2meg;do;dsk;nib;nb2;woz;po;hdv;img;gz;zip";
  std::array<char, 256> before{};
  CHECK(harddisk_loader_get_supported_extensions(
            before.data(), before.size()) == expected.size());
  CHECK(std::string(before.data()) == expected);
  {
    TestConfig_t config(harddisk_in_slot_7());
    TestFixtures::ScopedCore_t core(config);
    REQUIRE(peripheral_present(card_slot, harddisk_id));
    std::array<char, 256> queried{};
    size_t size = queried.size();
    REQUIRE(peripheral_query(card_slot, harddisk_query_supported_extensions,
                             queried.data(), &size) == peripheral_ok);
    CHECK(std::string(queried.data()) == expected);
    CHECK(linapple_is_supported_disk_image("TEST.2MG"));
    CHECK(linapple_is_supported_disk_image("volume.hdv"));
    CHECK_FALSE(linapple_is_supported_disk_image("notes.txt"));
  }
  std::array<char, 256> after{};
  CHECK(harddisk_loader_get_supported_extensions(after.data(), after.size()) ==
        expected.size());
  CHECK(std::string(after.data()) == expected);
}

TEST_CASE(
    "Harddisk drivers: a written block is in the file before the next "
    "register access, survives the eject, and reads back on another "
    "machine") {
  const auto scratch = TestFixtures::create_ephemeral("minimal-block.hdv");
  const std::array<uint8_t, block_size> pattern = pattern_block(0x11);
  {
    TestConfig_t config(harddisk_in_slot_7());
    TestFixtures::ScopedCore_t core(config);
    REQUIRE(peripheral_present(card_slot, harddisk_id));
    insert(0, scratch.path());
    REQUIRE(status().drive0_loaded == 1);

    REQUIRE(card_write(0, 5, pattern) == prodos_ok);
    const std::vector<uint8_t> file = read_file(scratch.path());
    CHECK(slice(file, 5 * block_size, block_size) ==
          std::vector<uint8_t>(pattern.begin(), pattern.end()));
    CHECK(slice(file, 4 * block_size, block_size) ==
          std::vector<uint8_t>(block_size, 4));
    CHECK(slice(file, 6 * block_size, block_size) ==
          std::vector<uint8_t>(block_size, 6));
    eject(0);
    CHECK(read_file(scratch.path()) == file);
  }
  {
    TestConfig_t config(harddisk_in_slot_7());
    TestFixtures::ScopedCore_t core(config);
    insert(0, scratch.path());
    REQUIRE(status().drive0_loaded == 1);
    const CardRead_t block5 = card_read(0, 5);
    CHECK(block5.result == prodos_ok);
    CHECK(block5.bytes == pattern);
  }
}

TEST_CASE(
    "Harddisk drivers: a read-only file, a locked 2MG and an archive are "
    "protected, refuse WRITE with the ProDOS code and still read") {
  TestConfig_t config(harddisk_in_slot_7());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(peripheral_present(card_slot, harddisk_id));

  // Superuser bypasses the file mode, as test_disk_prot does.
  if (getuid() != 0) {
    const auto scratch = TestFixtures::create_ephemeral("minimal-block.hdv");
    const ScopedFileMode_t readonly(scratch.path(), 0444, 0644);
    insert(0, scratch.path());
    REQUIRE(status().drive0_loaded == 1);
    CHECK(status().drive0_write_protected == 1);
    CHECK(card_write(0, 1, pattern_block(0)) == prodos_write_protected);
    CHECK(all_equal(card_read(0, 1).bytes, 0x01));
  }

  insert(0, TestFixtures::get_fixture_path("minimal-block-locked.2mg"));
  REQUIRE(status().drive0_loaded == 1);
  CHECK(status().drive0_write_protected == 1);
  CHECK(card_write(0, 1, pattern_block(0)) == prodos_write_protected);
  CHECK(all_equal(card_read(0, 1).bytes, 0x01));

  insert(0, TestFixtures::get_fixture_path("minimal-block.hdv.gz"));
  REQUIRE(status().drive0_loaded == 1);
  CHECK(status().drive0_write_protected == 1);
  CHECK(card_write(0, 1, pattern_block(0)) == prodos_write_protected);
  CHECK(all_equal(card_read(0, 1).bytes, 0x01));

  const OpenImage_t archive(
      TestFixtures::get_fixture_path("minimal-block.hdv.gz"));
  REQUIRE(archive.error == harddisk_err_none);
  CHECK(archive.driver->is_write_protected(archive.instance));
  const std::array<uint8_t, block_size> pattern = pattern_block(0);
  CHECK(archive.driver->write_block(archive.instance, 1, pattern.data()) ==
        harddisk_err_read_only);
}

TEST_CASE(
    "Harddisk drivers: the card's sector map is Fig. 3.14 of Beneath Apple "
    "ProDOS, and the content check finds each file system in its order") {
  TestFixtures::ScopedTempDir_t dir("linapple_hdd_map_");

  // DOS sector s of every track holds the byte s, so the file is built from
  // sector numbers alone and a block reads as the pair the figure names.
  std::vector<uint8_t> image(dos_image_size, 0);
  for (size_t track = 0; track < 35; ++track) {
    for (size_t sector = 0; sector < 16; ++sector) {
      const size_t offset = (track * track_size) + (sector * sector_size);
      std::fill_n(image.begin() + static_cast<ptrdiff_t>(offset), sector_size,
                  static_cast<uint8_t>(sector));
    }
  }
  const std::string path = dir.path() + "/sectors.dsk";
  write_file(path, image);

  const OpenImage_t opened(path);
  REQUIRE(opened.error == harddisk_err_none);
  CHECK(std::string(opened.driver->name) == "DOS Order");
  for (uint32_t track = 0; track < 35; ++track) {
    for (uint32_t k = 0; k < 8; ++k) {
      const std::array<uint8_t, block_size> block =
          opened.read((track * 8) + k);
      CAPTURE(track);
      CAPTURE(k);
      CHECK(block.at(0) == k_fig_3_14.at(k).at(0));
      CHECK(block.at(half_block - 1) == k_fig_3_14.at(k).at(0));
      CHECK(block.at(half_block) == k_fig_3_14.at(k).at(1));
      CHECK(block.at(block_size - 1) == k_fig_3_14.at(k).at(1));
    }
  }

  const auto probe = [](const char* fixture, BlockDiskOrder_e order) {
    const std::vector<uint8_t> bytes =
        read_file(TestFixtures::get_fixture_path(fixture));
    return block_disk_image_probe_signature(bytes.data(), bytes.size(),
                                            bytes.size(), order);
  };
  CHECK(probe("minimal.po", block_disk_order_prodos) ==
        harddisk_probe_definite);
  CHECK(probe("minimal.po", block_disk_order_dos) == harddisk_probe_possible);
  CHECK(probe("minimal-prodos.dsk", block_disk_order_dos) ==
        harddisk_probe_definite);
  CHECK(probe("minimal-prodos.dsk", block_disk_order_prodos) ==
        harddisk_probe_possible);
  CHECK(probe("minimal-dos33.dsk", block_disk_order_dos) ==
        harddisk_probe_definite);
  CHECK(probe("minimal-dos33.dsk", block_disk_order_prodos) ==
        harddisk_probe_possible);
  CHECK(probe("minimal.dsk", block_disk_order_dos) == harddisk_probe_possible);
  CHECK(probe("minimal.dsk", block_disk_order_prodos) ==
        harddisk_probe_possible);

  std::array<uint8_t, 100> short_header{};
  CHECK(block_disk_image_probe_signature(
            short_header.data(), short_header.size(), dos_image_size,
            block_disk_order_dos) == harddisk_probe_possible);
  CHECK(block_disk_image_probe_signature(
            short_header.data(), short_header.size(), dos_image_size,
            block_disk_order_prodos) == harddisk_probe_possible);
  CHECK(block_disk_image_probe_signature(
            short_header.data(), short_header.size(), block_size,
            block_disk_order_prodos) == harddisk_probe_no);
}

TEST_CASE(
    "Harddisk drivers: the content check reads nothing past the header it is "
    "given, whether that is the loader's window or a hundred bytes") {
  // The loader reads this much ahead before it asks a driver.
  constexpr size_t probe_window = 80 * 1024;
  const auto probe = [](const char* fixture, size_t header_size,
                        BlockDiskOrder_e order) {
    const std::vector<uint8_t> bytes =
        read_file(TestFixtures::get_fixture_path(fixture));
    REQUIRE(bytes.size() >= header_size);
    GuardedBuffer_t header(header_size);
    std::copy_n(bytes.begin(), static_cast<ptrdiff_t>(header_size),
                header.data());
    return block_disk_image_probe_signature(header.data(), header_size,
                                            bytes.size(), order);
  };
  CHECK(probe("minimal-dos33.dsk", probe_window, block_disk_order_dos) ==
        harddisk_probe_definite);
  CHECK(probe("minimal-dos33.dsk", probe_window, block_disk_order_prodos) ==
        harddisk_probe_possible);
  CHECK(probe("minimal-prodos.dsk", probe_window, block_disk_order_dos) ==
        harddisk_probe_definite);
  CHECK(probe("minimal.po", probe_window, block_disk_order_prodos) ==
        harddisk_probe_definite);

  GuardedBuffer_t short_header(100);
  std::fill_n(short_header.data(), short_header.size(), 0);
  CHECK(block_disk_image_probe_signature(
            short_header.data(), short_header.size(), dos_image_size,
            block_disk_order_dos) == harddisk_probe_possible);
  CHECK(block_disk_image_probe_signature(
            short_header.data(), short_header.size(), dos_image_size,
            block_disk_order_prodos) == harddisk_probe_possible);
  CHECK(block_disk_image_probe_signature(
            short_header.data(), short_header.size(), block_size,
            block_disk_order_prodos) == harddisk_probe_no);
}

TEST_CASE(
    "Harddisk drivers: an image the loader refuses is the insert's answer, "
    "an error to a direct caller and the drive's last error through the "
    "queue") {
  Peripheral_t* descriptor = peripheral_find_internal(harddisk_id);
  REQUIRE(descriptor != nullptr);

  HarddiskInsertCmd_t cmd{};
  cmd.drive = harddisk_drive_0;
  std::strncpy(cmd.path, missing_path, sizeof(cmd.path) - 1);

  HostInterface_t host = silent_host();
  void* instance = descriptor->init(card_slot, &host);
  REQUIRE(instance != nullptr);
  CHECK(descriptor->command(instance, harddisk_cmd_insert, &cmd, sizeof(cmd)) ==
        peripheral_error);
  HarddiskStatus_t direct{};
  size_t size = sizeof(direct);
  REQUIRE(descriptor->query(instance, harddisk_query_status, &direct, &size) ==
          peripheral_ok);
  CHECK(direct.drive0_loaded == 0);
  CHECK(direct.drive0_last_error == harddisk_err_not_found);
  descriptor->shutdown(instance);

  TestConfig_t config(harddisk_in_slot_7());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(peripheral_present(card_slot, harddisk_id));
  CHECK(peripheral_command(card_slot, harddisk_cmd_insert, &cmd, sizeof(cmd)) ==
        peripheral_ok);
  settle();
  HarddiskStatus_t queued = status();
  CHECK(queued.drive0_loaded == 0);
  CHECK(queued.drive0_last_error == harddisk_err_not_found);
  CHECK(drain_notes().empty());
}
