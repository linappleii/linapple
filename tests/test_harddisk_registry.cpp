// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;

auto probe_no(const uint8_t* /*unused*/, size_t /*unused*/, uint64_t /*unused*/,
              const char* /*unused*/) -> HarddiskProbe_e {
  return harddisk_probe_no;
}

auto fake_open(const char* /*unused*/, uint32_t /*unused*/, bool /*unused*/,
               void** out_instance) -> HarddiskError_e {
  static char fake_instance = 0;
  *out_instance = &fake_instance;
  return harddisk_err_none;
}

auto fake_close(void* /*unused*/) -> void {}

auto fake_is_write_protected(void* /*unused*/) -> bool { return true; }

auto fake_read_block(void* /*unused*/, uint32_t /*unused*/, uint8_t* /*unused*/)
    -> HarddiskError_e {
  return harddisk_err_io;
}

auto fake_write_block(void* /*unused*/, uint32_t /*unused*/,
                      const uint8_t* /*unused*/) -> HarddiskError_e {
  return harddisk_err_io;
}

auto fake_get_total_blocks(void* /*unused*/) -> uint32_t { return 0; }

auto make_fake(const char* name) -> HarddiskFormatDriver_t {
  HarddiskFormatDriver_t driver{};
  driver.abi_version = harddisk_format_abi_version;
  driver.name = name;
  driver.probe = probe_no;
  driver.open = fake_open;
  driver.close = fake_close;
  driver.is_write_protected = fake_is_write_protected;
  driver.read_block = fake_read_block;
  driver.get_total_blocks = fake_get_total_blocks;
  return driver;
}

auto drain_names() -> std::vector<std::string> {
  std::vector<std::string> names;
  harddisk_loader_drain_rejections(
      [](void* context, const char* driver_name, const char*) {
        static_cast<std::vector<std::string>*>(context)->emplace_back(
            driver_name);
      },
      &names);
  return names;
}

auto registered_names() -> std::vector<std::string> {
  std::vector<std::string> names;
  names.reserve(harddisk_loader_driver_count());
  for (uint32_t i = 0; i < harddisk_loader_driver_count(); ++i) {
    names.emplace_back(harddisk_loader_driver_at(i)->name);
  }
  return names;
}

}  // namespace

TEST_CASE(
    "Harddisk registry: the id resolves and a Slot 7 line places the card") {
  Peripheral_t* descriptor = peripheral_find_internal(harddisk_id);
  REQUIRE(descriptor != nullptr);
  CHECK(std::string(descriptor->id) == harddisk_id);

  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Harddisk";
  TestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_present(card_slot, harddisk_id));
  SS_PERIPHERAL_MANIFEST manifest;
  peripheral_get_manifest(&manifest);
  CHECK(std::string(manifest.peripherals[card_slot].name) == "Harddisk");
}

TEST_CASE(
    "Harddisk registry: every driver source registers itself, by name, with "
    "the nibble refusal among them") {
  harddisk_loader_reset();
  CHECK(harddisk_loader_driver_count() == HARDDISK_FORMAT_DRIVER_COUNT);
  const std::vector<std::string> expected = {
      "2MG",
      "DOS Order",
      "Nibble image",
      "ProDOS Order",
  };
  CHECK(registered_names() == expected);
  CHECK(harddisk_loader_driver_at(harddisk_loader_driver_count()) == nullptr);
  CHECK(drain_names().empty());
}

TEST_CASE(
    "Harddisk registry: the nibble refusal is a whole driver, every entry "
    "point present with its write bit, so the usable-check that admits the "
    "others admits it") {
  harddisk_loader_reset();
  const HarddiskFormatDriver_t* nibble = nullptr;
  for (uint32_t i = 0; i < harddisk_loader_driver_count(); ++i) {
    const HarddiskFormatDriver_t* driver = harddisk_loader_driver_at(i);
    if (std::string(driver->name) == "Nibble image") {
      nibble = driver;
    }
  }
  REQUIRE(nibble != nullptr);
  CHECK(nibble->abi_version == harddisk_format_abi_version);
  CHECK((nibble->capabilities & harddisk_driver_cap_write) != 0);
  CHECK(nibble->supported_exts != nullptr);
  CHECK(nibble->probe != nullptr);
  CHECK(nibble->open != nullptr);
  CHECK(nibble->close != nullptr);
  CHECK(nibble->is_write_protected != nullptr);
  CHECK(nibble->read_block != nullptr);
  CHECK(nibble->write_block != nullptr);
  CHECK(nibble->get_total_blocks != nullptr);

  const std::vector<std::string> expected = {"nib", "nb2", "woz"};
  std::vector<std::string> listed;
  for (const char* const* ext = nibble->supported_exts;
       ext != nullptr && *ext != nullptr; ++ext) {
    listed.emplace_back(*ext);
  }
  CHECK(listed == expected);

  // The name alone claims an image: open is the one entry point an image
  // reaches, and it refuses by the one code that says why.
  char sentinel = 0;
  void* instance = &sentinel;
  CHECK(nibble->open(TestFixtures::get_fixture_path("minimal.nib").c_str(), 0,
                     false, &instance) == harddisk_err_not_block_image);
  CHECK(instance == nullptr);
  CHECK(nibble->is_write_protected(nullptr));
  CHECK(nibble->get_total_blocks(nullptr) == 0);
}

TEST_CASE("Harddisk registry: the same driver registers once") {
  harddisk_loader_reset();
  const uint32_t baseline = harddisk_loader_driver_count();

  HarddiskFormatDriver_t fake = make_fake("Fake Once");
  harddisk_loader_register(&fake);
  harddisk_loader_register(&fake);
  harddisk_loader_register(&fake);

  CHECK(harddisk_loader_driver_count() == baseline + 1);
  CHECK(drain_names().empty());
  harddisk_loader_reset();
  CHECK(harddisk_loader_driver_count() == baseline);
}

TEST_CASE(
    "Harddisk registry: a driver with a foreign ABI, a missing entry point, "
    "a disagreeing write bit or a taken name is refused and named") {
  harddisk_loader_reset();
  const uint32_t baseline = harddisk_loader_driver_count();

  HarddiskFormatDriver_t future = make_fake("Fake Future");
  future.abi_version = harddisk_format_abi_version + 1;
  harddisk_loader_register(&future);

  HarddiskFormatDriver_t no_read = make_fake("Fake Unreadable");
  no_read.read_block = nullptr;
  harddisk_loader_register(&no_read);

  HarddiskFormatDriver_t no_count = make_fake("Fake Uncounted");
  no_count.get_total_blocks = nullptr;
  harddisk_loader_register(&no_count);

  HarddiskFormatDriver_t write_bit_only = make_fake("Fake Write Bit");
  write_bit_only.capabilities = harddisk_driver_cap_write;
  harddisk_loader_register(&write_bit_only);

  HarddiskFormatDriver_t write_fn_only = make_fake("Fake Write Fn");
  write_fn_only.write_block = fake_write_block;
  harddisk_loader_register(&write_fn_only);

  HarddiskFormatDriver_t taken = make_fake("DOS Order");
  harddisk_loader_register(&taken);

  CHECK(harddisk_loader_driver_count() == baseline);
  const std::vector<std::string> expected = {
      "Fake Future",    "Fake Unreadable", "Fake Uncounted",
      "Fake Write Bit", "Fake Write Fn",   "DOS Order",
  };
  CHECK(drain_names() == expected);
  CHECK(drain_names().empty());

  harddisk_loader_reset();
}

TEST_CASE(
    "Harddisk registry: a reset forgets what a test registered and every "
    "pending note") {
  harddisk_loader_reset();
  const uint32_t baseline = harddisk_loader_driver_count();

  HarddiskFormatDriver_t fake = make_fake("Fake Transient");
  harddisk_loader_register(&fake);
  CHECK(harddisk_loader_driver_count() == baseline + 1);
  harddisk_loader_note("Fake Transient", "a note nobody drained");

  harddisk_loader_reset();
  CHECK(harddisk_loader_driver_count() == baseline);
  CHECK(drain_names().empty());
  const std::vector<std::string> expected = {
      "2MG",
      "DOS Order",
      "Nibble image",
      "ProDOS Order",
  };
  CHECK(registered_names() == expected);
}

TEST_CASE("Harddisk registry: the extension list reports the length it needs") {
  harddisk_loader_reset();
  const char* const expected =
      "2mg;2img;2meg;do;dsk;nib;nb2;woz;po;hdv;img;gz;zip";
  const size_t needed = strlen(expected);

  CHECK(harddisk_loader_get_supported_extensions(nullptr, 0) == needed);

  char full[64] = {};
  CHECK(harddisk_loader_get_supported_extensions(full, sizeof(full)) == needed);
  CHECK(std::string(full) == expected);

  // Truncation keeps the terminator and still reports the whole length, so a
  // caller can size a second buffer from the first answer.
  char tiny[5] = {};
  CHECK(harddisk_loader_get_supported_extensions(tiny, sizeof(tiny)) == needed);
  CHECK(std::string(tiny) == "2mg;");

  char untouched[8] = "keep";
  CHECK(harddisk_loader_get_supported_extensions(untouched, 0) == needed);
  CHECK(std::string(untouched) == "keep");
}
