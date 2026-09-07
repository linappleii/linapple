// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>

#include <fstream>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/config/ConfigDiscovery.h"
#include "core/config/ConfigSchema.h"
#include "core/config/Toml.h"
#include "doctest.h"

TEST_CASE("ConfigDiscovery: Search Path Enumeration") {
  auto all_paths = config_get_search_paths();
  REQUIRE(all_paths.size() >= 4);
  CHECK(all_paths[0] == "./linapple.toml");
  CHECK(all_paths[1] == "./linapple.conf");

  auto toml_paths = config_get_toml_search_paths();
  REQUIRE_FALSE(toml_paths.empty());
  CHECK(toml_paths[0] == "./linapple.toml");

  auto legacy_paths = config_get_legacy_search_paths();
  REQUIRE_FALSE(legacy_paths.empty());
  CHECK(legacy_paths[0] == "./linapple.conf");
}

TEST_CASE("ConfigDiscovery: Default User Config Path") {
  std::string user_path = config_get_default_user_path();
  CHECK_FALSE(user_path.empty());
  CHECK(user_path.find("linapple.toml") != std::string::npos);
}

TEST_CASE("ConfigDiscovery: CLI Override Resolution") {
  const std::string temp_toml = "/tmp/test_linapple_override.toml";
  unlink(temp_toml.c_str());

  // Non-existent file returns empty
  CHECK(config_locate_toml_file(temp_toml).empty());

  // Create temporary file
  {
    std::ofstream out(temp_toml);
    out << "[Core]\nMachine = \"Apple //e\"\n";
  }

  // Existing file returns exact path
  CHECK(config_locate_toml_file(temp_toml) == temp_toml);

  ConfigSearchOptions_t options;
  options.cli_override = temp_toml;
  std::string resolved;
  bool is_legacy = true;
  CHECK(config_discover(options, &resolved, &is_legacy));
  CHECK(resolved == temp_toml);
  CHECK_FALSE(is_legacy);

  unlink(temp_toml.c_str());
}

TEST_CASE("ConfigDiscovery: Case-Insensitive Legacy Extension") {
  const std::string temp_conf = "/tmp/test_linapple_upper.CONF";
  unlink(temp_conf.c_str());

  {
    std::ofstream out(temp_conf);
    out << "[Configuration]\n";
  }

  ConfigSearchOptions_t options;
  options.cli_override = temp_conf;
  std::string resolved;
  bool is_legacy = false;
  CHECK(config_discover(options, &resolved, &is_legacy));
  CHECK(resolved == temp_conf);
  CHECK(is_legacy);

  unlink(temp_conf.c_str());
}

TEST_CASE("ConfigDiscovery: Default Template Auto-Creation") {
  const std::string temp_gen = "/tmp/test_linapple_generated_default.toml";
  unlink(temp_gen.c_str());

  std::string created_path;
  bool ok = config_ensure_default_exists(temp_gen, &created_path);
  CHECK(ok);
  CHECK(created_path == temp_gen);
  CHECK(access(temp_gen.c_str(), R_OK) == 0);

  // Parse and verify contents
  std::string parse_err;
  auto doc = toml_document_load_file(temp_gen, &parse_err);
  REQUIRE(doc != nullptr);
  CHECK(parse_err.empty());

  CHECK(toml_find_table(doc.get(), "Core") != nullptr);
  CHECK(toml_find_table(doc.get(), "Video") != nullptr);
  CHECK(toml_find_table(doc.get(), "Audio") != nullptr);
  CHECK(toml_find_table(doc.get(), "Slots") != nullptr);

  // Calling again on existing file succeeds idempotently
  std::string re_created;
  CHECK(config_ensure_default_exists(temp_gen, &re_created));
  CHECK(re_created == temp_gen);

  unlink(temp_gen.c_str());
}

TEST_CASE("ConfigDiscovery: config_load_active with Modern TOML") {
  const std::string temp_toml = "/tmp/test_linapple_active_modern.toml";
  unlink(temp_toml.c_str());

  {
    std::ofstream out(temp_toml);
    out << R"(
[Core]
Machine = "Apple //e Enhanced"
EmulationSpeed = 2.0

[Video]
VideoStandard = PAL
VideoEmulation = "Color TV Emulation"
ScreenFactor = 1.5

[Slots]
Slot1 = ParallelPrinter
Slot6 = DiskII
)";
  }

  LinAppleConfig_t config;
  std::string loaded_path;
  bool is_legacy = true;
  std::string err;

  bool ok =
      config_load_active(temp_toml, &config, &loaded_path, &is_legacy, &err);
  CHECK(ok);
  CHECK(err.empty());
  CHECK_FALSE(is_legacy);
  CHECK(loaded_path == temp_toml);
  CHECK(config.core.machine == MachineType_t::Apple2eEnhanced);
  CHECK(config.core.emulation_speed == doctest::Approx(2.0));
  CHECK(config.video.video_standard == VideoStandard_t::PAL);
  CHECK(config.video.video_emulation == VideoEmulation_t::ColorTvEmulation);
  CHECK(config.slots.cards[1] == PeripheralCardType_t::ParallelPrinter);
  CHECK(config.slots.cards[6] == PeripheralCardType_t::DiskII);

  unlink(temp_toml.c_str());
}

TEST_CASE("ConfigDiscovery: config_load_active with Legacy Conf") {
  const std::string temp_conf = "/tmp/test_linapple_active_legacy.conf";
  unlink(temp_conf.c_str());

  {
    std::ofstream out(temp_conf);
    out << "[Configuration]\nMachine Type = 3\n";
  }

  LinAppleConfig_t config;
  std::string loaded_path;
  bool is_legacy = false;
  std::string err;

  bool ok =
      config_load_active(temp_conf, &config, &loaded_path, &is_legacy, &err);
  CHECK(ok);
  CHECK(err.empty());
  CHECK(is_legacy);
  CHECK(loaded_path == temp_conf);
  // Defensively initialized to defaults
  CHECK(config.core.machine == MachineType_t::Apple2eEnhanced);
  CHECK(config.core.emulation_speed == doctest::Approx(1.0));

  unlink(temp_conf.c_str());
}

TEST_CASE(
    "ConfigDiscovery: Malformed TOML Syntax Fails Immediately Without "
    "Fall-Through") {
  const std::string temp_bad = "/tmp/test_linapple_malformed.toml";
  unlink(temp_bad.c_str());

  {
    std::ofstream out(temp_bad);
    out << "[Core\nInvalid TOML syntax here...\n";
  }

  LinAppleConfig_t config;
  std::string loaded_path;
  bool is_legacy = false;
  std::string err;

  ConfigSearchOptions_t options;
  options.cli_override = temp_bad;
  options.create_default_if_missing = true;

  bool ok =
      config_load_active(options, &config, &loaded_path, &is_legacy, &err);
  CHECK_FALSE(ok);
  CHECK_FALSE(err.empty());
  CHECK(err.find("Failed to parse TOML") != std::string::npos);

  unlink(temp_bad.c_str());
}

TEST_CASE("ConfigDiscovery: Schema Validation Failure Fails Immediately") {
  const std::string temp_bad_schema = "/tmp/test_linapple_bad_schema.toml";
  unlink(temp_bad_schema.c_str());

  {
    std::ofstream out(temp_bad_schema);
    out << "[Core]\nEmulationSpeed = -5.0\n";
  }

  LinAppleConfig_t config;
  std::string loaded_path;
  bool is_legacy = false;
  std::string err;

  ConfigSearchOptions_t options;
  options.cli_override = temp_bad_schema;
  options.create_default_if_missing = true;

  bool ok =
      config_load_active(options, &config, &loaded_path, &is_legacy, &err);
  CHECK_FALSE(ok);
  CHECK_FALSE(err.empty());
  CHECK(err.find("validation failed") != std::string::npos);

  unlink(temp_bad_schema.c_str());
}

TEST_CASE(
    "ConfigDiscovery: Missing File When create_default_if_missing is False") {
  ConfigSearchOptions_t options;
  options.cli_override = "";
  options.create_default_if_missing = false;

  LinAppleConfig_t config;
  std::string loaded_path;
  bool is_legacy = false;
  std::string err;

  // Assuming current dir doesn't have linapple.toml, or if it does, test with
  // missing cli_override:
  options.cli_override = "/tmp/nonexistent_linapple_never_created_9999.toml";
  bool ok =
      config_load_active(options, &config, &loaded_path, &is_legacy, &err);
  CHECK_FALSE(ok);
  CHECK_FALSE(err.empty());
  CHECK(err.find("not found or inaccessible") != std::string::npos);
}

TEST_CASE("ConfigDiscovery: Defensive Checks") {
  CHECK_FALSE(config_load_active("/tmp/nonexistent.toml", nullptr));
  LinAppleConfig_t config;
  std::string err;
  CHECK_FALSE(config_load_active("/tmp/nonexistent_file_path_1234.toml",
                                 &config, nullptr, nullptr, &err));
  CHECK_FALSE(err.empty());
}
