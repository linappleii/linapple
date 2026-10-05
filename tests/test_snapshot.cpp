// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Snapshot.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
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

std::array<FakeCard_t*, NUM_SLOTS> g_fake_cards{};

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

Peripheral_t g_fake_card = {LINAPPLE_ABI_VERSION,
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
                            nullptr};

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
  // The manifest names the front device of each slot; slot 0 holds three
  // internal devices and the registry puts the highest id in front, which
  // keeps the speaker there as older readers expect. In a plugin build the
  // speaker is a module registered after every built-in.
#if defined(ENABLE_PERIPHERAL_SPEAKER)
  CHECK(std::string(manifest.peripherals[0].name) == "Speaker");
#else
  CHECK(std::string(manifest.peripherals[0].name) == "Keyboard");
#endif
  CHECK(std::string(manifest.peripherals[1].name) == "Parallel Printer");
  CHECK(std::string(manifest.peripherals[2].name) == "Super Serial Card");
  CHECK(manifest.peripherals[3].name[0] == '\0');
  CHECK(std::string(manifest.peripherals[4].name) == "Mockingboard");
  CHECK(manifest.peripherals[5].name[0] == '\0');
  CHECK(manifest.peripherals[6].name[0] == '\0');
  CHECK(manifest.peripherals[7].name[0] == '\0');
}

namespace {

struct LogLines_t {
  std::vector<std::string> lines;
};

auto collect_log_line(LogLevel_t level, const char* message, void* user_data)
    -> void {
  (void)level;
  auto* lines = static_cast<LogLines_t*>(user_data);
  if (lines != nullptr && message != nullptr) {
    lines->lines.emplace_back(message);
  }
}

class ScopedLogCapture_t {
 public:
  ScopedLogCapture_t() : verbosity_(Logger::get_verbosity()) {
    Logger::set_verbosity(LogLevel_t::info);
    Logger::set_callback_with_context(collect_log_line, &lines_);
  }
  ~ScopedLogCapture_t() {
    Logger::set_callback_with_context(nullptr, nullptr);
    Logger::set_verbosity(verbosity_);
  }
  ScopedLogCapture_t(const ScopedLogCapture_t&) = delete;
  auto operator=(const ScopedLogCapture_t&) -> ScopedLogCapture_t& = delete;
  ScopedLogCapture_t(ScopedLogCapture_t&&) = delete;
  auto operator=(ScopedLogCapture_t&&) -> ScopedLogCapture_t& = delete;

  auto lines() const -> const std::vector<std::string>& { return lines_.lines; }
  auto count_containing(const std::string& needle) const -> size_t {
    size_t count = 0;
    for (const std::string& line : lines_.lines) {
      if (line.find(needle) != std::string::npos) {
        ++count;
      }
    }
    return count;
  }

 private:
  LogLevel_t verbosity_;
  LogLines_t lines_;
};

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

  for (const char* device : {"Speaker", "Keyboard", "Joystick"}) {
    snprintf(manifest.peripherals[0].name, sizeof(manifest.peripherals[0].name),
             "%s", device);
    CHECK_MESSAGE(peripheral_verify_manifest(&manifest), device);
  }

  snprintf(manifest.peripherals[0].name, sizeof(manifest.peripherals[0].name),
           "%s", "Mockingboard");
  CHECK(peripheral_verify_manifest(&manifest) == false);
}

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
constexpr uint8_t bit7 = 0x80;

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

TEST_CASE("Snapshot: A loaded file starts with every paddle timer expired") {
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
  const std::array<uint8_t, 7> main_loop = {0x58,            // CLI
                                            0xAD, 0x81, hi,  // LDA $C0n1
                                            0x4C, 0x01, 0x03};
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

#if defined(ENABLE_PERIPHERAL_MOUSE)

// The key's fixture was written with the shipped Disk II and Harddisk in
// slots 6 and 7; a build without both cards refuses it at the manifest before
// the key's slot is ever walked.
#if defined(ENABLE_PERIPHERAL_DISK) && defined(ENABLE_PERIPHERAL_HARDDISK)

namespace {

constexpr int mouse_key_slot = 4;
constexpr const char* mouse_card_id = "linapple.mouse";

// The shipped [Slots] with the key set: the machine a user of the key has had
// since the key stopped being read, Mockingboard and all.
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
         static_cast<size_t>(slot) * sizeof(SsPeripheralInfo_t);
}

auto file_names(const std::string& path, int slot) -> std::string {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  std::array<char, max_peripheral_name> name{};
  in.seekg(static_cast<std::streamoff>(manifest_name_offset(slot)));
  in.read(name.data(), static_cast<std::streamsize>(name.size()));
  REQUIRE(in.good());
  name.back() = '\0';
  return std::string(name.data());
}

// A copy of the fixture with one run of bytes replaced, written to the temp
// file so the fixture itself is never touched.
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

// What the legacy file does to a machine the key built: the Mockingboard
// comes back for the session, and a save afterwards says so.
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

#endif
