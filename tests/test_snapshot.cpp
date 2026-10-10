// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Snapshot.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#ifdef ENABLE_PERIPHERAL_HARDDISK
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#endif
#ifdef ENABLE_PERIPHERAL_CLOCK
#include "apple2/peripherals/clock/ClockCardCommands.h"
#endif
#ifdef ENABLE_PERIPHERAL_PRINTER
#include "apple2/peripherals/printer/PrinterCommands.h"
#endif
#ifdef ENABLE_PERIPHERAL_SUPER_SERIAL
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#endif
#ifdef ENABLE_PERIPHERAL_MOCKINGBOARD
#include "apple2/peripherals/mockingboard/MockingboardCommands.h"
#endif
#include "core/LinAppleCore.h"
#include "core/Util_Crc32.h"
#include "doctest.h"
#include "frontends/common/MouseFrontend.h"
#include "frontends/common/SaveStateManager.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {
// Declared rather than inherited. Nothing here reaches a card, and the slot
// fallbacks in peripheral_register_internal would put four of them in the
// snapshot.
using TestConfig_t = TestFixtures::ScopedTestConfig_t;
}  // namespace

TEST_CASE("Snapshot: [RoundTrip] Serialize and Deserialize") {
  TestConfig_t machine(TestConfig_t::enhanced_2e_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  uint8_t orig_a = cpu_get_registers()->a;
  uint8_t orig_x = cpu_get_registers()->x;
  uint8_t orig_y = cpu_get_registers()->y;
  uint16_t orig_pc = cpu_get_registers()->pc;
  uint16_t orig_sp = cpu_get_registers()->sp;
  uint64_t orig_cycles = cpu_get_cumulative_cycles();

  uint32_t orig_mem_mode = mem_get_active_context()->mem_mode;
  bool orig_last_write_ram = mem_get_active_context()->last_write_ram;

  uint8_t* mem_2000 = mem_get_main_ptr(0x2000);
  uint8_t orig_byte_2000 = *mem_2000;

  cpu_get_registers()->a = 0x11;
  cpu_get_registers()->x = 0x22;
  cpu_get_registers()->y = 0x33;
  cpu_get_registers()->pc = 0x1000;
  cpu_get_registers()->sp = 0x1FF;
  g_cumulative_cycles = 12345;

  mem_get_active_context()->mem_mode =
      MF_HRAM_BANK2 | MF_SLOTCXROM | MF_HRAM_WRITE;
  mem_get_active_context()->last_write_ram = true;
  *mem_2000 = 0x55;

  auto snapshot = std::unique_ptr<Snapshot_t>(new Snapshot_t());
  snapshot_serialize(snapshot.get());

  cpu_get_registers()->a = 0xFF;
  cpu_get_registers()->x = 0xFF;
  cpu_get_registers()->y = 0xFF;
  cpu_get_registers()->pc = 0x9999;
  cpu_get_registers()->sp = 0x100;
  g_cumulative_cycles = 99999;

  mem_get_active_context()->mem_mode = MF_80STORE | MF_ALTZP;
  mem_get_active_context()->last_write_ram = false;
  *mem_2000 = 0xAA;

  bool success = snapshot_deserialize(snapshot.get());
  REQUIRE(success);

  CHECK(cpu_get_registers()->a == 0x11);
  CHECK(cpu_get_registers()->x == 0x22);
  CHECK(cpu_get_registers()->y == 0x33);
  CHECK(cpu_get_registers()->pc == 0x1000);
  CHECK(cpu_get_registers()->sp == 0x1FF);
  CHECK(cpu_get_cumulative_cycles() == 12345);

  CHECK(mem_get_active_context()->mem_mode ==
        (MF_HRAM_BANK2 | MF_SLOTCXROM | MF_HRAM_WRITE));
  CHECK(mem_get_active_context()->last_write_ram == true);
  CHECK(*mem_2000 == 0x55);

  cpu_get_registers()->a = orig_a;
  cpu_get_registers()->x = orig_x;
  cpu_get_registers()->y = orig_y;
  cpu_get_registers()->pc = orig_pc;
  cpu_get_registers()->sp = orig_sp;
  g_cumulative_cycles = orig_cycles;

  mem_get_active_context()->mem_mode = orig_mem_mode;
  mem_get_active_context()->last_write_ram = orig_last_write_ram;
  *mem_2000 = orig_byte_2000;

  linapple_shutdown();
}

TEST_CASE("SaveStateManager: Filename management and Load/Save flow") {
  TestConfig_t machine(TestConfig_t::enhanced_2e_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  save_state_set_filename("test_custom_snapshot.aws");
  CHECK(strcmp(save_state_get_filename(), "test_custom_snapshot.aws") == 0);

  save_state_set_filename(nullptr);
  CHECK(strcmp(save_state_get_filename(), "") == 0);

  TestFixtures::ScopedTempFile_t test_file(".aws");
  save_state_set_filename(test_file.c_str());
  save_state_save();

  CHECK(access(test_file.c_str(), F_OK) == 0);
  struct stat written{};
  REQUIRE(stat(test_file.c_str(), &written) == 0);
  CHECK(static_cast<size_t>(written.st_size) == sizeof(Snapshot_t));
  CHECK(save_state_load());

  linapple_shutdown();
}

namespace {

constexpr size_t fake_state_size = 32;

// A card whose whole state is 32 bytes: too big for the 16-byte regions the
// fixed body gives slots 1, 3 and 7, an exact fit for slot 2's.
struct FakeCard_t {
  std::array<uint8_t, fake_state_size> state{};
  size_t last_load_size = 0;
};

std::array<FakeCard_t*, num_slots> g_fake_cards{};

auto fake_init(int slot, HostInterface_t* host) -> void* {
  (void)host;
  auto* card = new FakeCard_t();
  g_fake_cards.at(static_cast<size_t>(slot)) = card;
  return card;
}

auto fake_shutdown(void* instance) -> void {
  auto* card = static_cast<FakeCard_t*>(instance);
  for (auto*& slot : g_fake_cards) {
    if (slot == card) {
      slot = nullptr;
    }
  }
  delete card;
}

auto fake_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  if (size == nullptr) {
    return peripheral_error;
  }
  if (buffer == nullptr) {
    *size = fake_state_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *size < fake_state_size) {
    return peripheral_error;
  }
  memcpy(buffer, static_cast<FakeCard_t*>(instance)->state.data(),
         fake_state_size);
  *size = fake_state_size;
  return peripheral_ok;
}

auto fake_load_state(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  auto* card = static_cast<FakeCard_t*>(instance);
  if (card == nullptr) {
    return peripheral_error;
  }
  card->last_load_size = size;
  if (buffer == nullptr || size < fake_state_size) {
    return peripheral_error;
  }
  memcpy(card->state.data(), buffer, fake_state_size);
  return peripheral_ok;
}

Peripheral_t g_fake_card = {
    LINAPPLE_ABI_VERSION,
    "test.fake_card",
    "Fake Card",
    "Thirty-two bytes of state",
    "LinApple Contributors",
    "1.0.0",
    PERIPHERAL_MASK_EXPANSION,
    -1,
    fake_init,
    nullptr,
    fake_shutdown,
    nullptr,
    nullptr,
    fake_save_state,
    fake_load_state,
    nullptr,
    nullptr,
};

auto pattern_for(int slot) -> std::array<uint8_t, fake_state_size> {
  std::array<uint8_t, fake_state_size> pattern{};
  for (size_t i = 0; i < pattern.size(); ++i) {
    pattern[i] = static_cast<uint8_t>((slot << 4) | i);
  }
  return pattern;
}

}  // namespace

TEST_CASE("Snapshot: A 32-byte card state rides the trailer through any slot") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  const std::array<int, 3> slots = {1, 4, 2};
  for (int slot : slots) {
    REQUIRE(peripheral_register(&g_fake_card, slot) == 0);
    REQUIRE(g_fake_cards.at(static_cast<size_t>(slot)) != nullptr);
    g_fake_cards.at(static_cast<size_t>(slot))->state = pattern_for(slot);
  }

  auto snapshot = std::unique_ptr<Snapshot_t>(new Snapshot_t());
  snapshot_serialize(snapshot.get());

  CHECK(snapshot->hdr.version == snapshot_version);
  CHECK(snapshot->slot_trailer.unit_hdr.length == sizeof(SsSlotTrailer_t));
  for (int slot : slots) {
    const SsSlotState_t& entry = snapshot->slot_trailer.slots[slot - 1];
    CHECK(entry.length == fake_state_size);
    CHECK(memcmp(entry.data, pattern_for(slot).data(), fake_state_size) == 0);
  }
  CHECK(snapshot->slot_trailer.slots[2].length == 0);
  CHECK(snapshot->slot_trailer.slots[4].length == 0);
  CHECK(snapshot->slot_trailer.slots[5].length == 0);
  CHECK(snapshot->slot_trailer.slots[6].length == 0);
  CHECK(sizeof(snapshot->empty1) < fake_state_size);
  CHECK(sizeof(snapshot->mockingboard1) > fake_state_size);
  CHECK(sizeof(snapshot->apple2_unit.comms) == fake_state_size);

  for (int slot : slots) {
    g_fake_cards.at(static_cast<size_t>(slot))->state.fill(0xFF);
    g_fake_cards.at(static_cast<size_t>(slot))->last_load_size = 0;
  }

  REQUIRE(snapshot_deserialize(snapshot.get()));

  for (int slot : slots) {
    const FakeCard_t* card = g_fake_cards.at(static_cast<size_t>(slot));
    CHECK(card->state == pattern_for(slot));
    CHECK(card->last_load_size == fake_state_size);
  }
}

TEST_CASE("Snapshot: An impossible slot length refuses the file untouched") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(peripheral_register(&g_fake_card, 1) == 0);
  FakeCard_t* card = g_fake_cards.at(1);
  REQUIRE(card != nullptr);

  auto snapshot = std::unique_ptr<Snapshot_t>(new Snapshot_t());
  snapshot_serialize(snapshot.get());
  snapshot->slot_trailer.slots[0].length = snapshot_slot_state_capacity + 1;

  card->state.fill(0x77);
  card->last_load_size = 0;
  const uint8_t a_before = cpu_get_registers()->a = 0x42;

  CHECK(snapshot_deserialize(snapshot.get()) == false);
  CHECK(card->last_load_size == 0);
  CHECK(card->state == std::array<uint8_t, fake_state_size>{
                           0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,
                           0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,
                           0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,
                           0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77});
  CHECK(cpu_get_registers()->a == a_before);
}

// minimal.aws names a Parallel Printer, an SSC and a Mockingboard, so the
// cases that load it need all three cards.
#if defined(ENABLE_PERIPHERAL_PRINTER) &&      \
    defined(ENABLE_PERIPHERAL_SUPER_SERIAL) && \
    defined(ENABLE_PERIPHERAL_MOCKINGBOARD)
TEST_CASE("Snapshot: A fixed-body file loads with its slots intact") {
  // Verify snapshot matches golden fixture written by legacy writer.
  TestConfig_t::Description_t description;
  description.slots[0] = "Parallel Printer";
  description.slots[1] = "Super Serial Card";
  description.slots[3] = "Mockingboard";
  TestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);

  const std::string path = TestFixtures::get_fixture_path("minimal.aws");
  REQUIRE(access(path.c_str(), R_OK) == 0);
  struct stat on_disk{};
  REQUIRE(stat(path.c_str(), &on_disk) == 0);
  CHECK(static_cast<size_t>(on_disk.st_size) == snapshot_size_fixed_body);
  {
    std::ifstream in(path, std::ios::binary);
    SsFileHdr_t hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    REQUIRE(in.good());
    CHECK(hdr.tag == aw_ss_tag);
    CHECK(hdr.version == snapshot_version);
  }

  cpu_get_registers()->a = 0;
  cpu_get_registers()->pc = 0;
  *mem_get_main_ptr(0x2000) = 0;

  save_state_set_filename(path.c_str());
  REQUIRE(save_state_load());

  CHECK(cpu_get_registers()->a == 0x11);
  CHECK(cpu_get_registers()->x == 0x22);
  CHECK(cpu_get_registers()->y == 0x33);
  CHECK(cpu_get_registers()->pc == 0x1000);
  CHECK(cpu_get_registers()->sp == 0x1FF);
  CHECK(cpu_get_cumulative_cycles() == 12345);
  CHECK(*mem_get_main_ptr(0x2000) == 0x55);

  SS_PERIPHERAL_MANIFEST manifest;
  peripheral_get_manifest(&manifest);
  // The built-ins register before any module, in descending-id order, so slot
  // 0's front is the first built-in with default_slot 0: the speaker in a
  // static build, whichever built-in remains in a plugin build.
  const Peripheral_t* front = nullptr;
  for (const Peripheral_t* p : peripheral_get_builtin_registry()) {
    if (p != nullptr && p->default_slot == 0) {
      front = p;
      break;
    }
  }
  REQUIRE(front != nullptr);
  CHECK(std::string(manifest.peripherals[0].name) == front->name);
  CHECK(std::string(manifest.peripherals[1].name) == "Parallel Printer");
  CHECK(std::string(manifest.peripherals[2].name) == "Super Serial Card");
  CHECK(manifest.peripherals[3].name[0] == '\0');
  CHECK(std::string(manifest.peripherals[4].name) == "Mockingboard");
  CHECK(manifest.peripherals[5].name[0] == '\0');
  CHECK(manifest.peripherals[6].name[0] == '\0');
  CHECK(manifest.peripherals[7].name[0] == '\0');
}
#endif

using TestFixtures::ScopedLogCapture_t;

#if defined(ENABLE_PERIPHERAL_PRINTER) &&      \
    defined(ENABLE_PERIPHERAL_SUPER_SERIAL) && \
    defined(ENABLE_PERIPHERAL_MOCKINGBOARD)
namespace {

constexpr size_t serial_frame_size = 56;
constexpr size_t serial_frame_control = 12;
constexpr size_t serial_frame_command = 13;
constexpr size_t comms_region_offset = 40;
constexpr size_t comms_region_size = 32;

}  // namespace

TEST_CASE(
    "Snapshot: A fixed-body file leaves the serial card at reset and says so "
    "once") {
  TestConfig_t::Description_t description;
  description.slots[0] = "Parallel Printer";
  description.slots[1] = "Super Serial Card";
  description.slots[3] = "Mockingboard";
  TestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);

  const std::string path = TestFixtures::get_fixture_path("minimal.aws");
  REQUIRE(access(path.c_str(), R_OK) == 0);
  save_state_set_filename(path.c_str());
  {
    ScopedLogCapture_t log;
    REQUIRE(save_state_load());
    CHECK(log.count_containing("Slot 2: Super Serial Card refused") == 1);
    CHECK(log.count_containing("Slot 2:") == 1);
    // An empty slot has nobody to refuse the region.
    CHECK(log.count_containing("Slot 3:") == 0);
    CHECK(log.count_containing("Slot 5:") == 0);
    CHECK(log.count_containing("Slot 7:") == 0);
  }

  std::array<uint8_t, snapshot_slot_state_capacity> frame{};
  frame.fill(0xFF);
  size_t frame_size = frame.size();
  peripheral_save_state(2, frame.data(), &frame_size);
  REQUIRE(frame_size == serial_frame_size);
  CHECK(frame[serial_frame_control] == 0);
  CHECK(frame[serial_frame_command] == 0);

  TestFixtures::ScopedTempFile_t written(".aws");
  save_state_set_filename(written.c_str());
  save_state_save();
  std::ifstream in(written.path(), std::ios::binary);
  REQUIRE(in.good());
  std::array<char, comms_region_size> region{};
  in.seekg(static_cast<std::streamoff>(comms_region_offset));
  in.read(region.data(), static_cast<std::streamsize>(region.size()));
  REQUIRE(in.good());
  for (char byte : region) {
    CHECK(byte == 0);
  }
  const std::streamoff trailer_slot_2 =
      static_cast<std::streamoff>(offsetof(Snapshot_t, slot_trailer.slots)) +
      static_cast<std::streamoff>(sizeof(SsSlotState_t));
  in.seekg(trailer_slot_2);
  uint32_t length = 0;
  in.read(reinterpret_cast<char*>(&length), sizeof(length));
  REQUIRE(in.good());
  CHECK(length == serial_frame_size);
}
#endif

TEST_CASE("Snapshot: The file's length says whether a trailer follows") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(peripheral_register(&g_fake_card, 1) == 0);
  FakeCard_t* card = g_fake_cards.at(1);
  REQUIRE(card != nullptr);
  card->state = pattern_for(1);

  TestFixtures::ScopedTempFile_t file(".aws");
  save_state_set_filename(file.c_str());
  save_state_save();

  struct stat written{};
  REQUIRE(stat(file.c_str(), &written) == 0);
  CHECK(static_cast<size_t>(written.st_size) == sizeof(Snapshot_t));
  {
    std::ifstream in(file.path(), std::ios::binary);
    SsFileHdr_t hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    REQUIRE(in.good());
    CHECK(hdr.version == snapshot_version);
    CHECK(hdr.version == make_version(1, 0, 0, 1));
  }

  std::array<uint8_t, fake_state_size> untouched{};
  untouched.fill(0xFF);
  card->state = untouched;
  card->last_load_size = 0;

  SUBCASE("the whole file loads the slot from its trailer") {
    REQUIRE(save_state_load());
    CHECK(card->last_load_size == fake_state_size);
    CHECK(card->state == pattern_for(1));
  }
  SUBCASE("the fixed body alone loads with an all-zero trailer") {
    REQUIRE(truncate(file.c_str(),
                     static_cast<off_t>(snapshot_size_fixed_body)) == 0);
    REQUIRE(save_state_load());
    // Fall back to fixed body when snapshot trailer is missing.
    CHECK(card->last_load_size == sizeof(SsCardEmpty_t));
    CHECK(card->state == untouched);
  }
}

TEST_CASE("Snapshot: A file of any other length is refused") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  TestFixtures::ScopedTempFile_t file(".aws");
  save_state_set_filename(file.c_str());
  save_state_save();
  REQUIRE(save_state_load());

  SUBCASE("one byte short of the trailer") {
    REQUIRE(truncate(file.c_str(),
                     static_cast<off_t>(sizeof(Snapshot_t) - 1)) == 0);
    CHECK(save_state_load() == false);
  }
  SUBCASE("one byte past the trailer") {
    std::ofstream out(file.path(), std::ios::binary | std::ios::app);
    out.put('\0');
    out.close();
    CHECK(save_state_load() == false);
  }
  SUBCASE("one byte past the fixed body") {
    REQUIRE(truncate(file.c_str(),
                     static_cast<off_t>(snapshot_size_fixed_body + 1)) == 0);
    CHECK(save_state_load() == false);
  }
  SUBCASE("one byte short of the fixed body") {
    REQUIRE(truncate(file.c_str(),
                     static_cast<off_t>(snapshot_size_fixed_body - 1)) == 0);
    CHECK(save_state_load() == false);
  }
}

TEST_CASE("Snapshot: A manifest naming any slot-0 device is the same machine") {
  // Snapshots must verify independently of static vs plugin registration order.
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  SS_PERIPHERAL_MANIFEST manifest;
  peripheral_get_manifest(&manifest);
  REQUIRE(peripheral_verify_manifest(&manifest));

  // Only a device the build has in slot 0 can be named there.
  const std::initializer_list<const char*> slot0_devices = {
      "Speaker",
#ifdef ENABLE_PERIPHERAL_KEYBOARD
      "Keyboard",
#endif
#ifdef ENABLE_PERIPHERAL_JOYSTICK
      "Joystick",
#endif
  };
  for (const char* device : slot0_devices) {
    snprintf(manifest.peripherals[0].name, sizeof(manifest.peripherals[0].name),
             "%s", device);
    CHECK_MESSAGE(peripheral_verify_manifest(&manifest), device);
  }

  snprintf(manifest.peripherals[0].name, sizeof(manifest.peripherals[0].name),
           "%s", "Mockingboard");
  CHECK(peripheral_verify_manifest(&manifest) == false);
}

#ifdef ENABLE_PERIPHERAL_KEYBOARD

namespace {

constexpr uint16_t addr_keyboard_data = 0xC000;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint8_t strobe_bit = 0x80;
constexpr size_t keyboard_region_size = 552;
// gzip's CRC-32 of tests/fixtures/minimal.aws as shipped, so a rewrite shows
// here as well as in the pin.
constexpr uint32_t minimal_aws_crc32 = 0x1DFDECD4;

// The model is process-wide and a harness-built machine leaves it behind.
struct EnhancedIIe_t {
  struct Model_t {
    Apple2Type saved = current_apple2_type;
    Model_t() { current_apple2_type = A2TYPE_APPLE2EENHANCED; }
    ~Model_t() { current_apple2_type = saved; }
    Model_t(const Model_t&) = delete;
    auto operator=(const Model_t&) -> Model_t& = delete;
    Model_t(Model_t&&) = delete;
    auto operator=(Model_t&&) -> Model_t& = delete;
  };
  Model_t model;
  TestConfig_t config{TestConfig_t::enhanced_2e_only()};
  TestFixtures::ScopedCore_t core{config};
};

auto press(uint8_t code) -> void {
  linapple_set_key_state(code, true);
  peripheral_manager_think(0);
}

auto release(uint8_t code) -> void {
  linapple_set_key_state(code, false);
  peripheral_manager_think(0);
}

auto keyboard_data() -> uint8_t {
  return io_map_dispatch(0, addr_keyboard_data, 0, 0, 0);
}

auto any_key_down() -> bool {
  return (io_map_dispatch(0, addr_keyboard_strobe, 0, 0, 0) & strobe_bit) != 0;
}

auto file_bytes(const std::string& path) -> std::vector<char> {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  return {(std::istreambuf_iterator<char>(in)),
          std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE(
    "Snapshot: an .aws gives the keyboard back its latch and strobe with no "
    "key down, a fixed-body image whose keyboard region is zero loads with "
    "the card at reset, and minimal.aws is the file it was") {
  static_assert(offsetof(Snapshot_t, apple2_unit.keyboard) == 80,
                "the keyboard region sits at byte 80 of the file");
  static_assert(sizeof(SsKeyboardRegion_t) == keyboard_region_size,
                "the keyboard region is the card's 552-byte frame");
  EnhancedIIe_t machine;
  TestFixtures::ScopedTempFile_t file(".aws");

  press(0x5A);
  REQUIRE(keyboard_data() == (0x5A | strobe_bit));
  save_state_set_filename(file.c_str());
  save_state_save();
  struct stat written{};
  REQUIRE(stat(file.c_str(), &written) == 0);
  CHECK(static_cast<size_t>(written.st_size) == 134200);

  // Z comes back under its strobe, read before $C010 since that read clears
  // it, and no key is down though one was held at the save.
  release(0x5A);
  press(0x51);
  REQUIRE(keyboard_data() == (0x51 | strobe_bit));
  REQUIRE(any_key_down());
  REQUIRE(save_state_load());
  CHECK(keyboard_data() == (0x5A | strobe_bit));
  CHECK_FALSE(any_key_down());
  CHECK(keyboard_data() == 0x5A);

  // The keyboard's 552 bytes zeroed and no trailer: the card refuses a
  // version-0 frame and stays at reset while the rest of the machine loads.
  {
    std::fstream patch(file.path(),
                       std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(patch.good());
    const std::array<char, keyboard_region_size> zeros{};
    patch.seekp(static_cast<std::streamoff>(
        offsetof(Snapshot_t, apple2_unit.keyboard)));
    patch.write(zeros.data(), zeros.size());
    REQUIRE(patch.good());
  }
  REQUIRE(truncate(file.c_str(),
                   static_cast<off_t>(snapshot_size_fixed_body)) == 0);
  press(0x51);
  REQUIRE(keyboard_data() == (0x51 | strobe_bit));
  REQUIRE(save_state_load());
  CHECK(keyboard_data() == 0);
  CHECK_FALSE(any_key_down());

  const std::string minimal = TestFixtures::get_fixture_path("minimal.aws");
  const std::vector<char> shipped = file_bytes(minimal);
  CHECK(shipped.size() == snapshot_size_fixed_body);
  CHECK(crc32_compute(shipped.data(), shipped.size()) == minimal_aws_crc32);
}

TEST_CASE(
    "Snapshot: the .aws an earlier card wrote loads on a machine with nothing "
    "in any slot and gives back its latch and strobe with no key down") {
  EnhancedIIe_t machine;
  press(0x41);
  REQUIRE(any_key_down());

  const std::string path =
      TestFixtures::get_fixture_path("keyboard-14249ec3.aws");
  REQUIRE(access(path.c_str(), R_OK) == 0);
  save_state_set_filename(path.c_str());
  REQUIRE(save_state_load());
  CHECK(keyboard_data() == (0x5A | strobe_bit));
  CHECK_FALSE(any_key_down());
  CHECK(keyboard_data() == 0x5A);
}

#endif

TEST_CASE("Snapshot: Memory snapshot null pointer defense") {
  CHECK(mem_get_snapshot(nullptr) == 1);
  CHECK(mem_set_snapshot(nullptr) == 1);
}

TEST_CASE("Snapshot: Deserialization null pointer defense") {
  CHECK(snapshot_deserialize(nullptr) == false);
}

namespace {

constexpr uint32_t cycles_per_frame = 17030;
constexpr uint16_t probe_address = 0x0300;

// Runs one instruction at probe_address and leaves the registers as the
// instruction left them, for the caller to read.
auto step_one(const std::array<uint8_t, 3>& instruction) -> void {
  TestFixtures::ScopedCore_t::poke(probe_address, instruction);
  cpu_get_registers()->pc = probe_address;
  REQUIRE(cpu_execute(0) > 0);
}

}  // namespace

TEST_CASE("Snapshot: The game port's eight bytes are written as zeros") {
  static_assert(offsetof(Snapshot_t, apple2_unit.joystick) == 72,
                "the game port's field sits at byte 72 of the file");
  static_assert(sizeof(SsIoJoystick_t) == 8,
                "the game port's field is eight bytes long");

  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(cpu_execute(cycles_per_frame) >= cycles_per_frame);
  // Any access to $C070 triggers the paddle timers (Apple II Reference Manual,
  // 1979, p. 99), so the card holds a running timer when the file is written.
  step_one({0xAD, 0x70, 0xC0});

  TestFixtures::ScopedTempFile_t file(".aws");
  save_state_set_filename(file.c_str());
  save_state_save();

  struct stat written{};
  REQUIRE(stat(file.c_str(), &written) == 0);
  CHECK(static_cast<size_t>(written.st_size) == 134200);

  std::ifstream in(file.path(), std::ios::binary);
  std::array<char, sizeof(SsIoJoystick_t)> field{};
  in.seekg(offsetof(Snapshot_t, apple2_unit.joystick));
  in.read(field.data(), field.size());
  REQUIRE(in.good());
  CHECK(field == std::array<char, sizeof(SsIoJoystick_t)>{});
}

#ifdef ENABLE_PERIPHERAL_JOYSTICK
TEST_CASE("Snapshot: A loaded file starts with every paddle timer expired") {
  constexpr uint8_t bit7 = 0x80;
  TestFixtures::ScopedTempFile_t file(".aws");
  uint64_t saved_cycles = 0;
  {
    TestConfig_t config(TestConfig_t::enhanced_2e_only());
    TestFixtures::ScopedCore_t core(config);
    // A frame of running puts the counter the file restores well past the
    // longest pulse, so only a trigger carried by the file could read high.
    REQUIRE(cpu_execute(cycles_per_frame) >= cycles_per_frame);
    step_one({0xAD, 0x70, 0xC0});
    saved_cycles = cpu_get_cumulative_cycles();
    REQUIRE(saved_cycles >= cycles_per_frame);
    save_state_set_filename(file.c_str());
    save_state_save();
  }

  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  save_state_set_filename(file.c_str());
  REQUIRE(save_state_load());
  REQUIRE(cpu_get_cumulative_cycles() == saved_cycles);

  // LDA $C064: paddle 0 read at the counter the file restored. Had the strobe
  // travelled with the file the timer would be charging.
  cpu_get_registers()->a = 0xFF;
  step_one({0xAD, 0x64, 0xC0});
  CHECK((cpu_get_registers()->a & bit7) == 0);
}
#endif

#ifdef ENABLE_PERIPHERAL_SUPER_SERIAL
namespace {

constexpr size_t ssc_frame_size = 56;
constexpr size_t ssc_control = 12;
constexpr size_t ssc_command = 13;
constexpr size_t ssc_irq = 14;
constexpr size_t ssc_latches = 27;
constexpr size_t ssc_receive_data = 52;
constexpr uint8_t ssc_control_9600_8n1 = 0x1E;
constexpr uint8_t ssc_command_rx_irq = 0x09;
constexpr uint8_t ssc_latch_rdrf = 0x08;

using SscFrame_t = std::array<uint8_t, ssc_frame_size>;

// A received byte with the receive interrupt latched.
auto ssc_frame_with_interrupt() -> SscFrame_t {
  SscFrame_t frame{};
  frame.at(0) = 0x01;
  frame.at(4) = static_cast<uint8_t>(ssc_frame_size);
  frame.at(ssc_control) = ssc_control_9600_8n1;
  frame.at(ssc_command) = ssc_command_rx_irq;
  frame.at(ssc_irq) = 0x01;
  frame.at(ssc_latches) = ssc_latch_rdrf;
  frame.at(ssc_receive_data) = 0xC1;
  return frame;
}

auto ssc_saved_frame(int slot) -> SscFrame_t {
  SscFrame_t frame{};
  frame.fill(0xFF);
  size_t size = frame.size();
  peripheral_save_state(slot, frame.data(), &size);
  REQUIRE(size == ssc_frame_size);
  return frame;
}

// snapshot_deserialize restores the CPU before the slots load, so the card's
// AssertIrq during load_state sticks and a CLI loop afterwards can prove it.
auto ssc_survives_the_file(int slot) -> void {
  TestFixtures::ScopedByteSink_t sink;
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots.at(static_cast<size_t>(slot - 1)) = "Super Serial Card";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  peripheral_manager_init();
  linapple_register_peripherals();
  linapple_reset_hard();

  const SscFrame_t saved = ssc_frame_with_interrupt();
  REQUIRE(peripheral_load_state(slot, saved.data(), saved.size()) ==
          peripheral_ok);
  CHECK(ssc_saved_frame(slot) == saved);

  TestFixtures::ScopedTempFile_t file(".aws");
  save_state_set_filename(file.c_str());
  save_state_save();

  linapple_reset_hard();
  CHECK(ssc_saved_frame(slot).at(ssc_control) == 0x00);

  REQUIRE(save_state_load());
  CHECK(ssc_saved_frame(slot) == saved);

  // The handler's status read releases the line, so it runs once.
  const auto hi = static_cast<uint8_t>(0xC0 + slot);
  const auto status = static_cast<uint8_t>(0x89 + (slot << 4));
  const std::array<uint8_t, 4> handler = {0xE6, 0x06, 0xAD, status};
  TestFixtures::ScopedCore_t::poke(0x0380, handler);
  const std::array<uint8_t, 2> handler_tail = {0x40, 0x00};
  TestFixtures::ScopedCore_t::poke(0x0384, handler_tail);
  const std::array<uint8_t, 2> vector = {0x80, 0x03};
  TestFixtures::ScopedCore_t::poke(0xFFFE, vector);
  // $C0n1 advances the card without touching the ACIA.
  const std::array<uint8_t, 7> main_loop = {
      0x58,            // CLI
      0xAD, 0x81, hi,  // LDA $C0n1
      0x4C, 0x01, 0x03,
  };
  TestFixtures::ScopedCore_t::poke(0x0300, main_loop);
  mem[0x06] = 0;
  CpuRegisters_t* regs = cpu_get_registers();
  regs->pc = 0x0300;
  regs->ps |= 0x04;
  uint32_t ran = 0;
  while (ran < 300) {
    ran += cpu_execute(0);
  }
  CHECK(mem[0x06] >= 1);
}

}  // namespace

TEST_CASE(
    "Snapshot: An .aws gives the serial card back its registers, latch byte "
    "and interrupt in any slot") {
  SUBCASE("slot 1") { ssc_survives_the_file(1); }
  SUBCASE("slot 2") { ssc_survives_the_file(2); }
  SUBCASE("slot 7") { ssc_survives_the_file(7); }
}
#endif

#ifdef ENABLE_PERIPHERAL_MOUSE

// The key's fixture was written with the shipped Disk II and Harddisk in
// slots 6 and 7; a build without both cards refuses it at the manifest before
// the key's slot is ever walked.
#if defined(ENABLE_PERIPHERAL_DISK) && defined(ENABLE_PERIPHERAL_HARDDISK)

namespace {

constexpr int mouse_key_slot = 4;
constexpr const char* mouse_card_id = "linapple.mouse";

// The shipped [Slots] with the key set: the machine a user of the key has had
// since the key stopped being read.
auto key_machine() -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[0] = "Parallel Printer";
  description.slots[1] = "Super Serial Card";
  description.slots[3] = "Mockingboard";
  description.slots[4] = "Mockingboard";
  description.slots[5] = "Disk II";
  description.slots[6] = "Harddisk";
  description.extras.push_back({"Configuration", "Mouse in slot 4", "1"});
  return description;
}

auto legacy_key_fixture() -> std::string {
  const std::string path =
      TestFixtures::get_fixture_path("mouse-key1-mockingboard-0aad3663.aws");
  REQUIRE(access(path.c_str(), R_OK) == 0);
  return path;
}

auto mouse_in_slot(int slot) -> bool {
  uint8_t active = 0;
  size_t size = sizeof(active);
  return peripheral_query_by_id(slot, mouse_card_id, mouse_query_is_active,
                                &active, &size) == peripheral_ok;
}

auto machine_names(int slot) -> std::string {
  SS_PERIPHERAL_MANIFEST manifest;
  peripheral_get_manifest(&manifest);
  return manifest.peripherals[slot].name;
}

auto page_reads_zero(int slot) -> bool {
  const auto page = static_cast<uint16_t>(0xC000 + (slot << 8));
  for (uint16_t i = 0; i < 16; ++i) {
    if (mem[page + i] != 0) {
      return false;
    }
  }
  return true;
}

auto manifest_name_offset(int slot) -> size_t {
  return offsetof(Snapshot_t, manifest) +
         offsetof(SsPeripheralManifest_t, peripherals) +
         (static_cast<size_t>(slot) * sizeof(SsPeripheralInfo_t));
}

auto file_names(const std::string& path, int slot) -> std::string {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  std::array<char, max_peripheral_name> name{};
  in.seekg(static_cast<std::streamoff>(manifest_name_offset(slot)));
  in.read(name.data(), static_cast<std::streamsize>(name.size()));
  REQUIRE(in.good());
  name.back() = '\0';
  return name.data();
}

auto patched_copy(const std::string& source,
                  const TestFixtures::ScopedTempFile_t& destination, size_t at,
                  const std::vector<uint8_t>& bytes) -> void {
  std::ifstream in(source, std::ios::binary);
  REQUIRE(in.good());
  std::vector<char> image((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
  REQUIRE(image.size() == sizeof(Snapshot_t));
  REQUIRE(at + bytes.size() <= image.size());
  for (size_t i = 0; i < bytes.size(); ++i) {
    image.at(at + i) = static_cast<char>(bytes[i]);
  }
  std::ofstream out(destination.path(), std::ios::binary | std::ios::trunc);
  REQUIRE(out.good());
  out.write(image.data(), static_cast<std::streamsize>(image.size()));
  REQUIRE(out.good());
}

auto name_bytes(const std::string& name) -> std::vector<uint8_t> {
  std::vector<uint8_t> bytes(max_peripheral_name, 0);
  for (size_t i = 0; i < name.size() && i + 1 < bytes.size(); ++i) {
    bytes[i] = static_cast<uint8_t>(name[i]);
  }
  return bytes;
}

auto legacy_file_loads_with_the_mockingboard(
    const TestConfig_t::Description_t& description, bool strip_slot_4_line)
    -> void {
  TestConfig_t config(description);
  if (strip_slot_4_line) {
    std::ifstream in(config.path());
    REQUIRE(in.is_open());
    std::vector<std::string> kept;
    std::string line;
    while (std::getline(in, line)) {
      if (line.compare(0, 7, "Slot 4 ") != 0) {
        kept.push_back(line);
      }
    }
    in.close();
    std::ofstream out(config.path(), std::ios::trunc);
    for (const std::string& kept_line : kept) {
      out << kept_line << "\n";
    }
  }
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(mouse_in_slot(mouse_key_slot));
  mouse_frontend_initialize();
  REQUIRE(mouse_frontend_card_present());

  save_state_set_filename(legacy_key_fixture().c_str());
  {
    ScopedLogCapture_t log;
    REQUIRE(save_state_load());
    CHECK(log.count_containing("for this session") == 1);
    CHECK(log.count_containing(
              "Slot 4: the save state holds Mockingboard where Mouse in slot 4 "
              "left Mouse Interface; loading with Mockingboard for this "
              "session") == 1);
  }
  CHECK_FALSE(mouse_in_slot(mouse_key_slot));
  CHECK(machine_names(mouse_key_slot) == "Mockingboard");
  CHECK(page_reads_zero(mouse_key_slot));
  CHECK_FALSE(mouse_frontend_card_present());

  TestFixtures::ScopedTempFile_t written(".aws");
  save_state_set_filename(written.c_str());
  save_state_save();
  CHECK(file_names(written.path(), mouse_key_slot) == "Mockingboard");
}

}  // namespace

TEST_CASE(
    "Snapshot: a save state from before Mouse in slot 4 was read loads with "
    "the Mockingboard back in slot 4 for the session") {
  SUBCASE("Slot 4 = Mockingboard") {
    legacy_file_loads_with_the_mockingboard(key_machine(), false);
  }
  SUBCASE("no Slot 4 entry, the fallback displaced") {
    legacy_file_loads_with_the_mockingboard(key_machine(), true);
  }
  SUBCASE("Slot 4 = linapple.mockingboard, the id spelling") {
    TestConfig_t::Description_t description = key_machine();
    description.slots[3] = "linapple.mockingboard";
    legacy_file_loads_with_the_mockingboard(description, false);
  }
}

TEST_CASE(
    "Snapshot: the legacy file and a file this build wrote swap slot 4 back "
    "and forth within one session") {
  TestConfig_t config(key_machine());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(mouse_in_slot(mouse_key_slot));

  TestFixtures::ScopedTempFile_t mine(".aws");
  save_state_set_filename(mine.c_str());
  save_state_save();
  REQUIRE(file_names(mine.path(), mouse_key_slot) == "Mouse Interface");

  ScopedLogCapture_t log;
  save_state_set_filename(legacy_key_fixture().c_str());
  REQUIRE(save_state_load());
  CHECK_FALSE(mouse_in_slot(mouse_key_slot));

  save_state_set_filename(mine.c_str());
  REQUIRE(save_state_load());
  CHECK(mouse_in_slot(mouse_key_slot));
  CHECK(machine_names(mouse_key_slot) == "Mouse Interface");
  CHECK(log.count_containing(
            "Slot 4: the save state holds Mouse Interface where Mouse in slot "
            "4 left Mockingboard; loading with Mouse Interface for this "
            "session") == 1);

  save_state_set_filename(legacy_key_fixture().c_str());
  REQUIRE(save_state_load());
  CHECK_FALSE(mouse_in_slot(mouse_key_slot));
  CHECK(log.count_containing("for this session") == 3);
  CHECK(log.count_containing(
            "Slot 4: the save state holds Mockingboard where Mouse in slot 4 "
            "left Mouse Interface; loading with Mockingboard for this "
            "session") == 2);
}

TEST_CASE(
    "Snapshot: the swap is refused for any other difference, and a refused "
    "file leaves slot 4 alone") {
  SUBCASE("the key at 0 with Slot 4 = Mouse Interface names both cards") {
    TestConfig_t::Description_t description = key_machine();
    description.slots[3] = "Mouse Interface";
    description.extras.clear();
    description.extras.push_back({"Configuration", "Mouse in slot 4", "0"});
    TestConfig_t config(description);
    TestFixtures::ScopedCore_t core(config);
    REQUIRE(mouse_in_slot(mouse_key_slot));

    save_state_set_filename(legacy_key_fixture().c_str());
    ScopedLogCapture_t log;
    CHECK(save_state_load() == false);
    CHECK(log.count_containing("Slot 4: the save state names Mockingboard "
                               "where the machine holds Mouse Interface") == 1);
    CHECK(log.count_containing("for this session") == 0);
    CHECK(mouse_in_slot(mouse_key_slot));
  }

  SUBCASE("a file naming the Clock Card in slot 4 under the key") {
    TestConfig_t config(key_machine());
    TestFixtures::ScopedCore_t core(config);
    TestFixtures::ScopedTempFile_t copy(".aws");
    patched_copy(legacy_key_fixture(), copy,
                 manifest_name_offset(mouse_key_slot),
                 name_bytes("Clock Card"));

    save_state_set_filename(copy.c_str());
    ScopedLogCapture_t log;
    CHECK(save_state_load() == false);
    CHECK(log.count_containing("for this session") == 0);
    CHECK(mouse_in_slot(mouse_key_slot));
  }

  SUBCASE("a file differing in slot 2 is refused before slot 4 is swapped") {
    TestConfig_t config(key_machine());
    TestFixtures::ScopedCore_t core(config);
    TestFixtures::ScopedTempFile_t copy(".aws");
    patched_copy(legacy_key_fixture(), copy, manifest_name_offset(2),
                 name_bytes("Clock Card"));

    save_state_set_filename(copy.c_str());
    ScopedLogCapture_t log;
    CHECK(save_state_load() == false);
    CHECK(log.count_containing("Slot 2: the save state names Clock Card") == 1);
    CHECK(log.count_containing("for this session") == 0);
    CHECK(mouse_in_slot(mouse_key_slot));
    CHECK(machine_names(2) == "Super Serial Card");
  }

  SUBCASE(
      "a file differing in slot 5, walked after slot 4, is refused with slot 4 "
      "kept") {
    TestConfig_t config(key_machine());
    TestFixtures::ScopedCore_t core(config);
    TestFixtures::ScopedTempFile_t copy(".aws");
    patched_copy(legacy_key_fixture(), copy, manifest_name_offset(5),
                 name_bytes("Clock Card"));

    save_state_set_filename(copy.c_str());
    ScopedLogCapture_t log;
    CHECK(save_state_load() == false);
    CHECK(log.count_containing("Slot 5: the save state names Clock Card") == 1);
    CHECK(log.count_containing("for this session") == 0);
    CHECK(mouse_in_slot(mouse_key_slot));
    CHECK(machine_names(mouse_key_slot) == "Mouse Interface");
    CHECK(machine_names(5) == "Mockingboard");
  }

  SUBCASE("a Slot 4 naming a card that does not exist displaced nothing") {
    TestConfig_t::Description_t description = key_machine();
    description.slots[3] = "No Such Card";
    TestConfig_t config(description);
    TestFixtures::ScopedCore_t core(config);
    REQUIRE(mouse_in_slot(mouse_key_slot));

    save_state_set_filename(legacy_key_fixture().c_str());
    ScopedLogCapture_t log;
    CHECK(save_state_load() == false);
    CHECK(log.count_containing("for this session") == 0);
    CHECK(mouse_in_slot(mouse_key_slot));
  }

  SUBCASE("an impossible trailer length is refused before the swap") {
    TestConfig_t config(key_machine());
    TestFixtures::ScopedCore_t core(config);
    TestFixtures::ScopedTempFile_t copy(".aws");
    const size_t slot_1_length = offsetof(Snapshot_t, slot_trailer) +
                                 offsetof(SsSlotTrailer_t, slots) +
                                 offsetof(SsSlotState_t, length);
    patched_copy(legacy_key_fixture(), copy, slot_1_length,
                 {0xFF, 0xFF, 0x00, 0x00});

    save_state_set_filename(copy.c_str());
    ScopedLogCapture_t log;
    CHECK(save_state_load() == false);
    CHECK(log.count_containing("claims 65535 bytes, more than a slot holds") ==
          1);
    CHECK(log.count_containing("for this session") == 0);
    CHECK(mouse_in_slot(mouse_key_slot));
    CHECK(machine_names(mouse_key_slot) == "Mouse Interface");
  }
}

#endif

namespace {

constexpr uint16_t mouse_program = 0x0300;
constexpr uint16_t mouse_indirect_jump = 0x03F0;
constexpr uint16_t mouse_handler = 0x0380;
constexpr uint32_t mouse_cycle_cap = 200000;
constexpr uint64_t mouse_tick_period = 17030;
constexpr uint64_t mouse_legacy_phase = 1023;
// Not $06: SERVEMOUSE borrows that byte as an RTS while it runs (bank 0
// $0C4-$0CC).
constexpr uint8_t mouse_entry_count = 0x0A;
constexpr size_t mouse_frame_size = 92;
constexpr size_t mouse_frame_phase = 32;
constexpr size_t mouse_frame_rate = 64;
constexpr size_t mouse_frame_pending = 65;
constexpr size_t mouse_frame_irq = 66;
constexpr size_t mouse_frame_mode = 74;
constexpr size_t mouse_frame_status = 76;
constexpr size_t mouse_frame_button = 79;

enum MouseEntry_t : uint8_t {
  mouse_entry_set = 0,
  mouse_entry_serve = 1,
  mouse_entry_read = 2,
  mouse_entry_pos = 4,
  mouse_entry_clamp = 5,
};

using MouseFrame_t = std::array<uint8_t, mouse_frame_size>;

// The table at $Cn12 holds the low bytes of the entries (manual p. 49), so
// every call goes through it indirectly: LDA $Cn12+k / STA $07 / LDA #$Cn /
// STA $08 / LDA #a / LDX #$Cn / LDY #$n0 / JSR $03F0, with JMP ($0007) there.
auto emit_mouse_call(std::vector<uint8_t>& program, int slot, int entry,
                     uint8_t a) -> void {
  const auto page = static_cast<uint8_t>(0xC0 + slot);
  const auto table = static_cast<uint16_t>((page << 8) + 0x12 + entry);
  const std::vector<uint8_t> call = {
      0xAD,
      static_cast<uint8_t>(table & 0xFF),
      static_cast<uint8_t>(table >> 8),
      0x85,
      0x07,
      0xA9,
      page,
      0x85,
      0x08,
      0xA9,
      a,
      0xA2,
      page,
      0xA0,
      static_cast<uint8_t>(slot << 4),
      0x20,
      static_cast<uint8_t>(mouse_indirect_jump & 0xFF),
      static_cast<uint8_t>(mouse_indirect_jump >> 8),
  };
  program.insert(program.end(), call.begin(), call.end());
  const std::array<uint8_t, 3> jump = {0x6C, 0x07, 0x00};
  TestFixtures::ScopedCore_t::poke(mouse_indirect_jump, jump);
}

auto call_mouse_firmware(int slot, int entry, uint8_t a) -> bool {
  std::vector<uint8_t> program;
  emit_mouse_call(program, slot, entry, a);
  const auto spin = static_cast<uint16_t>(mouse_program + program.size());
  program.push_back(0x4C);
  program.push_back(static_cast<uint8_t>(spin & 0xFF));
  program.push_back(static_cast<uint8_t>(spin >> 8));
  TestFixtures::ScopedCore_t::poke(mouse_program, program.data(),
                                   program.size());
  TestFixtures::enter_at({mouse_program, 0, 0, 0});
  TestFixtures::step_until_pc(spin, mouse_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == spin);
  return (cpu_get_registers()->ps & 0x01) != 0;
}

auto mouse_frame(int slot) -> MouseFrame_t {
  MouseFrame_t frame{};
  size_t size = frame.size();
  peripheral_save_state(slot, frame.data(), &size);
  REQUIRE(size == frame.size());
  return frame;
}

auto mouse_frame_word(const MouseFrame_t& frame, size_t at) -> uint32_t {
  return static_cast<uint32_t>(frame.at(at)) |
         (static_cast<uint32_t>(frame.at(at + 1)) << 8) |
         (static_cast<uint32_t>(frame.at(at + 2)) << 16) |
         (static_cast<uint32_t>(frame.at(at + 3)) << 24);
}

struct MouseReading_t {
  int16_t x;
  int16_t y;
  uint8_t status;
};

// READMOUSE through the table, then the slot's holes (manual p. 44).
auto read_mouse_holes(int slot) -> MouseReading_t {
  REQUIRE_FALSE(call_mouse_firmware(slot, mouse_entry_read, 0));
  const auto n = static_cast<uint16_t>(slot);
  MouseReading_t reading{};
  reading.x = static_cast<int16_t>(static_cast<uint16_t>(
      mem[0x478 + n] | (static_cast<uint16_t>(mem[0x578 + n]) << 8)));
  reading.y = static_cast<int16_t>(static_cast<uint16_t>(
      mem[0x4F8 + n] | (static_cast<uint16_t>(mem[0x5F8 + n]) << 8)));
  reading.status = mem[0x778 + n];
  return reading;
}

auto poke_mouse_byte(uint16_t at, uint8_t value) -> void {
  const std::array<uint8_t, 1> byte = {value};
  TestFixtures::ScopedCore_t::poke(at, byte);
}

auto press_mouse_button(int slot, bool down) -> void {
  MouseButtonPayload_t payload{0, static_cast<uint8_t>(down ? 1 : 0), {0, 0}};
  REQUIRE(peripheral_command(slot, mouse_cmd_set_button, &payload,
                             sizeof(payload)) == peripheral_ok);
  peripheral_manager_think(0);
}

auto move_mouse_by(int slot, int32_t dx, int32_t dy) -> void {
  MouseMovePayload_t payload{dx, dy};
  REQUIRE(peripheral_command(slot, mouse_cmd_move, &payload, sizeof(payload)) ==
          peripheral_ok);
  peripheral_manager_think(0);
}

// Stepped execution services no card event, so the card is brought up to date
// after every instruction.
auto step_with_think(uint64_t cycles) -> void {
  const uint64_t end = cpu_get_cumulative_cycles() + cycles;
  while (cpu_get_cumulative_cycles() < end) {
    cpu_execute(0);
    peripheral_manager_think(0);
  }
}

// A CLI / JMP * loop; its handler counts entries in $0A and serves the card.
auto enter_mouse_cli_loop(int slot) -> void {
  std::vector<uint8_t> handler = {0xE6, mouse_entry_count};
  emit_mouse_call(handler, slot, mouse_entry_serve, 0);
  handler.push_back(0x40);
  TestFixtures::ScopedCore_t::poke(mouse_handler, handler.data(),
                                   handler.size());
  const std::array<uint8_t, 2> vector = {
      static_cast<uint8_t>(mouse_handler & 0xFF),
      static_cast<uint8_t>(mouse_handler >> 8),
  };
  TestFixtures::ScopedCore_t::poke(IRQ_VECTOR_ADDR, vector);
  poke_mouse_byte(mouse_entry_count, 0);
  const std::array<uint8_t, 4> loop = {0x58, 0x4C, 0x01, 0x03};
  TestFixtures::ScopedCore_t::poke(mouse_program, loop);
  TestFixtures::enter_at({mouse_program, 0, 0, 0});
}

// Runs on until the handler has returned, so the hole it wrote can be read.
auto cycles_until_mouse_entries(uint8_t count, uint64_t cap) -> uint64_t {
  constexpr uint64_t handler_cap = 1000;
  const uint64_t start = cpu_get_cumulative_cycles();
  while (mem[mouse_entry_count] < count &&
         cpu_get_cumulative_cycles() - start < cap) {
    cpu_execute(0);
    peripheral_manager_think(0);
  }
  const uint64_t entered = cpu_get_cumulative_cycles() - start;
  if (mem[mouse_entry_count] < count) {
    return entered;
  }
  const uint64_t handler_end = cpu_get_cumulative_cycles() + handler_cap;
  while ((cpu_get_registers()->pc < mouse_program ||
          cpu_get_registers()->pc > mouse_program + 3) &&
         cpu_get_cumulative_cycles() < handler_end) {
    cpu_execute(0);
    peripheral_manager_think(0);
  }
  return entered;
}

auto mouse_machine_in(int slot) -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots.at(static_cast<size_t>(slot - 1)) = "Mouse Interface";
  return description;
}

// snapshot_deserialize restores the CPU before the slots load, so the card's
// AssertIrq during load_state sticks and a CLI loop afterwards proves it.
auto mouse_survives_the_file(int slot) -> void {
  TestConfig_t config(mouse_machine_in(slot));
  TestFixtures::ScopedCore_t core(config);
  peripheral_manager_init();
  linapple_register_peripherals();
  linapple_reset_hard();

  REQUIRE_FALSE(call_mouse_firmware(slot, mouse_entry_set, 0x0F));
  poke_mouse_byte(0x478, 0x64);
  poke_mouse_byte(0x4F8, 0xC8);
  poke_mouse_byte(0x578, 0x00);
  poke_mouse_byte(0x5F8, 0x00);
  REQUIRE_FALSE(call_mouse_firmware(slot, mouse_entry_clamp, 0));
  press_mouse_button(slot, true);
  // One masked tick reports the button and the refresh and raises the line; a
  // move after it is pending for the next.
  step_with_think(mouse_tick_period);
  move_mouse_by(slot, 1, 0);

  const MouseFrame_t saved = mouse_frame(slot);
  CHECK(saved.at(mouse_frame_mode) == 0x0F);
  CHECK(mouse_frame_word(saved, 16) == 100);
  CHECK(mouse_frame_word(saved, 20) == 200);
  CHECK(mouse_frame_word(saved, 8) == 100);
  CHECK(saved.at(mouse_frame_button) == 1);
  CHECK(saved.at(mouse_frame_pending) == 0x02);
  CHECK(saved.at(mouse_frame_irq) == 1);
  CHECK((saved.at(mouse_frame_status) & 0x0E) == 0x0C);

  TestFixtures::ScopedTempFile_t file(".aws");
  save_state_set_filename(file.c_str());
  save_state_save();

  linapple_reset_hard();
  CHECK(mouse_frame(slot).at(mouse_frame_mode) == 0x00);
  REQUIRE(save_state_load());
  CHECK(mouse_frame(slot) == saved);

  // The line is up, so the handler runs at the CLI with the tick's two sources;
  // the pending move comes with the next tick, within one period of the load.
  const uint64_t loaded_at = cpu_get_cumulative_cycles();
  enter_mouse_cli_loop(slot);
  CHECK(cycles_until_mouse_entries(1, 400) < 400);
  CHECK((mem[0x778 + slot] & 0x0E) == 0x0C);
  const uint64_t second = cycles_until_mouse_entries(2, 2 * mouse_tick_period);
  CHECK(second < mouse_tick_period);
  CHECK(cpu_get_cumulative_cycles() - loaded_at <= mouse_tick_period + 32);
  CHECK((mem[0x778 + slot] & 0x0E) == 0x0A);

  const MouseReading_t reading = read_mouse_holes(slot);
  CHECK(reading.x == 100);
  CHECK(reading.y == 0);
  CHECK(reading.status == 0xA0);
}

}  // namespace

TEST_CASE(
    "Snapshot: an .aws gives the mouse card back its mode, clamps, position, "
    "button, pending sources and asserted line in slot 4 and in slot 5, with "
    "the next tick within one period") {
  SUBCASE("slot 4") { mouse_survives_the_file(4); }
  SUBCASE("slot 5") { mouse_survives_the_file(5); }
}

namespace {

// The machine minimal.aws was written on, with the mouse where its Mockingboard
// was.
auto mouse_minimal_machine() -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[0] = "Parallel Printer";
  description.slots[1] = "Super Serial Card";
  description.slots[3] = "Mouse Interface";
  return description;
}

}  // namespace

// A trailer-less file hands slot 4 its 104-byte fixed region. The shipped
// minimal.aws names the Mockingboard there, so a mouse machine refuses it at
// the manifest, before any region is read.
TEST_CASE(
    "Snapshot: a fixed-body file whose slot-4 region is not a mouse frame "
    "loads with the card at reset and says so once, and minimal.aws is "
    "refused at the manifest") {
  TestConfig_t config(mouse_minimal_machine());
  TestFixtures::ScopedCore_t core(config);
  peripheral_manager_init();
  linapple_register_peripherals();
  linapple_reset_hard();

  REQUIRE_FALSE(call_mouse_firmware(4, mouse_entry_set, 0x01));
  poke_mouse_byte(0x47C, 150);
  poke_mouse_byte(0x57C, 0);
  poke_mouse_byte(0x4FC, 20);
  poke_mouse_byte(0x5FC, 0);
  REQUIRE_FALSE(call_mouse_firmware(4, mouse_entry_pos, 0));
  REQUIRE(read_mouse_holes(4).x == 150);

  TestFixtures::ScopedTempFile_t file(".aws");
  save_state_set_filename(file.c_str());
  save_state_save();
  REQUIRE(truncate(file.c_str(),
                   static_cast<off_t>(snapshot_size_fixed_body)) == 0);
  {
    std::fstream patch(file.path(),
                       std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(patch.good());
    const std::array<char, 4> no_version = {0, 0, 0, 0};
    patch.seekp(
        static_cast<std::streamoff>(offsetof(Snapshot_t, mockingboard1)));
    patch.write(no_version.data(), no_version.size());
    REQUIRE(patch.good());
  }

  {
    ScopedLogCapture_t log;
    REQUIRE(save_state_load());
    CHECK(log.count_containing("Slot 4: Mouse Interface refused the 104-byte "
                               "fixed-body region and stays at reset") == 1);
    CHECK(log.count_containing("Slot 4:") == 1);
  }
  REQUIRE_FALSE(call_mouse_firmware(4, mouse_entry_set, 0x01));
  MouseReading_t reading = read_mouse_holes(4);
  CHECK(reading.x == 0);
  CHECK(reading.y == 0);
  move_mouse_by(4, 2000, 2000);
  reading = read_mouse_holes(4);
  CHECK(reading.x == 1023);
  CHECK(reading.y == 1023);

  const std::string minimal = TestFixtures::get_fixture_path("minimal.aws");
  struct stat on_disk{};
  REQUIRE(stat(minimal.c_str(), &on_disk) == 0);
  CHECK(static_cast<size_t>(on_disk.st_size) == snapshot_size_fixed_body);
  save_state_set_filename(minimal.c_str());
  {
    ScopedLogCapture_t log;
    CHECK(save_state_load() == false);
    CHECK(log.count_containing("Slot 4: the save state names Mockingboard "
                               "where the machine holds Mouse Interface") == 1);
    CHECK(log.count_containing("refused the") == 0);
  }
}

// The two fixtures name the shipped Disk II and Harddisk in slots 6 and 7, so
// a build without both cards refuses them at the manifest.
#if defined(ENABLE_PERIPHERAL_DISK) && defined(ENABLE_PERIPHERAL_HARDDISK)

namespace {

// The machine the two slot-4 fixtures were written on.
auto mouse_fixture_machine() -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[0] = "Parallel Printer";
  description.slots[1] = "Super Serial Card";
  description.slots[3] = "Mouse Interface";
  description.slots[4] = "Mockingboard";
  description.slots[5] = "Disk II";
  description.slots[6] = "Harddisk";
  return description;
}

constexpr int harddisk_slot = 7;
constexpr uint16_t harddisk_io_base = 0xC080 + (harddisk_slot << 4);

auto harddisk_status() -> HarddiskStatus_t {
  HarddiskStatus_t out{};
  size_t size = sizeof(out);
  REQUIRE(peripheral_query(harddisk_slot, harddisk_query_status, &out, &size) ==
          peripheral_ok);
  return out;
}

// The mouse fixtures carry an empty slot-7 trailer entry, so the load falls
// back to the 16-byte fixed-body region, which the card refuses and says so
// once; the card stays at reset with its mounted image where the host put it.
auto load_mouse_fixture(const std::string& name) -> void {
  const std::string path = TestFixtures::get_fixture_path(name);
  REQUIRE(access(path.c_str(), R_OK) == 0);

  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  std::strncpy(insert.path, image.c_str(), sizeof(insert.path) - 1);
  REQUIRE(peripheral_command(harddisk_slot, harddisk_cmd_insert, &insert,
                             sizeof(insert)) == peripheral_ok);
  peripheral_manager_think(0);
  REQUIRE(harddisk_status().drive0_loaded == 1);
  io_map_dispatch(0, harddisk_io_base + 2, 1, 0x05, 0);

  save_state_set_filename(path.c_str());
  ScopedLogCapture_t log;
  REQUIRE(save_state_load());
  CHECK(log.count_containing("Slot 7: Harddisk refused the 16-byte "
                             "fixed-body region and stays at reset") == 1);
  CHECK(log.count_containing("Slot 4:") == 0);

  CHECK(io_map_dispatch(0, harddisk_io_base + 0, 0, 0, 0) == 0);
  CHECK(io_map_dispatch(0, harddisk_io_base + 1, 0, 0, 0) == 0);
  CHECK(io_map_dispatch(0, harddisk_io_base + 2, 0, 0, 0) == 0);
  CHECK(io_map_dispatch(0, harddisk_io_base + 5, 0, 0, 0) == 0);
  const HarddiskStatus_t after = harddisk_status();
  CHECK(after.drive0_loaded == 1);
  CHECK(std::string(after.drive0_full_path) == image.path());
}

// The fixtures hold the frame the card wrote before the tick existed: mode $0B,
// position (123, 456), the button held through one READMOUSE, the host
// window's 1023 x 1023 range where the phase now travels, status $80 with the
// button in bit 7, and zeros where the rate, pending sources and line travel.
auto mouse_fixture_loads(const std::string& name) -> void {
  TestConfig_t config(mouse_fixture_machine());
  TestFixtures::ScopedCore_t core(config);
  peripheral_manager_init();
  linapple_register_peripherals();
  linapple_reset_hard();

  load_mouse_fixture(name);
  const MouseFrame_t loaded = mouse_frame(4);
  CHECK(loaded.at(mouse_frame_mode) == 0x0B);
  CHECK(mouse_frame_word(loaded, mouse_frame_phase) == mouse_legacy_phase);
  CHECK(loaded.at(mouse_frame_rate) == 0);
  CHECK(loaded.at(mouse_frame_pending) == 0);
  CHECK(loaded.at(mouse_frame_irq) == 0);
  CHECK(loaded.at(mouse_frame_status) == 0x00);

  // Before the first tick can come there is nothing to serve.
  CHECK(call_mouse_firmware(4, mouse_entry_serve, 0));
  enter_mouse_cli_loop(4);
  CHECK(cycles_until_mouse_entries(1, 300) >= 300);
  CHECK(mem[mouse_entry_count] == 0);

  MouseReading_t reading = read_mouse_holes(4);
  CHECK(mem[0x47C] == 0x7B);
  CHECK(mem[0x57C] == 0x00);
  CHECK(mem[0x4FC] == 0xC8);
  CHECK(mem[0x5FC] == 0x01);
  CHECK(reading.status == 0xC0);
  move_mouse_by(4, 2000, 2000);
  reading = read_mouse_holes(4);
  CHECK(reading.x == 1023);
  CHECK(reading.y == 1023);

  // The first tick comes within the 1,023 cycles the width reads as, plus the
  // instruction in flight; the frame written then carries the line.
  load_mouse_fixture(name);
  enter_mouse_cli_loop(4);
  const uint64_t first_tick =
      cycles_until_mouse_entries(1, 2 * mouse_tick_period);
  CHECK(first_tick <= mouse_legacy_phase + 24);
  CHECK((mem[0x77C] & 0x0E) == 0x08);

  load_mouse_fixture(name);
  const std::array<uint8_t, 3> spin = {0x4C, 0x00, 0x03};
  TestFixtures::ScopedCore_t::poke(mouse_program, spin);
  TestFixtures::enter_at({mouse_program, 0, 0, 0});
  step_with_think(mouse_legacy_phase + 24);
  const MouseFrame_t ticked = mouse_frame(4);
  CHECK(ticked.at(mouse_frame_irq) == 1);
  CHECK((ticked.at(mouse_frame_status) & 0x0E) == 0x08);
  CHECK(ticked.at(mouse_frame_pending) == 0);
  CHECK(ticked.at(mouse_frame_rate) == 0);
  CHECK(mouse_frame_word(ticked, mouse_frame_phase) <= mouse_tick_period);
}

}  // namespace

TEST_CASE(
    "Snapshot: the slot-4 fixtures from before the tick existed load through "
    "the trailer and through the fixed region, serve nothing, read their "
    "position and button, and tick within 1,023 cycles") {
  SUBCASE("the 134,200-byte file, from the trailer") {
    mouse_fixture_loads("mouse-slot4-0aad3663.aws");
  }
  SUBCASE("the 132,344-byte file, from the fixed region") {
    mouse_fixture_loads("mouse-slot4-0aad3663-fixed.aws");
  }
}

#endif

#endif

// Every card whose frame rides the slot trailer must fit its 256-byte entry,
// or save_slot_to_trailer writes nothing and the card is silently at reset
// on every load.
TEST_CASE("Snapshot: every frame that rides the slot trailer fits its entry") {
#ifdef ENABLE_PERIPHERAL_HARDDISK
  static_assert(sizeof(HarddiskSaveState_t) <= snapshot_slot_state_capacity,
                "the hard disk's frame rides the trailer");
  CHECK(sizeof(HarddiskSaveState_t) == 20);
#endif
#ifdef ENABLE_PERIPHERAL_MOUSE
  static_assert(sizeof(MouseSaveState_t) <= snapshot_slot_state_capacity,
                "the mouse's frame rides the trailer");
#endif
#ifdef ENABLE_PERIPHERAL_CLOCK
  static_assert(sizeof(ClockCardSaveState_t) <= snapshot_slot_state_capacity,
                "the clock's frame rides the trailer");
#endif
#ifdef ENABLE_PERIPHERAL_PRINTER
  static_assert(sizeof(PrinterSaveState_t) <= snapshot_slot_state_capacity,
                "the printer's frame rides the trailer");
#endif
#ifdef ENABLE_PERIPHERAL_SUPER_SERIAL
  static_assert(sizeof(SuperSerialSaveState_t) <= snapshot_slot_state_capacity,
                "the serial card's frame rides the trailer");
#endif
#ifdef ENABLE_PERIPHERAL_MOCKINGBOARD
  static_assert(sizeof(MockingboardSaveState_t) <= snapshot_slot_state_capacity,
                "the Mockingboard's frame rides the trailer");
#endif
  CHECK(snapshot_slot_state_capacity == 256);
}

#ifdef ENABLE_PERIPHERAL_HARDDISK
TEST_CASE(
    "Snapshot: An .aws carries the hard disk's registers in the slot-7 "
    "trailer entry and gives them back, with no warning about its size") {
  TestConfig_t::Description_t description;
  description.slots[6] = "Harddisk";
  TestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  peripheral_manager_init();
  linapple_register_peripherals();
  linapple_reset_hard();
  REQUIRE(peripheral_present(7, "linapple.harddisk"));

  constexpr uint16_t io_base = 0xC080 + (7 << 4);
  io_map_dispatch(0, io_base + 1, 1, 0xF0, 0);
  io_map_dispatch(0, io_base + 2, 1, 0x34, 0);
  io_map_dispatch(0, io_base + 3, 1, 0x12, 0);
  std::array<uint8_t, 20> saved{};
  size_t size = saved.size();
  peripheral_save_state(7, saved.data(), &size);
  REQUIRE(size == saved.size());
  CHECK(saved.at(8) == 0xF0);
  CHECK(saved.at(12) == 0x34);
  CHECK(saved.at(13) == 0x12);

  TestFixtures::ScopedTempFile_t file(".aws");
  save_state_set_filename(file.c_str());
  {
    ScopedLogCapture_t log;
    save_state_save();
    CHECK(log.count_containing("exceeds the") == 0);
  }
  std::ifstream in(file.path(), std::ios::binary | std::ios::ate);
  REQUIRE(in.is_open());
  CHECK(in.tellg() == static_cast<std::streamoff>(134200));
  const std::streamoff slot7_entry = static_cast<std::streamoff>(
      offsetof(Snapshot_t, slot_trailer) + offsetof(SsSlotTrailer_t, slots) +
      (6 * sizeof(SsSlotState_t)));
  in.seekg(slot7_entry);
  std::array<uint8_t, 28> entry{};
  in.read(reinterpret_cast<char*>(entry.data()), entry.size());
  CHECK(entry.at(0) == 20);
  CHECK(std::equal(saved.begin(), saved.end(), entry.begin() + 8));

  linapple_reset_hard();
  std::array<uint8_t, 20> reset{};
  size = reset.size();
  peripheral_save_state(7, reset.data(), &size);
  CHECK(reset.at(8) == 0);
  CHECK(reset.at(12) == 0);

  REQUIRE(save_state_load());
  std::array<uint8_t, 20> loaded{};
  size = loaded.size();
  peripheral_save_state(7, loaded.data(), &size);
  CHECK(loaded == saved);
}
#endif
