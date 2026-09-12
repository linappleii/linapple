// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <limits>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "core/config/ConfigDispatch.h"
#include "core/config/ConfigSchema.h"
#include "core/config/Toml.h"
#include "doctest.h"

namespace {

struct ScopedDispatchHarness_t {
  ScopedDispatchHarness_t()
      : saved_apple2_type_(g_apple2_type),
        saved_state_(g_state),
        saved_videotype_(g_videotype),
        saved_monochrome_(monochrome),
        saved_show_leds_(g_show_leds),
        saved_singlethreaded_(g_singlethreaded) {
    peripheral_manager_init();
  }

  ~ScopedDispatchHarness_t() {
    peripheral_manager_shutdown();
    g_apple2_type = saved_apple2_type_;
    g_state = saved_state_;
    g_videotype = saved_videotype_;
    monochrome = saved_monochrome_;
    g_show_leds = saved_show_leds_;
    g_singlethreaded = saved_singlethreaded_;
  }

  eApple2Type saved_apple2_type_;
  SystemState_t saved_state_;
  uint32_t saved_videotype_;
  uint32_t saved_monochrome_;
  bool saved_show_leds_;
  uint32_t saved_singlethreaded_;
};

}  // namespace

TEST_CASE("ConfigDispatch: Card Type Mappings") {
  const char* id = nullptr;
  const char* sec = nullptr;

  id = config_card_type_to_id(PeripheralCardType_t::ParallelPrinter);
  REQUIRE(id != nullptr);
  CHECK(std::string(id) == "linapple.printer");

  id = config_card_type_to_id(PeripheralCardType_t::SuperSerial);
  REQUIRE(id != nullptr);
  CHECK(std::string(id) == "linapple.ssc");

  id = config_card_type_to_id(PeripheralCardType_t::Mockingboard);
  REQUIRE(id != nullptr);
  CHECK(std::string(id) == "linapple.mockingboard");

  id = config_card_type_to_id(PeripheralCardType_t::DiskII);
  REQUIRE(id != nullptr);
  CHECK(std::string(id) == "linapple.disk_II");

  id = config_card_type_to_id(PeripheralCardType_t::Harddisk);
  REQUIRE(id != nullptr);
  CHECK(std::string(id) == "linapple.harddisk");

  id = config_card_type_to_id(PeripheralCardType_t::Mouse);
  REQUIRE(id != nullptr);
  CHECK(std::string(id) == "linapple.mouse");

  id = config_card_type_to_id(PeripheralCardType_t::Clock);
  REQUIRE(id != nullptr);
  CHECK(std::string(id) == "linapple.clock");

  CHECK(config_card_type_to_id(PeripheralCardType_t::Empty) == nullptr);
  CHECK(config_card_type_to_id(static_cast<PeripheralCardType_t>(999)) ==
        nullptr);

  sec = config_card_type_to_section(PeripheralCardType_t::ParallelPrinter);
  REQUIRE(sec != nullptr);
  CHECK(std::string(sec) == "Printer");

  sec = config_card_type_to_section(PeripheralCardType_t::SuperSerial);
  REQUIRE(sec != nullptr);
  CHECK(std::string(sec) == "SuperSerial");

  sec = config_card_type_to_section(PeripheralCardType_t::Mockingboard);
  REQUIRE(sec != nullptr);
  CHECK(std::string(sec) == "Mockingboard");

  sec = config_card_type_to_section(PeripheralCardType_t::DiskII);
  REQUIRE(sec != nullptr);
  CHECK(std::string(sec) == "DiskII");

  sec = config_card_type_to_section(PeripheralCardType_t::Harddisk);
  REQUIRE(sec != nullptr);
  CHECK(std::string(sec) == "Harddisk");

  sec = config_card_type_to_section(PeripheralCardType_t::Mouse);
  REQUIRE(sec != nullptr);
  CHECK(std::string(sec) == "Mouse");

  sec = config_card_type_to_section(PeripheralCardType_t::Clock);
  REQUIRE(sec != nullptr);
  CHECK(std::string(sec) == "Clock");

  CHECK(config_card_type_to_section(PeripheralCardType_t::Empty) == nullptr);
  CHECK(config_card_type_to_section(static_cast<PeripheralCardType_t>(999)) ==
        nullptr);
}

TEST_CASE("ConfigDispatch: Hierarchical Precedence Resolution") {
  const std::string toml_data = R"(
[Peripheral.DiskII]
FastDisk = false
Drive1 = "/common/default1.dsk"
Drive2 = "/common/default2.dsk"

[Peripheral.DiskII.Slot6]
Drive1 = "/slot6/override1.dsk"
)";

  std::string parse_err;
  auto doc = toml_document_parse(toml_data, &parse_err);
  REQUIRE(doc != nullptr);
  REQUIRE(parse_err.empty());

  const PeripheralConfigOption_t opt_drive1 = {"Drive1",
                                               "First drive image",
                                               "/fallback.dsk",
                                               peripheral_config_filepath,
                                               nullptr,
                                               0,
                                               0};
  const PeripheralConfigOption_t opt_drive2 = {"Drive2",
                                               "Second drive image",
                                               "/fallback.dsk",
                                               peripheral_config_filepath,
                                               nullptr,
                                               0,
                                               0};
  const PeripheralConfigOption_t opt_fast = {"FastDisk", "Speed up disk access",
                                             "true",     peripheral_config_bool,
                                             nullptr,    0,
                                             1};
  const PeripheralConfigOption_t opt_missing = {"MissingOption",
                                                "Unset option",
                                                "default_val",
                                                peripheral_config_string,
                                                nullptr,
                                                0,
                                                0};

  std::string resolved;

  // Precedence 1: Slot-specific override takes top priority
  bool found = config_resolve_peripheral_option(
      doc.get(), "DiskII", "linapple.disk_II", 6, opt_drive1, &resolved);
  CHECK(found);
  CHECK(resolved == "/slot6/override1.dsk");

  // Precedence 2: Base table takes priority when slot override is absent
  found = config_resolve_peripheral_option(
      doc.get(), "DiskII", "linapple.disk_II", 6, opt_drive2, &resolved);
  CHECK(found);
  CHECK(resolved == "/common/default2.dsk");

  found = config_resolve_peripheral_option(
      doc.get(), "DiskII", "linapple.disk_II", 6, opt_fast, &resolved);
  CHECK(found);
  CHECK(resolved == "false");

  // Precedence 3: Schema default is used when not in TOML at all
  found = config_resolve_peripheral_option(
      doc.get(), "DiskII", "linapple.disk_II", 6, opt_missing, &resolved);
  CHECK(found);
  CHECK(resolved == "default_val");

  // Different slot (e.g. Slot 5) does not see Slot 6 override
  found = config_resolve_peripheral_option(
      doc.get(), "DiskII", "linapple.disk_II", 5, opt_drive1, &resolved);
  CHECK(found);
  CHECK(resolved == "/common/default1.dsk");
}

TEST_CASE("ConfigDispatch: Case-Insensitive and Dotted Slot Resolution") {
  const std::string toml_data = R"(
[slot6.printer]
filename = "/output/slot6_print.txt"
idlelimit = 30
)";

  std::string parse_err;
  auto doc = toml_document_parse(toml_data, &parse_err);
  REQUIRE(doc != nullptr);
  REQUIRE(parse_err.empty());

  const PeripheralConfigOption_t opt_filename = {"Filename",
                                                 "Output file",
                                                 "Default.txt",
                                                 peripheral_config_filepath,
                                                 nullptr,
                                                 0,
                                                 0};
  const PeripheralConfigOption_t opt_idle = {
      "IdleLimit", "Idle limit", "10", peripheral_config_int, nullptr, 0, 3600};

  std::string resolved;
  bool found = config_resolve_peripheral_option(
      doc.get(), "Printer", "linapple.printer", 6, opt_filename, &resolved);
  CHECK(found);
  CHECK(resolved == "/output/slot6_print.txt");

  found = config_resolve_peripheral_option(
      doc.get(), "Printer", "linapple.printer", 6, opt_idle, &resolved);
  CHECK(found);
  CHECK(resolved == "30");
}

TEST_CASE("ConfigDispatch: Core Subsystem Dispatch") {
  const ScopedDispatchHarness_t scoped_harness;

  CoreConfig_t core;
  core.machine = MachineType_t::Apple2Plus;
  core.emulation_speed = 2.5;

  CHECK(config_dispatch_core(core));
  CHECK(g_apple2_type == A2TYPE_APPLE2PLUS);
  CHECK(g_state.speed == 25);

  core.machine = MachineType_t::Apple2eEnhanced;
  core.emulation_speed = 1.0;
  CHECK(config_dispatch_core(core));
  CHECK(g_apple2_type == A2TYPE_APPLE2EENHANCED);
  CHECK(g_state.speed == 10);
}

TEST_CASE("ConfigDispatch: Video Subsystem Dispatch") {
  const ScopedDispatchHarness_t scoped_harness;

  VideoConfig_t video;
  video.video_standard = VideoStandard_t::PAL;
  video.video_emulation = VideoEmulation_t::ColorTvEmulation;
  video.fullscreen = false;
  video.screen_factor = 2.0;

  CHECK(config_dispatch_video(video));
  CHECK(g_state.video_scanner_ntsc == false);
  CHECK(g_state.clks_per_frame == 20280);
  CHECK(g_videotype == VT_COLOR_TVEMU);
  CHECK(g_state.screen_width == static_cast<uint32_t>(SCREEN_WIDTH * 2.0));
  CHECK(g_state.screen_height == static_cast<uint32_t>(SCREEN_HEIGHT * 2.0));

  // Monochrome custom color test
  video.video_standard = VideoStandard_t::NTSC;
  video.video_emulation = VideoEmulation_t::MonochromeCustom;
  video.monochrome_color = "#33FF33";
  CHECK(config_dispatch_video(video));
  CHECK(g_state.video_scanner_ntsc == true);
  CHECK(g_state.clks_per_frame == 17030);
  CHECK(g_videotype == VT_MONO_CUSTOM);
  CHECK(monochrome == RGB(0x33, 0xFF, 0x33));
}

TEST_CASE("ConfigDispatch: Slot Dispatch Engine") {
  const ScopedDispatchHarness_t scoped_harness;

  const std::string toml_data = R"(
[Peripheral.Printer.Slot1]
Filename = "CustomPrinter.txt"
IdleLimit = 45
Append = false

[Peripheral.DiskII.Slot6]
FastDisk = true
)";

  LinAppleConfig_t config;
  config.slots.cards.fill(PeripheralCardType_t::Empty);
  std::string parse_err;
  config.raw_doc = toml_document_parse(toml_data, &parse_err);
  REQUIRE(config.raw_doc != nullptr);
  config.slots.cards[1] = PeripheralCardType_t::ParallelPrinter;
  config.slots.cards[6] = PeripheralCardType_t::DiskII;

  ConfigDispatchResult_t result;
  int status = config_dispatch_slots(config, &result);
  CHECK(status == 0);
  CHECK(result.error_count == 0);
  CHECK(result.applied_options_count == 6);

  // Verify Slot 1 has Printer registered
  CHECK_FALSE(peripheral_is_slot_empty(1));
  const auto* p1 = peripheral_get_registered(1);
  REQUIRE(p1 != nullptr);
  CHECK(std::string(p1->id) == "linapple.printer");

  // Verify Slot 6 has Disk II registered
  CHECK_FALSE(peripheral_is_slot_empty(6));
  const auto* p6 = peripheral_get_registered(6);
  REQUIRE(p6 != nullptr);
  CHECK(std::string(p6->id) == "linapple.disk_II");

  // Empty slot 2 remains empty
  CHECK(peripheral_is_slot_empty(2));

  // Change Slot 1 to Empty and re-dispatch
  config.slots.cards[1] = PeripheralCardType_t::Empty;
  status = config_dispatch_slots(config, &result);
  CHECK(status == 0);
  CHECK(peripheral_is_slot_empty(1));

  // Slot 0 error check
  status =
      config_dispatch_slot(0, PeripheralCardType_t::DiskII, nullptr, &result);
  CHECK(status == -1);
  CHECK(result.error_count == 1);

  // Slot range error check
  status =
      config_dispatch_slot(8, PeripheralCardType_t::DiskII, nullptr, &result);
  CHECK(status == -1);
  CHECK(result.error_count == 2);

  status =
      config_dispatch_slot(-1, PeripheralCardType_t::DiskII, nullptr, &result);
  CHECK(status == -1);
  CHECK(result.error_count == 3);
}

TEST_CASE("ConfigDispatch: Full Config Dispatch All") {
  const ScopedDispatchHarness_t scoped_harness;

  LinAppleConfig_t config;
  config.slots.cards.fill(PeripheralCardType_t::Empty);
  config.core.machine = MachineType_t::Apple2eEnhanced;
  config.core.emulation_speed = 1.0;
  config.video.video_standard = VideoStandard_t::NTSC;
  config.video.video_emulation = VideoEmulation_t::ColorStandard;
  config.slots.cards[6] = PeripheralCardType_t::DiskII;

  ConfigDispatchResult_t result;
  int status = config_dispatch_all(config, &result);
  CHECK(status == 0);
  CHECK(result.error_count == 0);
  CHECK(g_apple2_type == A2TYPE_APPLE2EENHANCED);
  CHECK(g_state.speed == 10);
  CHECK(g_videotype == VT_COLOR_STANDARD);
  CHECK_FALSE(peripheral_is_slot_empty(6));
}

TEST_CASE("ConfigDispatch: Floating-point NaN and Inf Defensive Handling") {
  const ScopedDispatchHarness_t scoped_harness;

  CoreConfig_t core;
  core.emulation_speed = std::numeric_limits<double>::quiet_NaN();
  CHECK(config_dispatch_core(core));
  CHECK(g_state.speed == 10);

  core.emulation_speed = std::numeric_limits<double>::infinity();
  CHECK(config_dispatch_core(core));
  CHECK(g_state.speed == 10);

  VideoConfig_t video;
  const uint32_t prev_w = g_state.screen_width;
  const uint32_t prev_h = g_state.screen_height;

  video.screen_factor = std::numeric_limits<double>::quiet_NaN();
  CHECK(config_dispatch_video(video));
  CHECK(g_state.screen_width == prev_w);
  CHECK(g_state.screen_height == prev_h);

  video.screen_factor = std::numeric_limits<double>::infinity();
  CHECK(config_dispatch_video(video));
  CHECK(g_state.screen_width == prev_w);
  CHECK(g_state.screen_height == prev_h);
}

TEST_CASE("ConfigDispatch: Extra Keys and Alternative Table Syntax") {
  const ScopedDispatchHarness_t scoped_harness;

  const std::string toml_data = R"(
[Slot6.DiskII]
FastDisk = false

[DiskII]
Drive1 = "/custom/drive1.dsk"
)";

  LinAppleConfig_t config;
  config.slots.cards.fill(PeripheralCardType_t::Empty);
  std::string parse_err;
  config.raw_doc = toml_document_parse(toml_data, &parse_err);
  REQUIRE(config.raw_doc != nullptr);
  config.slots.cards[6] = PeripheralCardType_t::DiskII;

  ConfigDispatchResult_t result;
  int status = config_dispatch_slots(config, &result);
  CHECK(status == 0);
  CHECK(result.error_count == 0);
  CHECK(result.applied_options_count == 3);
}
