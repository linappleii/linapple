// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <vector>

#include "apple2/peripherals/Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/Apple2Types.h"
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
#ifdef ENABLE_PERIPHERAL_MOCKINGBOARD
  description.slots[3] = "Mockingboard";
#endif
#ifdef ENABLE_PERIPHERAL_DISK
  description.slots[5] = "Disk II";
#endif
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);

  SS_PERIPHERAL_MANIFEST manifest;
  peripheral_get_manifest(&manifest);

#ifdef ENABLE_PERIPHERAL_MOCKINGBOARD
  CHECK(std::string(manifest.peripherals[4].name) == "Mockingboard");
#else
  CHECK(manifest.peripherals[4].name[0] == '\0');
#endif
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

TEST_CASE(
    "Peripheral Manager: The built-in cards register in descending id order") {
  // Three internal devices share slot 0 and the first registered is the one
  // the manifest names. Static initialisation gives no order of its own, so
  // the registry has to, and ids descend so that the speaker stays the front
  // device older readers compare the manifest with.
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
    CHECK(std::strcmp(registry[i - 1]->id, registry[i]->id) > 0);
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
  // In a plugin build the speaker and the keyboard are modules, registered
  // after every built-in, so the front is whichever built-in remains.
#if defined(ENABLE_PERIPHERAL_SPEAKER)
  CHECK(std::string(front->id) == "linapple.speaker");
  CHECK(std::string(manifest.peripherals[0].name) == "Speaker");
#endif
}

// A //e with its keyboard plugged in: PB0 and PB1 rest low through the
// keyboard's 470 ohm pull-downs (Apple IIe Technical Note #9; Sather,
// Understanding the Apple IIe, 7-8).
#if defined(ENABLE_PERIPHERAL_KEYBOARD) && defined(ENABLE_PERIPHERAL_JOYSTICK)
namespace {

auto record_log_line(LogLevel level, const char* message, void* user_data)
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
    // PB2 has no pull-down on the keyboard or in a two-button plug, so its
    // open TTL input reads high (Sather, Understanding the Apple II, 7-9 and
    // 7-11); PB0, PB1 and the expired timers read low.
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
#endif

namespace {

constexpr size_t page_size = 256;
std::array<uint8_t, page_size> g_page_card_rom{};

auto page_card_init(int slot, HostInterface_t* host) -> void* {
  host->RegisterCxROM(slot, g_page_card_rom.data());
  return g_page_card_rom.data();
}

auto page_card_shutdown(void* instance) -> void { (void)instance; }

Peripheral_t g_page_card = {.abi_version = LINAPPLE_ABI_VERSION,
                            .id = "test.page_card",
                            .name = "Page Card",
                            .description = "Registers a $Cn00 page",
                            .author = "LinApple Contributors",
                            .version = "1.0.0",
                            .compatible_slots = PERIPHERAL_MASK_EXPANSION,
                            .default_slot = -1,
                            .init = page_card_init,
                            .reset = nullptr,
                            .shutdown = page_card_shutdown,
                            .think = nullptr,
                            .on_vblank = nullptr,
                            .save_state = nullptr,
                            .load_state = nullptr,
                            .command = nullptr,
                            .query = nullptr};

}  // namespace

TEST_CASE(
    "Peripheral Manager: an unregistered card's $Cn00 page reads zero in the "
    "store and the live image") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  for (size_t i = 0; i < page_size; ++i) {
    g_page_card_rom.at(i) = static_cast<uint8_t>(i ^ 0xA5);
  }
  constexpr int slot = 4;
  constexpr uint16_t page = 0xC400;
  REQUIRE(peripheral_register(&g_page_card, slot) == 0);
  REQUIRE(mem[page] == g_page_card_rom.at(0));
  REQUIRE(mem[page + page_size - 1] == g_page_card_rom.at(page_size - 1));

  REQUIRE(peripheral_unregister(slot) == 0);
  const uint8_t* store = mem_get_cx_rom_peripheral() + slot * page_size;
  for (size_t i = 0; i < page_size; ++i) {
    CHECK(mem[page + i] == 0);
    CHECK(store[i] == 0);
  }
}

namespace {

// A card that keeps its host, so a case can report activity the way a card
// does: through the host interface and nothing else.
HostInterface_t* g_activity_card_host = nullptr;

auto activity_card_init(int slot, HostInterface_t* host) -> void* {
  (void)slot;
  g_activity_card_host = host;
  return &g_activity_card_host;
}

Peripheral_t g_activity_card = {.abi_version = LINAPPLE_ABI_VERSION,
                                .id = "test.activity_card",
                                .name = "Activity Card",
                                .description = "Reports activity on request",
                                .author = "LinApple Contributors",
                                .version = "1.0.0",
                                .compatible_slots = PERIPHERAL_MASK_EXPANSION,
                                .default_slot = -1,
                                .init = activity_card_init,
                                .reset = nullptr,
                                .shutdown = page_card_shutdown,
                                .think = nullptr,
                                .on_vblank = nullptr,
                                .save_state = nullptr,
                                .load_state = nullptr,
                                .command = nullptr,
                                .query = nullptr};

constexpr const char* run_request_line = "for this run";

// The line naming a fallback slot is the one thing a user sees of the request
// at the default verbosity, so the level it is logged at is part of the
// contract and a case asks for it; the shared capture drops the level.
class ScopedLevelLog_t {
 public:
  struct Line_t {
    LogLevel_t level;
    std::string text;
  };

  ScopedLevelLog_t() : verbosity_(Logger::get_verbosity()) {
    Logger::set_verbosity(LogLevel_t::info);
    Logger::set_callback_with_context(collect, &lines_);
  }
  ~ScopedLevelLog_t() {
    Logger::set_callback_with_context(nullptr, nullptr);
    Logger::set_verbosity(verbosity_);
  }
  ScopedLevelLog_t(const ScopedLevelLog_t&) = delete;
  auto operator=(const ScopedLevelLog_t&) -> ScopedLevelLog_t& = delete;
  ScopedLevelLog_t(ScopedLevelLog_t&&) = delete;
  auto operator=(ScopedLevelLog_t&&) -> ScopedLevelLog_t& = delete;

  auto count_containing(const std::string& needle) const -> size_t {
    size_t n = 0;
    for (const Line_t& line : lines_) {
      n += (line.text.find(needle) != std::string::npos) ? 1 : 0;
    }
    return n;
  }
  auto count_at(LogLevel_t level, const std::string& needle) const -> size_t {
    size_t n = 0;
    for (const Line_t& line : lines_) {
      n += (line.level == level && line.text.find(needle) != std::string::npos)
               ? 1
               : 0;
    }
    return n;
  }

 private:
  static auto collect(LogLevel_t level, const char* message, void* user_data)
      -> void {
    auto* lines = static_cast<std::vector<Line_t>*>(user_data);
    if (lines != nullptr && message != nullptr) {
      lines->push_back({level, message});
    }
  }

  std::vector<Line_t> lines_;
  LogLevel_t verbosity_;
};

}  // namespace

TEST_CASE(
    "Peripheral Manager: peripheral_slot_of names the lowest slot holding the "
    "id and -1 for none") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_slot_of("test.page_card") == -1);
  CHECK(peripheral_slot_of(nullptr) == -1);

  REQUIRE(peripheral_register(&g_page_card, 5) == 0);
  CHECK(peripheral_slot_of("test.page_card") == 5);
  REQUIRE(peripheral_register(&g_activity_card, 2) == 0);
  CHECK(peripheral_slot_of("test.page_card") == 5);
  CHECK(peripheral_slot_of("test.activity_card") == 2);

  REQUIRE(peripheral_unregister(5) == 0);
  CHECK(peripheral_slot_of("test.page_card") == -1);
  CHECK(peripheral_slot_of("test.activity_card") == 2);

  // Two of one card: the lower slot is the answer.
  REQUIRE(peripheral_register(&g_page_card, 6) == 0);
  REQUIRE(peripheral_register(&g_page_card, 3) == 0);
  CHECK(peripheral_slot_of("test.page_card") == 3);
}

TEST_CASE(
    "Peripheral Manager: an activity poll answers once per report, clears on "
    "the read and leaves the level alone") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  constexpr int slot = 5;
  REQUIRE(peripheral_register(&g_activity_card, slot) == 0);
  REQUIRE(g_activity_card_host != nullptr);
  REQUIRE(g_activity_card_host->NotifyActivityChanged != nullptr);

  CHECK_FALSE(peripheral_activity_poll(slot));
  CHECK_FALSE(peripheral_is_any_active());

  g_activity_card_host->NotifyActivityChanged(slot, true);
  CHECK(peripheral_is_any_active());
  CHECK(peripheral_activity_poll(slot));
  CHECK_FALSE(peripheral_activity_poll(slot));
  CHECK(peripheral_is_any_active());

  g_activity_card_host->NotifyActivityChanged(slot, false);
  CHECK_FALSE(peripheral_activity_poll(slot));
  CHECK_FALSE(peripheral_is_any_active());

  // A burst that starts and ends between two polls is still seen once.
  g_activity_card_host->NotifyActivityChanged(slot, true);
  g_activity_card_host->NotifyActivityChanged(slot, false);
  CHECK(peripheral_activity_poll(slot));
  CHECK_FALSE(peripheral_activity_poll(slot));

  CHECK_FALSE(peripheral_activity_poll(-1));
  CHECK_FALSE(peripheral_activity_poll(static_cast<int>(NUM_SLOTS)));

  g_activity_card_host->NotifyActivityChanged(slot, true);
  peripheral_manager_reset();
  CHECK_FALSE(peripheral_activity_poll(slot));
}

TEST_CASE(
    "Peripheral Manager: a run request for a card the build lacks installs "
    "nothing and reports no slot") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  linapple_request_card_for_run("test.no_such_card");
  TestFixtures::ScopedCore_t core(config);

  CHECK(linapple_requested_slot() == -1);
  SS_PERIPHERAL_MANIFEST manifest;
  peripheral_get_manifest(&manifest);
  for (int slot = 1; slot < static_cast<int>(NUM_SLOTS); ++slot) {
    CAPTURE(slot);
    CHECK(manifest.peripherals[slot].name[0] == '\0');
  }
}

#ifdef ENABLE_PERIPHERAL_HARDDISK
namespace {

constexpr const char* harddisk_id = "linapple.harddisk";

// The core takes the machine type from the configuration only through the
// application controller, so a case built on the bridge alone names the
// model here and puts the previous one back.
class ScopedMachineType_t {
 public:
  explicit ScopedMachineType_t(Apple2Type_t type)
      : previous_(linapple_get_apple2_type()) {
    linapple_set_apple2_type(type);
  }
  ~ScopedMachineType_t() { linapple_set_apple2_type(previous_); }
  ScopedMachineType_t(const ScopedMachineType_t&) = delete;
  auto operator=(const ScopedMachineType_t&) -> ScopedMachineType_t& = delete;
  ScopedMachineType_t(ScopedMachineType_t&&) = delete;
  auto operator=(ScopedMachineType_t&&) -> ScopedMachineType_t& = delete;

 private:
  Apple2Type_t previous_;
};

auto slot_entry(int slot) -> std::string {
  return Configuration_t::instance().get_string("Slots",
                                                "Slot " + std::to_string(slot));
}

}  // namespace

TEST_CASE(
    "Peripheral Manager: peripheral_slot_of finds the hard disk wherever the "
    "slot table put it") {
  SUBCASE("slot 7") {
    TestFixtures::ScopedTestConfig_t::Description_t description;
    description.slots[6] = "Harddisk";
    TestFixtures::ScopedTestConfig_t config(description);
    TestFixtures::ScopedCore_t core(config);
    CHECK(peripheral_slot_of(harddisk_id) == 7);
  }
  SUBCASE("slot 5") {
    TestFixtures::ScopedTestConfig_t::Description_t description;
    description.slots[4] = "Harddisk";
    TestFixtures::ScopedTestConfig_t config(description);
    TestFixtures::ScopedCore_t core(config);
    CHECK(peripheral_slot_of(harddisk_id) == 5);
  }
  SUBCASE("none") {
    TestFixtures::ScopedTestConfig_t config(
        TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
    TestFixtures::ScopedCore_t core(config);
    CHECK(peripheral_slot_of(harddisk_id) == -1);
  }
}

TEST_CASE(
    "Peripheral Manager: a run request takes an empty slot 7 silently and "
    "writes nothing into the slot table") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedLogCapture_t log;
  linapple_request_card_for_run(harddisk_id);
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_present(7, harddisk_id));
  CHECK(linapple_requested_slot() == 7);
  CHECK(peripheral_slot_of(harddisk_id) == 7);
  CHECK(log.count_containing(run_request_line) == 0);
  CHECK(slot_entry(7) == "None");
  CHECK(Configuration_t::instance()
            .get_string("Configuration", "Harddisk Enable")
            .empty());
  CHECK(Configuration_t::instance()
            .get_string("Preferences", "Harddisk Enable")
            .empty());
}

TEST_CASE(
    "Peripheral Manager: a run request is satisfied by the hard disk the slot "
    "table already placed, wherever that is") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[4] = "Harddisk";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedLogCapture_t log;
  linapple_request_card_for_run(harddisk_id);
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_present(5, harddisk_id));
  CHECK_FALSE(peripheral_present(7, harddisk_id));
  CHECK(linapple_requested_slot() == 5);
  CHECK(log.count_containing(run_request_line) == 0);
}

TEST_CASE(
    "Peripheral Manager: a run request lasts one registration and is asked "
    "for again by whoever still wants it") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  linapple_request_card_for_run(harddisk_id);
  {
    TestFixtures::ScopedCore_t core(config);
    REQUIRE(peripheral_present(7, harddisk_id));
  }
  {
    TestFixtures::ScopedCore_t core(config);
    CHECK_FALSE(peripheral_present(7, harddisk_id));
    CHECK(linapple_requested_slot() == -1);
  }
}

namespace {

constexpr const char* harddisk_key_line = "Harddisk Enable is set, but [Slots]";

// The fixture writes every slot; a file saved without a Slot 7 line leaves it
// out, which is the only case Harddisk Enable decides.
auto drop_slot_line(const TestFixtures::ScopedTestConfig_t& config, int slot)
    -> void {
  std::ifstream in(config.path());
  std::stringstream kept;
  const std::string prefix = "Slot " + std::to_string(slot) + " ";
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind(prefix, 0) != 0) {
      kept << line << "\n";
    }
  }
  in.close();
  std::ofstream out(config.path(), std::ios::trunc);
  out << kept.str();
}

}  // namespace

TEST_CASE(
    "Peripheral Manager: a Slot 7 line naming the hard disk gives the card "
    "whatever Harddisk Enable says, and logs nothing") {
  for (const char* key : {"0", "1"}) {
    CAPTURE(key);
    TestFixtures::ScopedTestConfig_t::Description_t description;
    description.slots[6] = "Harddisk";
    description.extras.push_back({"Configuration", "Harddisk Enable", key});
    TestFixtures::ScopedTestConfig_t config(description);
    TestFixtures::ScopedLogCapture_t log;
    TestFixtures::ScopedCore_t core(config);

    CHECK(peripheral_present(7, harddisk_id));
    CHECK(log.count_containing(harddisk_key_line) == 0);
  }
}

TEST_CASE(
    "Peripheral Manager: Harddisk Enable = 1 beside a Slot 7 line naming no "
    "card installs nothing and says why once") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.extras.push_back({"Configuration", "Harddisk Enable", "1"});
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedLogCapture_t log;
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_slot_of(harddisk_id) == -1);
  CHECK(log.count_containing(
            "Harddisk Enable is set, but [Slots] names None for slot 7") == 1);
}

#ifdef ENABLE_PERIPHERAL_CLOCK
TEST_CASE(
    "Peripheral Manager: Harddisk Enable = 1 beside a Slot 7 line naming "
    "another card keeps that card and says why once") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[6] = "Clock Card";
  description.extras.push_back({"Preferences", "Harddisk Enable", "1"});
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedLogCapture_t log;
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_present(7, "linapple.clock"));
  CHECK(peripheral_slot_of(harddisk_id) == -1);
  CHECK(log.count_containing("Harddisk Enable is set, but [Slots] names Clock "
                             "Card for slot 7") == 1);
}
#endif

TEST_CASE(
    "Peripheral Manager: with no Slot 7 line Harddisk Enable decides, "
    "[Preferences] before [Configuration]") {
  struct Row_t {
    const char* preferences;
    const char* configuration;
    bool installed;
  };
  // An empty value leaves the key out of that section.
  const std::array<Row_t, 6> rows = {{{"1", "", true},
                                      {"", "1", true},
                                      {"1", "0", true},
                                      {"0", "1", false},
                                      {"", "0", false},
                                      {"", "", false}}};
  for (const Row_t& row : rows) {
    CAPTURE(row.preferences);
    CAPTURE(row.configuration);
    TestFixtures::ScopedTestConfig_t::Description_t description;
    if (*row.preferences != '\0') {
      description.extras.push_back(
          {"Preferences", "Harddisk Enable", row.preferences});
    }
    if (*row.configuration != '\0') {
      description.extras.push_back(
          {"Configuration", "Harddisk Enable", row.configuration});
    }
    TestFixtures::ScopedTestConfig_t config(description);
    drop_slot_line(config, 7);
    TestFixtures::ScopedLogCapture_t log;
    TestFixtures::ScopedCore_t core(config);

    CHECK(peripheral_present(7, harddisk_id) == row.installed);
    CHECK(peripheral_slot_of(harddisk_id) == (row.installed ? 7 : -1));
    CHECK(log.count_containing(harddisk_key_line) == 0);
  }
}

#ifdef ENABLE_PERIPHERAL_CLOCK
TEST_CASE(
    "Peripheral Manager: a run request never displaces the card in slot 7 and "
    "names the slot it took below it once") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[6] = "Clock Card";
  TestFixtures::ScopedTestConfig_t config(description);
  ScopedLevelLog_t log;
  linapple_request_card_for_run(harddisk_id);
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_present(7, "linapple.clock"));
  CHECK(peripheral_present(6, harddisk_id));
  CHECK(peripheral_slot_of(harddisk_id) == 6);
  CHECK(linapple_requested_slot() == 6);
  CHECK(log.count_at(LogLevel_t::warning,
                     "Slot 7 holds Clock Card; Harddisk installed in slot 6 "
                     "for this run") == 1);
  CHECK(log.count_containing(run_request_line) == 1);
  CHECK(log.count_containing("already has a peripheral") == 0);
  CHECK(slot_entry(6) == "None");
  CHECK(slot_entry(7) == "Clock Card");
}

#if defined(ENABLE_PERIPHERAL_DISK) && defined(ENABLE_PERIPHERAL_MOCKINGBOARD)
TEST_CASE(
    "Peripheral Manager: a run request skips slot 3 on a //e, whose page the "
    "internal firmware owns, and takes it on a II Plus") {
  struct Model_t {
    int config_type;
    Apple2Type_t type;
    int expected_slot;
  };
  const std::array<Model_t, 3> models = {
      {{TestFixtures::ScopedTestConfig_t::machine_apple2e_enhanced,
        A2TYPE_APPLE2EENHANCED, 2},
       {TestFixtures::ScopedTestConfig_t::machine_apple2e, A2TYPE_APPLE2E, 2},
       {TestFixtures::ScopedTestConfig_t::machine_apple2_plus,
        A2TYPE_APPLE2PLUS, 3}}};
  for (const Model_t& model : models) {
    CAPTURE(model.config_type);
    TestFixtures::ScopedTestConfig_t::Description_t description;
    description.machine_type = model.config_type;
    description.slots[6] = "Clock Card";
    description.slots[5] = "Disk II";
    description.slots[4] = "Mockingboard";
    description.slots[3] = "Mockingboard";
    TestFixtures::ScopedTestConfig_t config(description);
    ScopedLevelLog_t log;
    ScopedMachineType_t machine(model.type);
    linapple_request_card_for_run(harddisk_id);
    TestFixtures::ScopedCore_t core(config);

    CHECK(peripheral_slot_of(harddisk_id) == model.expected_slot);
    CHECK(linapple_requested_slot() == model.expected_slot);
    CHECK(log.count_containing(run_request_line) == 1);
    CHECK(log.count_at(
              LogLevel_t::warning,
              "installed in slot " + std::to_string(model.expected_slot)) == 1);
    CHECK(peripheral_present(7, "linapple.clock"));
    CHECK(peripheral_present(6, "linapple.disk_II"));
    CHECK(peripheral_present(5, "linapple.mockingboard"));
    CHECK(peripheral_present(4, "linapple.mockingboard"));
  }
}
#endif

#ifdef ENABLE_PERIPHERAL_MOCKINGBOARD
TEST_CASE(
    "Peripheral Manager: a run request with no free slot installs nothing and "
    "leaves every card where it was") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[6] = "Clock Card";
  for (size_t i = 0; i < 6; ++i) {
    description.slots[i] = "Mockingboard";
  }
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedLogCapture_t log;
  linapple_request_card_for_run(harddisk_id);
  TestFixtures::ScopedCore_t core(config);

  CHECK(linapple_requested_slot() == -1);
  CHECK(peripheral_slot_of(harddisk_id) == -1);
  CHECK(peripheral_present(7, "linapple.clock"));
  for (int slot = 1; slot <= 6; ++slot) {
    CAPTURE(slot);
    CHECK(peripheral_present(slot, "linapple.mockingboard"));
  }
  CHECK(log.count_containing(run_request_line) == 0);
  CHECK(log.count_containing("already has a peripheral") == 0);
}
#endif

#if defined(ENABLE_PERIPHERAL_MOUSE) && defined(ENABLE_PERIPHERAL_DISK) && \
    defined(ENABLE_PERIPHERAL_MOCKINGBOARD)
TEST_CASE(
    "Peripheral Manager: the Mouse in slot 4 key is applied before a run "
    "request, so the two never contend for a slot") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[6] = "Clock Card";
  description.slots[5] = "Disk II";
  description.slots[4] = "Mockingboard";
  description.extras.push_back({"Configuration", "Mouse in slot 4", "1"});
  TestFixtures::ScopedTestConfig_t config(description);
  linapple_request_card_for_run(harddisk_id);
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_present(4, "linapple.mouse"));
  CHECK(peripheral_slot_of(harddisk_id) == 2);
  CHECK(linapple_requested_slot() == 2);
}
#endif
#endif
#endif
