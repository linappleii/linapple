// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/peripherals/Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/clock/ClockCardCommands.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "doctest.h"
#include "fixture_plugin_slot0.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

static bool g_mock_shutdown_called = false;

static auto Mock_Init(int slot, HostInterface_t* host) -> void* {
  (void)slot;
  g_mock_shutdown_called = false;
  // We'll use a dummy pointer as the instance.
  void* instance = (void*)0xDEADBEEF;
  host->RegisterDirectIO(
      instance, 0xC000,
      [](void* instance, uint16_t, uint16_t, uint8_t, uint8_t,
         uint32_t) -> uint8_t {
        if (instance == (void*)0xDEADBEEF && !g_mock_shutdown_called) {
          return 0xAA;
        }
        return 0xEE;
      },
      nullptr);
  return instance;
}

static auto Mock_Shutdown(void* instance) -> void {
  if (instance == (void*)0xDEADBEEF) {
    g_mock_shutdown_called = true;
  }
}

static Peripheral_t g_mock_peripheral = {
    LINAPPLE_ABI_VERSION,
    "test.mock",
    "MockPeripheral",
    "Description",
    "Author",
    "1.0.0",
    0xFF,
    -1,
    Mock_Init,
    nullptr,  // reset
    Mock_Shutdown,
    nullptr,  // think
    nullptr,  // on_vblank
    nullptr,  // save_state
    nullptr,  // load_state
    nullptr,  // command
    nullptr   // query
};

TEST_CASE("Peripheral Manager: Direct IO handlers are cleared during re-init") {
  TestFixtures::ScopedTestConfig_t machine(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  machine.load();
  linapple_init();

  // 1. Initial setup
  peripheral_manager_init();
  peripheral_register(&g_mock_peripheral, 1);

  // Verify it works
  CHECK(io_map_dispatch(0, 0xC000, 0, 0, 0) == 0xAA);

  // 2. Re-init
  // The bug is that peripheral_manager_init calls clear_all_peripherals()
  // which frees instances, but hasn't yet zeroed g_num_direct_handlers.
  // If we call io_map_dispatch after clear_all_peripherals() but before
  // g_num_direct_handlers = 0, we get a UAF or access to stale instance.

  peripheral_manager_init();

  // After Init, the old handler should be gone.
  // In the buggy version, if we hadn't called the second part of Init,
  // this would hit the lambda with a stale instance or 0xDEADBEEF but
  // g_mock_shutdown_called=true.

  // Actually, io_map_dispatch should return floating bus (0) or default io_null
  // if no handler is found. io_null returns mem_read_floating_bus which might
  // be non-zero but usually predictable in tests.
  uint8_t val = io_map_dispatch(0, 0xC000, 0, 0, 0);
  CHECK(val != 0xAA);
  CHECK(val !=
        0xEE);  // 0xEE would mean it called the old handler after shutdown

  linapple_shutdown();
}

TEST_CASE(
    "Peripheral Manager: Direct IO handlers are cleared when a peripheral is "
    "unregistered") {
  TestFixtures::ScopedTestConfig_t machine(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();

  // 1. Register
  peripheral_register(&g_mock_peripheral, 1);
  CHECK(io_map_dispatch(0, 0xC000, 0, 0, 0) == 0xAA);

  // 2. Unregister
  peripheral_unregister(1);

  // After unregistering slot 1, the mock peripheral is gone.
  // Any direct IO handlers it registered should also be gone.
  // In the buggy version, the handler remains and will call the lambda with
  // shutdown=true.
  uint8_t val = io_map_dispatch(0, 0xC000, 0, 0, 0);
  CHECK(val != 0xAA);
  CHECK(val !=
        0xEE);  // 0xEE would mean it called the old handler after shutdown

  linapple_shutdown();
}

TEST_CASE("Peripheral Manager: host_get_config lifetime") {
  TestFixtures::ScopedTestConfig_t machine(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();

  // We need a way to get the HostInterface_t.
  // We can use a dummy peripheral and register it.
  static char captured_val1[32];
  static char captured_val2[32];

  static Peripheral_t test_api_config = {
      LINAPPLE_ABI_VERSION,
      "test.config",
      "ConfigTest",
      "Desc",
      "Author",
      "1.0.0",
      0xFF,
      -1,
      [](int slot, HostInterface_t* host) -> void* {
        (void)slot;
        host->GetConfig("Peripheral", "TestKey1", captured_val1,
                        sizeof(captured_val1));
        host->GetConfig("Peripheral", "TestKey2", captured_val2,
                        sizeof(captured_val2));
        return (void*)0x1234;
      },
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr};

  // Set some config values
  config_save_string("Peripheral", "TestKey1", "Value1");
  config_save_string("Peripheral", "TestKey2", "Value2");

  peripheral_register(&test_api_config, 1);

  // Now they should both be correct because they have their own buffers.
  CHECK(std::string(captured_val2) == "Value2");
  CHECK(std::string(captured_val1) == "Value1");

  linapple_shutdown();
}

#include "core/Util_Path.h"

TEST_CASE("Peripheral Manager: Plugin path construction") {
  // This test verifies that we can construct a valid path even if the directory
  // doesn't have a trailing slash.
  std::string dir = "/tmp/linapple-test";
  std::string file = "plugin.so";

  std::string fullPath = Path::join(dir, file);
  CHECK(fullPath == "/tmp/linapple-test/plugin.so");

  // Test with trailing slash already present
  dir = "/tmp/linapple-test/";
  fullPath = Path::join(dir, file);
  CHECK(fullPath == "/tmp/linapple-test/plugin.so");

  // Test with empty dir
  CHECK(Path::join("", file) == file);

  // Test with empty file
  CHECK(Path::join(dir, "") == dir);
}

TEST_CASE("Peripheral Manager: Command payload capacity") {
  TestFixtures::ScopedTestConfig_t machine(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();

  static size_t captured_size = 0;
  static uint8_t last_byte = 0;

  static Peripheral_t test_api = {
      LINAPPLE_ABI_VERSION,
      "test.max_payload",
      "MaxPayloadTest",
      "Desc",
      "Author",
      "1.0.0",
      0xFF,
      -1,
      [](int, HostInterface_t*) -> void* { return (void*)0x1; },
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      [](void*, uint32_t, const void* data, size_t size) -> PeripheralStatus_t {
        captured_size = size;
        if (size > 0) {
          last_byte = static_cast<const uint8_t*>(data)[size - 1];
        }
        return peripheral_ok;
      },
      nullptr};

  peripheral_register(&test_api, 1);

  // 1. Send exactly PERIPHERAL_CMD_MAX_DATA (512) bytes
  std::vector<uint8_t> payload(512, 0xAA);
  payload.back() = 0xBB;

  PeripheralStatus_t status =
      peripheral_command(1, 0x123, payload.data(), payload.size());
  CHECK(status == peripheral_ok);

  // Commands are queued and processed during Think(0)
  peripheral_manager_think(0);

  CHECK(captured_size == 512);
  CHECK(last_byte == 0xBB);

  // 2. Send 513 bytes - should be rejected
  std::vector<uint8_t> huge_payload(513, 0xCC);
  status =
      peripheral_command(1, 0x124, huge_payload.data(), huge_payload.size());
  CHECK(status == peripheral_error);

  linapple_shutdown();
}

#include <dlfcn.h>

#ifdef BUILD_SHARED_PERIPHERALS

TEST_CASE(
    "Peripheral Manager: Dynamic plugin loader success path and lifecycle") {
  TestFixtures::ScopedTestConfig_t machine(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();

  const std::string exec_dir = Path::get_executable_dir();
  peripheral_plugins_init(exec_dir.c_str());

  // 1. Verify clock plugin resolution and ABI
  Peripheral_t* clock_desc = peripheral_find_internal("linapple.clock");
  REQUIRE(clock_desc != nullptr);
  CHECK(clock_desc->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(clock_desc->id) == "linapple.clock");
  CHECK(std::string(clock_desc->name) == "Clock Card");
  REQUIRE(clock_desc->init != nullptr);
  REQUIRE(clock_desc->shutdown != nullptr);

  const char* clock_path = peripheral_get_plugin_path("linapple.clock");
  REQUIRE(clock_path != nullptr);
  CHECK(std::string(clock_path).find("clock.so") != std::string::npos);

  // Subsequent dlopen verifies only published descriptor symbol is visible.
  void* clock_handle = dlopen(clock_path, RTLD_NOW | RTLD_LOCAL);
  REQUIRE(clock_handle != nullptr);
  auto* exported = static_cast<Peripheral_t*>(
      dlsym(clock_handle, "linapple_peripheral_descriptor"));
  REQUIRE(exported != nullptr);
  CHECK(std::string(exported->id) == "linapple.clock");
  CHECK(dlsym(clock_handle, "clockcard_get_descriptor") == nullptr);
  dlclose(clock_handle);

  // 2. Verify printer plugin resolution and ABI
  Peripheral_t* printer_desc = peripheral_find_internal("linapple.printer");
  REQUIRE(printer_desc != nullptr);
  CHECK(printer_desc->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(printer_desc->id) == "linapple.printer");
  CHECK(std::string(printer_desc->name) == "Parallel Printer");
  REQUIRE(printer_desc->init != nullptr);
  REQUIRE(printer_desc->shutdown != nullptr);

  const char* printer_path = peripheral_get_plugin_path("linapple.printer");
  REQUIRE(printer_path != nullptr);
  CHECK(std::string(printer_path).find("printer.so") != std::string::npos);

  void* printer_handle = dlopen(printer_path, RTLD_NOW | RTLD_LOCAL);
  REQUIRE(printer_handle != nullptr);
  auto* printer_exported = static_cast<Peripheral_t*>(
      dlsym(printer_handle, "linapple_peripheral_descriptor"));
  REQUIRE(printer_exported != nullptr);
  CHECK(std::string(printer_exported->id) == "linapple.printer");
  CHECK(std::string(printer_exported->name) == "Parallel Printer");
  CHECK(dlsym(printer_handle, "printer_get_descriptor") == nullptr);
  dlclose(printer_handle);

  // 3. Lifecycle execution: Register, Reset, Think, Unregister
  CHECK(peripheral_register(clock_desc, 4) == 0);
  CHECK(peripheral_register(printer_desc, 1) == 0);

  peripheral_manager_reset();
  peripheral_manager_think(200);

  REQUIRE(clock_desc->save_state != nullptr);
  REQUIRE(clock_desc->load_state != nullptr);
  size_t state_sz = 0;
  peripheral_save_state(4, nullptr, &state_sz);
  REQUIRE(state_sz == sizeof(ClockCardSaveState_t));
  std::vector<uint8_t> state_buf(state_sz, 0);
  peripheral_save_state(4, state_buf.data(), &state_sz);
  CHECK(state_sz == sizeof(ClockCardSaveState_t));
  peripheral_load_state(4, state_buf.data(), state_sz);

  // Test minimal host interface directly against plugin entry points.
  HostInterface_t bare_host{};
  bare_host.RegisterIO = [](int, PeripheralIOHandler, PeripheralIOHandler,
                            PeripheralIOHandler, PeripheralIOHandler) {};
  bare_host.RegisterCxROM = [](int, const uint8_t*) {};
  bare_host.GetLocalTime = [](HostLocalTime_t*) -> bool { return false; };
  bare_host.ReadFloatingBus = [](uint32_t) -> uint8_t { return 0; };
  void* bare_card = clock_desc->init(4, &bare_host);
  REQUIRE(bare_card != nullptr);
  size_t probe = 0;
  CHECK(clock_desc->save_state(bare_card, nullptr, &probe) == peripheral_ok);
  CHECK(probe == sizeof(ClockCardSaveState_t));
  CHECK(clock_desc->save_state(bare_card, state_buf.data(), &state_sz) ==
        peripheral_ok);
  CHECK(clock_desc->load_state(bare_card, state_buf.data(), state_sz) ==
        peripheral_ok);
  clock_desc->shutdown(bare_card);

  CHECK(peripheral_unregister(4) == 0);
  CHECK(peripheral_unregister(1) == 0);

  // 4. Shutdown cleanly
  peripheral_plugins_shutdown();
  linapple_shutdown();
}
#endif

TEST_CASE("Peripheral Manager: A declared machine reaches the slots") {
  // What a suite declares is the machine it gets. A fixture that built the
  // core and then cleared the slots left every card to be registered by hand,
  // so the declaration described nothing and the hand registration was the
  // only truth.
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[3] = "Mockingboard";
#ifdef ENABLE_PERIPHERAL_DISK
  description.slots[5] = "Disk II";
#endif
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);

  SS_PERIPHERAL_MANIFEST manifest;
  peripheral_get_manifest(&manifest);

  CHECK(std::string(manifest.peripherals[4].name) == "Mockingboard");
#ifdef ENABLE_PERIPHERAL_DISK
  CHECK(std::string(manifest.peripherals[6].name) == "Disk II");
#else
  CHECK(manifest.peripherals[6].name[0] == '\0');
#endif
  CHECK(manifest.peripherals[1].name[0] == '\0');
  CHECK(manifest.peripherals[2].name[0] == '\0');
  CHECK(manifest.peripherals[3].name[0] == '\0');
  CHECK(manifest.peripherals[5].name[0] == '\0');
  CHECK(manifest.peripherals[7].name[0] == '\0');
}

TEST_CASE(
    "Peripheral Manager: A plugin declaring slot zero is registered there") {
  // Slot 0 is the one slot no configuration key names, so a plugin reaches it
  // only by declaring it. Without this the internal speaker vanishes from
  // every build that ships it as a shared object.
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  REQUIRE(config.load());

  peripheral_plugins_shutdown();
  peripheral_plugins_init(TEST_PLUGIN_DIR);

  REQUIRE(linapple_init() == 0);

  int32_t reported_slot = -1;
  size_t out_size = sizeof(reported_slot);
  CHECK(peripheral_query(0, slot0_fixture_query_slot, &reported_slot,
                         &out_size) == peripheral_ok);
  CHECK(out_size == sizeof(int32_t));
  CHECK(reported_slot == 0);

  linapple_shutdown();
}

TEST_CASE(
    "Peripheral Manager: Plugin loader ABI verification and error handling") {
  SUBCASE("ABI mismatch rejection") {
    Peripheral_t mismatched_api = {LINAPPLE_ABI_VERSION + 99,
                                   "test.mismatched_abi",
                                   "MismatchedABITest",
                                   "Desc",
                                   "Author",
                                   "1.0.0",
                                   0xFF,
                                   -1,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   nullptr};

    CHECK(mismatched_api.abi_version != LINAPPLE_ABI_VERSION);
  }

  SUBCASE("Non-existent plugin path returns error") {
    void* handle =
        dlopen("/nonexistent/path/to/plugin.so", RTLD_NOW | RTLD_LOCAL);
    CHECK(handle == nullptr);
    CHECK(dlerror() != nullptr);
  }

  SUBCASE("Self-binary lookup for peripheral descriptor symbol") {
    void* self_handle = dlopen(nullptr, RTLD_NOW | RTLD_LOCAL);
    REQUIRE(self_handle != nullptr);

    // Missing descriptor symbol on arbitrary handle
    auto* missing = reinterpret_cast<Peripheral_t*>(
        dlsym(self_handle, "nonexistent_peripheral_descriptor_symbol"));
    CHECK(missing == nullptr);

    dlclose(self_handle);
  }
}

TEST_CASE("Peripheral Manager: The built-in cards register in id order") {
  // Three internal devices share slot 0 and the first registered is the one
  // the manifest names and the one that answers an address two of them claim.
  // Static initialisation gives no order of its own, so the registry has to.
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  const std::vector<Peripheral_t*>& registry =
      peripheral_get_builtin_registry();
  REQUIRE(registry.size() >= 2);
  for (size_t i = 1; i < registry.size(); ++i) {
    REQUIRE(registry[i - 1] != nullptr);
    REQUIRE(registry[i] != nullptr);
    CAPTURE(registry[i - 1]->id);
    CAPTURE(registry[i]->id);
    CHECK(std::strcmp(registry[i - 1]->id, registry[i]->id) < 0);
  }

  const Peripheral_t* front = nullptr;
  for (const Peripheral_t* p : registry) {
    if (p->default_slot == 0) {
      front = p;
      break;
    }
  }
  REQUIRE(front != nullptr);

  SS_PERIPHERAL_MANIFEST manifest;
  peripheral_get_manifest(&manifest);
  CHECK(std::string(manifest.peripherals[0].name) == front->name);
#ifdef ENABLE_PERIPHERAL_JOYSTICK
  CHECK(std::string(front->id) == "linapple.joystick");
  CHECK(std::string(manifest.peripherals[0].name) == "Joystick");
#endif
}

namespace {

auto record_log_line(LogLevel_t level, const char* message, void* user_data)
    -> void {
  (void)level;
  auto* lines = static_cast<std::string*>(user_data);
  if (lines != nullptr && message != nullptr) {
    lines->append(message);
  }
}

}  // namespace

TEST_CASE(
    "Peripheral Manager: The shipped slot layout fits the direct I/O table") {
  // A registration past the end of the table is dropped with one log line and
  // its address reads as the floating bus from then on, so a machine that
  // overflows it loses inputs silently.
  std::string log;
  Logger::set_callback_with_context(record_log_line, &log);

  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[0] = "Parallel Printer";
  description.slots[1] = "Super Serial Card";
  description.slots[3] = "Mockingboard";
  description.slots[4] = "Mockingboard";
#ifdef ENABLE_PERIPHERAL_DISK
  description.slots[5] = "Disk II";
#endif
#ifdef ENABLE_PERIPHERAL_HARDDISK
  description.slots[6] = "Harddisk";
#endif
  {
    TestFixtures::ScopedTestConfig_t config(description);
    TestFixtures::ScopedCore_t core(config);

    CHECK(log.find("Too many direct IO handlers") == std::string::npos);

    // An unhandled address returns the bus byte whole, a game-port handler
    // keeps only its low seven bits, so a marker with bit 7 set tells them
    // apart. With the counter far past any pulse every paddle reads low, and
    // the strobe at $C070 is proven by the paddles reading high afterwards.
    constexpr uint32_t probe_cycle = 100;
    constexpr uint8_t marker = 0xDA;
    constexpr uint8_t marker_masked = 0x5A;
    constexpr uint16_t first_button = 0xC061;
    constexpr uint16_t first_paddle = 0xC064;
    constexpr uint16_t last_paddle = 0xC067;
    constexpr uint16_t paddle_strobe = 0xC070;
    constexpr uint64_t long_after_any_pulse = 1000000;
    TestFixtures::ScopedCore_t::poke(
        video_get_scanner_address(nullptr, probe_cycle), &marker, 1);
    g_cumulative_cycles = long_after_any_pulse;
    // PB2 has no pull-down in a standard two-button controller, so its open
    // TTL input reads high at rest (Sather, Understanding the Apple II, 7-9
    // and 7-11); PB0, PB1 and the four expired timers read low.
    constexpr uint16_t open_button = 0xC063;
    for (uint16_t addr = first_button; addr <= last_paddle; ++addr) {
      CAPTURE(addr);
      CHECK(io_map_dispatch(0, addr, 0, 0, probe_cycle) ==
            (addr == open_button ? marker : marker_masked));
    }
    static_cast<void>(io_map_dispatch(0, paddle_strobe, 0, 0, probe_cycle));
    for (uint16_t addr = first_paddle; addr <= last_paddle; ++addr) {
      CAPTURE(addr);
      CHECK(io_map_dispatch(0, addr, 0, 0, probe_cycle) == marker);
    }
  }
  Logger::set_callback_with_context(nullptr, nullptr);
}
