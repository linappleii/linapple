// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include "Apple2Types.h"
#include "Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>

#include "apple2/peripherals/Peripheral.h"
#include "doctest.h"
#include "test_fixtures.h"

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

  auto snapshot = std::unique_ptr<ApplewinSnapshot_t>(new ApplewinSnapshot_t());
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
  CHECK(static_cast<size_t>(written.st_size) == sizeof(ApplewinSnapshot_t));
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

  auto snapshot = std::unique_ptr<ApplewinSnapshot_t>(new ApplewinSnapshot_t());
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

  auto snapshot = std::unique_ptr<ApplewinSnapshot_t>(new ApplewinSnapshot_t());
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
  // Verify snapshot handles arbitrary slot 0 peripheral registration order.
  const std::string slot0 = manifest.peripherals[0].name;
  CHECK((slot0 == "Speaker" || slot0 == "Keyboard"));
  CHECK(std::string(manifest.peripherals[1].name) == "Parallel Printer");
  CHECK(std::string(manifest.peripherals[2].name) == "Super Serial Card");
  CHECK(manifest.peripherals[3].name[0] == '\0');
  CHECK(std::string(manifest.peripherals[4].name) == "Mockingboard");
  CHECK(manifest.peripherals[5].name[0] == '\0');
  CHECK(manifest.peripherals[6].name[0] == '\0');
  CHECK(manifest.peripherals[7].name[0] == '\0');
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
  CHECK(static_cast<size_t>(written.st_size) == sizeof(ApplewinSnapshot_t));
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
                     static_cast<off_t>(sizeof(ApplewinSnapshot_t) - 1)) == 0);
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
