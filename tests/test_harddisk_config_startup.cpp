// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppController.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;
constexpr uint16_t boot_entry = 0x0801;
constexpr uint32_t frame_cycles = 17030;
constexpr int boot_frame_cap = 120;

constexpr const char* fallback_line = "Harddisk installed in slot";
constexpr const char* duplicate_warning = "already has a peripheral";
constexpr const char* no_free_slot_line =
    "--hd1: no free slot for the Harddisk; image not mounted";
constexpr const char* not_configured_line =
    "Harddisk Image 1 is set but no hard disk is configured; not mounted";

// Every line at the level it was logged, so a case can tell an error from the
// same words logged as information. The application controller sets the
// verbosity from the arguments, which is why the cases pass --log.
class ScopedLevelLog_t {
 public:
  struct Line_t {
    LogLevel level;
    std::string text;
  };

  ScopedLevelLog_t() { Logger::set_callback_with_context(collect, &lines_); }
  ~ScopedLevelLog_t() { Logger::set_callback_with_context(nullptr, nullptr); }
  ScopedLevelLog_t(const ScopedLevelLog_t&) = delete;
  auto operator=(const ScopedLevelLog_t&) -> ScopedLevelLog_t& = delete;
  ScopedLevelLog_t(ScopedLevelLog_t&&) = delete;
  auto operator=(ScopedLevelLog_t&&) -> ScopedLevelLog_t& = delete;

  auto count(const std::string& needle) const -> size_t {
    size_t n = 0;
    for (const Line_t& line : lines_) {
      n += (line.text.find(needle) != std::string::npos) ? 1 : 0;
    }
    return n;
  }
  auto count_at(LogLevel level, const std::string& needle) const -> size_t {
    size_t n = 0;
    for (const Line_t& line : lines_) {
      n += (line.level == level && line.text.find(needle) != std::string::npos)
               ? 1
               : 0;
    }
    return n;
  }

 private:
  static auto collect(LogLevel level, const char* message, void* user_data)
      -> void {
    auto* lines = static_cast<std::vector<Line_t>*>(user_data);
    if (lines != nullptr && message != nullptr) {
      lines->push_back({level, message});
    }
  }

  std::vector<Line_t> lines_;
};

// The machine a user gets from the command line, started the way every
// frontend starts it: the arguments parsed, the controller initialised, the
// initial media loaded. The declared configuration names the machine.
class CommandLineMachine_t {
 public:
  CommandLineMachine_t(const TestConfig_t& machine,
                       std::vector<std::string> args)
      : args_(std::move(args)) {
    args_.insert(args_.begin(), {"linapple", "--log"});
    std::vector<char*> argv;
    argv.reserve(args_.size());
    for (std::string& arg : args_) {
      argv.push_back(&arg.front());
    }
    // Parsed into the one configuration object, as every frontend's main
    // does, so the controller reads its paths from the instance's own
    // buffers.
    AppConfig& config = Configuration::instance();
    config = AppConfig{};
    REQUIRE(app_args_parse(static_cast<int>(argv.size()), argv.data(),
                           &config) == 0);
    util_safe_strcpy(config.config_path.data(), machine.c_str(), path_max_len);
    REQUIRE(app_controller_initialize(&config) == 0);
    app_controller_load_initial_media(&config);
  }
  ~CommandLineMachine_t() { app_controller_shutdown(); }
  CommandLineMachine_t(const CommandLineMachine_t&) = delete;
  auto operator=(const CommandLineMachine_t&) -> CommandLineMachine_t& = delete;
  CommandLineMachine_t(CommandLineMachine_t&&) = delete;
  auto operator=(CommandLineMachine_t&&) -> CommandLineMachine_t& = delete;

 private:
  std::vector<std::string> args_;
};

auto harddisk_in_slot(int slot) -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[slot - 1] = "Harddisk";
  return description;
}

auto status_in(int slot) -> HarddiskStatus_t {
  HarddiskStatus_t out{};
  size_t size = sizeof(out);
  REQUIRE(peripheral_query(slot, harddisk_query_status, &out, &size) ==
          peripheral_ok);
  return out;
}

auto cards_of(const char* id) -> int {
  int n = 0;
  for (int slot = 1; slot < static_cast<int>(num_slots); ++slot) {
    n += peripheral_present(slot, id) ? 1 : 0;
  }
  return n;
}

// The fixture writes every slot; a file saved without a Slot 7 line leaves it
// out, which is the only case Harddisk Enable decides.
auto drop_slot_line(const TestConfig_t& config, int slot) -> void {
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

// get_string falls back to any section holding the key, so absence from one
// section is read from that section alone.
auto in_section(const Configuration& config, const char* section,
                const char* key) -> bool {
  const auto* entries = config.get_section(section);
  return entries != nullptr && entries->count(key) != 0;
}

auto saved_file(const TestConfig_t& config) -> Configuration {
  Configuration saved{};
  REQUIRE(saved.load(config.path()));
  return saved;
}

auto file_text(const std::string& path) -> std::string {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

auto modification_time(const std::string& path) -> timespec {
  struct stat info{};
  REQUIRE(stat(path.c_str(), &info) == 0);
  return info.st_mtim;
}

auto same_time(const timespec& a, const timespec& b) -> bool {
  return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

// Block 0 of minimal-block.hdv jumps to itself, so a boot that reached $0801
// stays there.
auto boots_from_hard_disk() -> bool {
  linapple_reset_hard();
  for (int frame = 0; frame < boot_frame_cap; ++frame) {
    linapple_run_frame(frame_cycles);
    if (cpu_get_registers()->pc == boot_entry) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE(
    "Harddisk configuration at start-up: a Slot 7 line gives the machine the "
    "card") {
  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Harddisk";
  TestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_present(card_slot, harddisk_id));
}

TEST_CASE(
    "Harddisk configuration at start-up: --hd1 with the shipped Slot 7 mounts "
    "into the configured card once and boots it") {
  TestConfig_t config(harddisk_in_slot(card_slot));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  ScopedLevelLog_t log;
  CommandLineMachine_t machine(config, {"--hd1", image.path()});

  CHECK(cards_of(harddisk_id) == 1);
  CHECK(peripheral_present(card_slot, harddisk_id));
  CHECK(status_in(card_slot).drive0_loaded == 1);
  CHECK(log.count(duplicate_warning) == 0);
  CHECK(log.count(fallback_line) == 0);
  CHECK(boots_from_hard_disk());
}

TEST_CASE(
    "Harddisk configuration at start-up: --hd1 naming a missing image says so "
    "at error level once and the machine runs on") {
  TestConfig_t config(harddisk_in_slot(card_slot));
  ScopedLevelLog_t log;
  CommandLineMachine_t machine(config, {"--hd1", "missing.hdv"});

  // The path the controller handed over lives in the configuration's own
  // buffer; a run request is never recorded, so the buffer still holds it.
  CHECK(std::string(Configuration::instance().harddisk_path.at(0).data()) ==
        "missing.hdv");
  CHECK(log.count_at(LogLevel::error,
                     "could not insert hard disk image 'missing.hdv': "
                     "file not found or unreadable") == 1);
  // The level supplies the word on the terminal, so the text carries none.
  CHECK(log.count("error:") == 0);
  CHECK(log.count("missing.hdv") == 1);
  CHECK(log.count("hard disk drive 1:") == 0);
  CHECK(log.count("(2)") == 0);
  CHECK(status_in(card_slot).drive0_loaded == 0);
  CHECK(Configuration::instance()
            .get_string("Preferences", "Harddisk Image 1")
            .empty());
  linapple_run_frame(frame_cycles);
  CHECK(system_state.mode == app_mode_running);
}

TEST_CASE(
    "Harddisk configuration at start-up: --hd1 with slot 7 empty installs the "
    "card there for the run and remembers nothing of it") {
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");

  SUBCASE("Slot 7 = None") {
    TestConfig_t config(TestConfig_t::enhanced_2e_only());
    ScopedLevelLog_t log;
    CommandLineMachine_t machine(config, {"--hd1", image.path()});

    CHECK(peripheral_present(card_slot, harddisk_id));
    CHECK(status_in(card_slot).drive0_loaded == 1);
    CHECK(log.count(fallback_line) == 0);
    CHECK(log.count(duplicate_warning) == 0);
    CHECK(boots_from_hard_disk());

    REQUIRE(Configuration::instance().save());
    CHECK(Configuration::instance().get_string("Slots", "Slot 7") == "None");
    const Configuration saved = saved_file(config);
    CHECK(saved.get_string("Slots", "Slot 7") == "None");
    CHECK_FALSE(in_section(saved, "Preferences", "Harddisk Enable"));
    CHECK_FALSE(in_section(saved, "Configuration", "Harddisk Enable"));
    CHECK_FALSE(in_section(saved, "Preferences", "Harddisk Image 1"));
  }

  SUBCASE("no Slot 7 line and Harddisk Enable = 0") {
    TestConfig_t::Description_t description;
    description.extras.push_back({"Configuration", "Harddisk Enable", "0"});
    TestConfig_t config(description);
    drop_slot_line(config, card_slot);
    CommandLineMachine_t machine(config, {"--hd1", image.path()});

    CHECK(peripheral_present(card_slot, harddisk_id));
    CHECK(status_in(card_slot).drive0_loaded == 1);

    REQUIRE(Configuration::instance().save());
    const Configuration saved = saved_file(config);
    CHECK_FALSE(in_section(saved, "Slots", "Slot 7"));
    CHECK(saved.get_string("Configuration", "Harddisk Enable") == "0");
    CHECK_FALSE(in_section(saved, "Preferences", "Harddisk Enable"));
  }
}

TEST_CASE(
    "Harddisk configuration at start-up: --hd1 is this run's request, so the "
    "start-up insert writes no file and a later save keeps the saved image "
    "keys as they were") {
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  const std::string remembered = "/remembered/volume.hdv";
  TestConfig_t::Description_t description = harddisk_in_slot(card_slot);
  description.extras.push_back({"Preferences", "Harddisk Image 1", remembered});
  TestConfig_t config(description);
  const std::string text_before = file_text(config.path());
  const timespec time_before = modification_time(config.path());

  CommandLineMachine_t machine(config, {"--hd1", image.path()});
  const HarddiskStatus_t status = status_in(card_slot);
  CHECK(status.drive0_loaded == 1);
  CHECK(std::string(status.drive0_full_path) == image.path());
  CHECK(file_text(config.path()) == text_before);
  CHECK(same_time(modification_time(config.path()), time_before));

  Configuration::instance().set_string("Preferences", "HDV Starting Directory",
                                       "/elsewhere");
  REQUIRE(Configuration::instance().save());
  CHECK(file_text(config.path()) != text_before);
  const Configuration saved = saved_file(config);
  CHECK(saved.get_string("Preferences", "HDV Starting Directory") ==
        "/elsewhere");
  CHECK(saved.get_string("Preferences", "Harddisk Image 1") == remembered);
  CHECK_FALSE(in_section(saved, "Preferences", "Harddisk Image 2"));
}

TEST_CASE(
    "Harddisk configuration at start-up: a saved Harddisk Image 1 mounts into "
    "the configured card without the file being rewritten") {
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  TestConfig_t::Description_t description = harddisk_in_slot(card_slot);
  description.extras.push_back(
      {"Preferences", "Harddisk Image 1", image.path()});
  TestConfig_t config(description);
  const std::string text_before = file_text(config.path());
  const timespec time_before = modification_time(config.path());

  CommandLineMachine_t machine(config, {});
  const HarddiskStatus_t status = status_in(card_slot);
  CHECK(status.drive0_loaded == 1);
  CHECK(std::string(status.drive0_full_path) == image.path());
  CHECK(Configuration::instance().get_string(
            "Preferences", "Harddisk Image 1") == image.path());
  CHECK(file_text(config.path()) == text_before);
  CHECK(same_time(modification_time(config.path()), time_before));
}

TEST_CASE(
    "Harddisk configuration at start-up: --hd2 alone installs the card and "
    "fills drive 2 only") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  CommandLineMachine_t machine(config, {"--hd2", image.path()});

  REQUIRE(peripheral_present(card_slot, harddisk_id));
  const HarddiskStatus_t status = status_in(card_slot);
  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive1_loaded == 1);
  CHECK(std::string(status.drive1_full_path) == image.path());
}

TEST_CASE(
    "Harddisk configuration at start-up: --hd1 mounts into a hard disk the "
    "slot table put elsewhere and installs no second card") {
  TestConfig_t config(harddisk_in_slot(5));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  ScopedLevelLog_t log;
  CommandLineMachine_t machine(config, {"--hd1", image.path()});

  CHECK(cards_of(harddisk_id) == 1);
  CHECK(peripheral_present(5, harddisk_id));
  CHECK_FALSE(peripheral_present(card_slot, harddisk_id));
  CHECK(linapple_requested_slot() == 5);
  CHECK(status_in(5).drive0_loaded == 1);
  CHECK(log.count(fallback_line) == 0);
}

TEST_CASE(
    "Harddisk configuration at start-up: a saved image with no hard disk "
    "configured is not mounted and installs nothing") {
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  TestConfig_t::Description_t description;
  description.extras.push_back(
      {"Preferences", "Harddisk Image 1", image.path()});
  TestConfig_t config(description);
  ScopedLevelLog_t log;
  CommandLineMachine_t machine(config, {});

  CHECK(peripheral_slot_of(harddisk_id) == -1);
  CHECK(linapple_requested_slot() == -1);
  CHECK(log.count(not_configured_line) == 1);
  CHECK(log.count(fallback_line) == 0);
}

#ifdef ENABLE_PERIPHERAL_CLOCK
TEST_CASE(
    "Harddisk configuration at start-up: --hd1 beside a clock card in slot 7 "
    "installs the card in slot 6, says so once and autoboots it") {
  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Clock Card";
  TestConfig_t config(description);
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  ScopedLevelLog_t log;
  CommandLineMachine_t machine(config, {"--hd1", image.path()});

  CHECK(peripheral_present(card_slot, "linapple.clock"));
  CHECK(peripheral_slot_of(harddisk_id) == 6);
  CHECK(status_in(6).drive0_loaded == 1);
  CHECK(log.count_at(LogLevel::warning,
                     "Slot 7 holds Clock Card; Harddisk installed in slot 6 "
                     "for this run") == 1);
  CHECK(log.count(fallback_line) == 1);
  CHECK(log.count("--hd1:") == 0);
  CHECK(log.count(duplicate_warning) == 0);

  // The clock's $C701 fails the Autostart ID test, so the scan reaches 6.
  CHECK(boots_from_hard_disk());
  CHECK(cpu_get_registers()->x == 0x60);

  REQUIRE(Configuration::instance().save());
  const Configuration saved = saved_file(config);
  CHECK(saved.get_string("Slots", "Slot 6") == "None");
  CHECK(saved.get_string("Slots", "Slot 7") == "Clock Card");
}

TEST_CASE(
    "Harddisk configuration at start-up: --hd1 and --hd2 beside a clock card "
    "land on one card holding both") {
  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Clock Card";
  TestConfig_t config(description);
  const auto first = TestFixtures::create_ephemeral("minimal-block.hdv");
  const auto second = TestFixtures::create_ephemeral("minimal.po");
  CommandLineMachine_t machine(config,
                               {"--hd1", first.path(), "--hd2", second.path()});

  CHECK(cards_of(harddisk_id) == 1);
  REQUIRE(peripheral_slot_of(harddisk_id) == 6);
  const HarddiskStatus_t status = status_in(6);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive1_loaded == 1);
}

#if defined(ENABLE_PERIPHERAL_DISK) && defined(ENABLE_PERIPHERAL_MOCKINGBOARD)
TEST_CASE(
    "Harddisk configuration at start-up: --hd1 skips slot 3 on a //e and "
    "takes it on a II Plus") {
  struct Model_t {
    int config_type;
    int expected_slot;
  };
  const std::vector<Model_t> models = {
      {TestConfig_t::machine_apple2e_enhanced, 2},
      {TestConfig_t::machine_apple2e, 2},
      {TestConfig_t::machine_apple2_plus, 3},
  };
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  for (const Model_t& model : models) {
    CAPTURE(model.config_type);
    TestConfig_t::Description_t description;
    description.machine_type = model.config_type;
    description.slots[6] = "Clock Card";
    description.slots[5] = "Disk II";
    description.slots[4] = "Mockingboard";
    description.slots[3] = "Mockingboard";
    TestConfig_t config(description);
    ScopedLevelLog_t log;
    CommandLineMachine_t machine(config, {"--hd1", image.path()});

    CHECK(peripheral_slot_of(harddisk_id) == model.expected_slot);
    CHECK(status_in(model.expected_slot).drive0_loaded == 1);
    CHECK(log.count_at(
              LogLevel::warning,
              "installed in slot " + std::to_string(model.expected_slot)) == 1);
  }
}
#endif

#ifdef ENABLE_PERIPHERAL_MOCKINGBOARD
TEST_CASE(
    "Harddisk configuration at start-up: --hd1 with no free slot says so at "
    "error level once and leaves every card where it was") {
  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Clock Card";
  for (size_t i = 0; i < 6; ++i) {
    description.slots[i] = "Mockingboard";
  }
  TestConfig_t config(description);
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  ScopedLevelLog_t log;
  CommandLineMachine_t machine(config, {"--hd1", image.path()});

  CHECK(peripheral_slot_of(harddisk_id) == -1);
  CHECK(log.count_at(LogLevel::error, no_free_slot_line) == 1);
  CHECK(log.count(no_free_slot_line) == 1);
  CHECK(peripheral_present(card_slot, "linapple.clock"));
  for (int slot = 1; slot <= 6; ++slot) {
    CAPTURE(slot);
    CHECK(peripheral_present(slot, "linapple.mockingboard"));
  }
  CHECK(Configuration::instance()
            .get_string("Preferences", "Harddisk Image 1")
            .empty());
}
#endif
#endif
