// SPDX-License-Identifier: GPL-2.0-only
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/Peripheral_Types.h"
#include "core/config/ConfigSchema.h"
#include "core/config/Toml.h"
#include "doctest.h"

TEST_CASE("ConfigSchema: MachineType Conversions") {
  MachineType_t mach;

  CHECK(machine_type_from_string("Apple //e Enhanced", &mach));
  CHECK(mach == MachineType_t::Apple2eEnhanced);
  CHECK(std::string(machine_type_to_string(mach)) == "Apple //e Enhanced");

  CHECK(machine_type_from_string("apple2e_enhanced", &mach));
  CHECK(mach == MachineType_t::Apple2eEnhanced);

  CHECK(machine_type_from_string("Apple //e", &mach));
  CHECK(mach == MachineType_t::Apple2e);

  CHECK(machine_type_from_string("Apple ][+", &mach));
  CHECK(mach == MachineType_t::Apple2Plus);

  CHECK(machine_type_from_string("Apple ][", &mach));
  CHECK(mach == MachineType_t::Apple2);

  CHECK(machine_type_from_string("Pravets 82", &mach));
  CHECK(mach == MachineType_t::ClonePravets82);

  CHECK(machine_type_from_string("Base 64A", &mach));
  CHECK(mach == MachineType_t::CloneBase64A);

  // Numeric codes are rejected in modern TOML parser (handled by migration
  // tooling)
  CHECK_FALSE(machine_type_from_string("3", &mach));
  CHECK_FALSE(machine_type_from_string("2", &mach));

  // Invalid string
  CHECK_FALSE(machine_type_from_string("Commodore64", &mach));
  CHECK_FALSE(machine_type_from_string("", nullptr));
}

TEST_CASE("ConfigSchema: Video Enums Conversions") {
  VideoStandard_t std_val;
  CHECK(video_standard_from_string("NTSC", &std_val));
  CHECK(std_val == VideoStandard_t::NTSC);
  CHECK(video_standard_from_string("pal", &std_val));
  CHECK(std_val == VideoStandard_t::PAL);
  CHECK(std::string(video_standard_to_string(std_val)) == "PAL");

  VideoEmulation_t emu_val;
  CHECK(video_emulation_from_string("Color Standard", &emu_val));
  CHECK(emu_val == VideoEmulation_t::ColorStandard);
  CHECK(video_emulation_from_string("monochrome_amber", &emu_val));
  CHECK(emu_val == VideoEmulation_t::MonochromeAmber);
  CHECK(video_emulation_from_string("Monochrome Green", &emu_val));
  CHECK(emu_val == VideoEmulation_t::MonochromeGreen);
  CHECK_FALSE(video_emulation_from_string("Green", &emu_val));
  CHECK(video_emulation_from_string("Color TV Emulation", &emu_val));
  CHECK(emu_val == VideoEmulation_t::ColorTvEmulation);
}

TEST_CASE("ConfigSchema: Peripheral Card Conversions") {
  PeripheralCardType_t card;
  CHECK(peripheral_card_from_string("Disk II", &card));
  CHECK(card == PeripheralCardType_t::DiskII);
  CHECK(peripheral_card_from_string("Mockingboard", &card));
  CHECK(card == PeripheralCardType_t::Mockingboard);
  CHECK(peripheral_card_from_string("Parallel Printer", &card));
  CHECK(card == PeripheralCardType_t::ParallelPrinter);
  CHECK(peripheral_card_from_string("Super Serial Card", &card));
  CHECK(card == PeripheralCardType_t::SuperSerial);
  CHECK(peripheral_card_from_string("Harddisk", &card));
  CHECK(card == PeripheralCardType_t::Harddisk);
  CHECK(peripheral_card_from_string("Mouse", &card));
  CHECK(card == PeripheralCardType_t::Mouse);
  CHECK(peripheral_card_from_string("Mouse Interface", &card));
  CHECK(card == PeripheralCardType_t::Mouse);
  CHECK(peripheral_card_from_string("Clock", &card));
  CHECK(card == PeripheralCardType_t::Clock);
  CHECK(peripheral_card_from_string("None", &card));
  CHECK(card == PeripheralCardType_t::Empty);
  CHECK(peripheral_card_from_string("", &card));
  CHECK(card == PeripheralCardType_t::Empty);
}

TEST_CASE("ConfigSchema: Audio and Joystick Conversions") {
  SoundcardType_t sc;
  CHECK(soundcard_type_from_string("Mockingboard", &sc));
  CHECK(sc == SoundcardType_t::Mockingboard);
  CHECK(soundcard_type_from_string("Phasor", &sc));
  CHECK(sc == SoundcardType_t::Phasor);
  CHECK(soundcard_type_from_string("None", &sc));
  CHECK(sc == SoundcardType_t::None);

  JoystickMode_t joy;
  CHECK(joystick_mode_from_string("KeyboardStandard", &joy));
  CHECK(joy == JoystickMode_t::KeyboardStandard);
  CHECK(joystick_mode_from_string("HostGamepad", &joy));
  CHECK(joy == JoystickMode_t::HostGamepad);
  CHECK(joystick_mode_from_string("Disabled", &joy));
  CHECK(joy == JoystickMode_t::Disabled);

  QuickSaveModifier_t qs;
  CHECK(quick_save_modifier_from_string("Alt", &qs));
  CHECK(qs == QuickSaveModifier_t::Alt);
  CHECK(quick_save_modifier_from_string("Ctrl", &qs));
  CHECK(qs == QuickSaveModifier_t::Ctrl);
  CHECK(quick_save_modifier_from_string("AltCtrl", &qs));
  CHECK(qs == QuickSaveModifier_t::AltCtrl);
}

TEST_CASE("ConfigSchema: Default Configuration and Validation") {
  LinAppleConfig_t config = config_defaults();
  std::vector<ConfigValidationError_t> errors;

  CHECK(config_validate(config, &errors) == true);
  CHECK(errors.empty());

  // Check default settings
  CHECK(config.core.machine == MachineType_t::Apple2eEnhanced);
  CHECK(config.core.emulation_speed == doctest::Approx(1.0));
  CHECK(config.core.boot_on_startup == true);
  CHECK(config.core.enable_debugger == true);
  CHECK(config.video.video_standard == VideoStandard_t::NTSC);
  CHECK(config.video.video_emulation == VideoEmulation_t::ColorStandard);
  CHECK(config.video.monochrome_color == "#C0C0C0");
  CHECK(config.audio.speaker_volume == 50);
  CHECK(config.audio.mockingboard_volume == 50);
  CHECK(config.slots.cards[6] == PeripheralCardType_t::DiskII);
  CHECK(config.slots.cards[7] == PeripheralCardType_t::Harddisk);
}

TEST_CASE("ConfigSchema: Validation Bounds Checks") {
  LinAppleConfig_t config = config_defaults();

  SUBCASE("Invalid Emulation Speed") {
    config.core.emulation_speed = -1.0;
    std::vector<ConfigValidationError_t> errors;
    CHECK_FALSE(config_validate(config, &errors));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].section == "Core");
    CHECK(errors[0].key == "EmulationSpeed");
    CHECK(errors[0].message ==
          "EmulationSpeed must be between 0.01 and 100.0x");
  }

  SUBCASE("Invalid Screen Factor") {
    config.video.screen_factor = 0.05;
    std::vector<ConfigValidationError_t> errors;
    CHECK_FALSE(config_validate(config, &errors));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].section == "Video");
    CHECK(errors[0].key == "ScreenFactor");
    CHECK(errors[0].message == "ScreenFactor must be between 0.1 and 10.0");
  }

  SUBCASE("Invalid Monochrome Color") {
    config.video.monochrome_color = "not-a-color";
    std::vector<ConfigValidationError_t> errors;
    CHECK_FALSE(config_validate(config, &errors));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].section == "Video");
    CHECK(errors[0].key == "MonochromeColor");
    CHECK(
        errors[0].message ==
        "MonochromeColor must be a valid hex RGB or RGBA code (e.g. #C0C0C0)");
  }

  SUBCASE("Invalid Volume") {
    config.audio.speaker_volume = 150;
    std::vector<ConfigValidationError_t> errors;
    CHECK_FALSE(config_validate(config, &errors));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].section == "Audio");
    CHECK(errors[0].key == "SpeakerVolume");
    CHECK(errors[0].message == "SpeakerVolume must be between 0 and 100");
  }

  SUBCASE("Invalid Slot 0 Card") {
    config.slots.cards[0] = PeripheralCardType_t::DiskII;
    std::vector<ConfigValidationError_t> errors;
    CHECK_FALSE(config_validate(config, &errors));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].section == "Slots");
    CHECK(errors[0].key == "Slot0");
    CHECK(errors[0].message ==
          "Slot 0 is reserved for motherboard memory/firmware and cannot "
          "hold expansion cards");
  }

  SUBCASE("Invalid Paddle Trim") {
    config.joystick.pdl_x_trim = 300;
    std::vector<ConfigValidationError_t> errors;
    CHECK_FALSE(config_validate(config, &errors));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].section == "Joystick");
    CHECK(errors[0].key == "PDLXTrim");
    CHECK(errors[0].message == "PDLXTrim must be between -128 and 127");
  }
}

TEST_CASE("ConfigSchema: Parse from TOML Document") {
  const std::string toml_sample = R"(
[Core]
Machine = "Apple //e"
EmulationSpeed = 2.0x
EnhanceDiskSpeed = false
BootOnStartup = true
EnableDebugger = false

[Video]
VideoStandard = PAL
VideoEmulation = "Monochrome Amber"
MonochromeColor = #FFB000
ScreenFactor = 2.0
Fullscreen = true
Multithreaded = false
ShowLeds = false

[Audio]
SpeakerVolume = 25
MockingboardVolume = 30
SoundcardType = Phasor

[Keyboard]
KeyboardType = UK
MappingMode = Positional
CapsLockMode = Emulated

[Slots]
Slot1 = ParallelPrinter
Slot2 = SuperSerial
Slot4 = Mockingboard
Slot6 = DiskII
Slot7 = Harddisk

[Peripheral.DiskII.Slot6]
Drive1 = "/path/to/my_game.dsk"
)";

  std::string parse_err;
  auto doc = toml_document_parse(toml_sample, &parse_err);
  REQUIRE(doc != nullptr);

  LinAppleConfig_t config;
  std::vector<ConfigValidationError_t> errors;
  CHECK(config_from_toml(doc.get(), &config, &errors));
  CHECK(errors.empty());

  CHECK(config.core.machine == MachineType_t::Apple2e);
  CHECK(config.core.emulation_speed == doctest::Approx(2.0));
  CHECK(config.core.enhance_disk_speed == false);
  CHECK(config.core.boot_on_startup == true);
  CHECK(config.core.enable_debugger == false);

  CHECK(config.video.video_standard == VideoStandard_t::PAL);
  CHECK(config.video.video_emulation == VideoEmulation_t::MonochromeAmber);
  CHECK(config.video.monochrome_color == "#FFB000");
  CHECK(config.video.screen_factor == doctest::Approx(2.0));
  CHECK(config.video.fullscreen == true);
  CHECK(config.video.multithreaded == false);
  CHECK(config.video.show_leds == false);

  CHECK(config.audio.speaker_volume == 25);
  CHECK(config.audio.mockingboard_volume == 30);
  CHECK(config.audio.soundcard_type == SoundcardType_t::Phasor);

  CHECK(config.keyboard.layout == ConfigKeyboardLayout_t::UK);
  CHECK(config.keyboard.mapping_mode == KeyMappingMode_t::Positional);
  CHECK(config.keyboard.caps_lock_mode == ConfigCapsLockMode_t::Emulated);

  CHECK(config.slots.cards[1] == PeripheralCardType_t::ParallelPrinter);
  CHECK(config.slots.cards[6] == PeripheralCardType_t::DiskII);

  // Raw doc preservation
  REQUIRE(config.raw_doc != nullptr);
  const auto* slot6_table =
      toml_find_table(config.raw_doc.get(), "Peripheral.DiskII.Slot6");
  REQUIRE(slot6_table != nullptr);
  CHECK(toml_table_get_string(slot6_table, "Drive1") == "/path/to/my_game.dsk");
}

TEST_CASE("ConfigSchema: Round-Trip TOML Serialization") {
  LinAppleConfig_t config = config_defaults();
  config.core.machine = MachineType_t::Apple2Plus;
  config.core.emulation_speed = 3.5;
  config.video.video_standard = VideoStandard_t::PAL;
  config.video.monochrome_color = "#33FF33";
  config.audio.speaker_volume = 20;

  // Serialize to TOML
  auto toml_doc = config_to_toml(config);
  REQUIRE(toml_doc != nullptr);

  const std::string serialized = toml_document_serialize(toml_doc.get());
  CHECK(serialized.find("[Core]") != std::string::npos);
  CHECK(serialized.find("Machine = \"Apple ][+\"") != std::string::npos);

  // Parse back
  std::string err;
  auto reparsed_doc = toml_document_parse(serialized, &err);
  REQUIRE(reparsed_doc != nullptr);

  LinAppleConfig_t reparsed_config;
  std::vector<ConfigValidationError_t> validation_errors;
  CHECK(config_from_toml(reparsed_doc.get(), &reparsed_config,
                         &validation_errors));
  CHECK(validation_errors.empty());

  CHECK(reparsed_config.core.machine == MachineType_t::Apple2Plus);
  CHECK(reparsed_config.core.emulation_speed == doctest::Approx(3.5));
  CHECK(reparsed_config.video.video_standard == VideoStandard_t::PAL);
  CHECK(reparsed_config.video.monochrome_color == "#33FF33");
  CHECK(reparsed_config.audio.speaker_volume == 20);
}

TEST_CASE("ConfigSchema: Quick Save Modifier Aliases") {
  QuickSaveModifier_t qs;
  CHECK(quick_save_modifier_from_string("control", &qs));
  CHECK(qs == QuickSaveModifier_t::Ctrl);
  CHECK(quick_save_modifier_from_string("Disabled", &qs));
  CHECK(qs == QuickSaveModifier_t::Disabled);
  CHECK(quick_save_modifier_from_string("none", &qs));
  CHECK(qs == QuickSaveModifier_t::Disabled);
  CHECK_FALSE(quick_save_modifier_from_string("0", &qs));
  CHECK_FALSE(quick_save_modifier_from_string("off", &qs));
}

TEST_CASE("ConfigSchema: Reject Slot 0 in TOML Document") {
  const std::string bad_slot0_toml = R"(
[Slots]
Slot0 = "DiskII"
Slot6 = "DiskII"
)";

  std::string err;
  auto doc = toml_document_parse(bad_slot0_toml, &err);
  REQUIRE(doc != nullptr);

  LinAppleConfig_t config;
  std::vector<ConfigValidationError_t> errors;
  CHECK_FALSE(config_from_toml(doc.get(), &config, &errors));
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].section == "Slots");
  CHECK(errors[0].key == "Slot0");
  CHECK(errors[0].message ==
        "Slot 0 is reserved for motherboard memory/firmware and cannot "
        "hold expansion cards");
}

TEST_CASE("ConfigSchema: Joystick Axes Round-Trip") {
  LinAppleConfig_t config = config_defaults();
  config.joystick.joy0_axis0 = 3;
  config.joystick.joy0_axis1 = 4;
  config.joystick.joy1_axis0 = 5;
  config.joystick.joy1_axis1 = 6;

  auto toml_doc = config_to_toml(config);
  REQUIRE(toml_doc != nullptr);

  const std::string serialized = toml_document_serialize(toml_doc.get());
  CHECK(serialized.find("Joy0Axis0 = 3") != std::string::npos);
  CHECK(serialized.find("Joy0Axis1 = 4") != std::string::npos);
  CHECK(serialized.find("Joy1Axis0 = 5") != std::string::npos);
  CHECK(serialized.find("Joy1Axis1 = 6") != std::string::npos);

  std::string err;
  auto reparsed_doc = toml_document_parse(serialized, &err);
  REQUIRE(reparsed_doc != nullptr);

  LinAppleConfig_t reparsed_config;
  std::vector<ConfigValidationError_t> validation_errors;
  CHECK(config_from_toml(reparsed_doc.get(), &reparsed_config,
                         &validation_errors));
  CHECK(validation_errors.empty());

  CHECK(reparsed_config.joystick.joy0_axis0 == 3);
  CHECK(reparsed_config.joystick.joy0_axis1 == 4);
  CHECK(reparsed_config.joystick.joy1_axis0 == 5);
  CHECK(reparsed_config.joystick.joy1_axis1 == 6);
}

TEST_CASE(
    "ConfigSchema: Backwards Compatibility with Legacy Configuration Block") {
  const std::string legacy_toml = R"(
[Configuration]
Computer Emulation = "Apple //e Enhanced"
Emulation Speed = 20
Enhance Disk Speed = 0
Boot at Startup = 1
Disable Debugger = 1
Video Standard = "PAL"
Video Emulation = "Color Text Optimized"
Monochrome Color = "#33FF33"
Screen factor = 1.5
Singlethreaded = 1
Soundcard Type = "Phasor"
Keyboard Type = "UK"
Keyboard Rocker Switch = "Local"
Mapping Mode = "Positional"
Caps Lock Mode = "Emulated"
Joystick 0 Axis 0 = 2
Joystick 0 Axis 1 = 3
Joystick 1 Axis 0 = 4
Joystick 1 Axis 1 = 5
)";

  std::string err;
  auto doc = toml_document_parse(legacy_toml, &err);
  REQUIRE(doc != nullptr);

  LinAppleConfig_t config;
  std::vector<ConfigValidationError_t> errors;
  CHECK(config_from_toml(doc.get(), &config, &errors));
  CHECK(errors.empty());

  CHECK(config.core.machine == MachineType_t::Apple2eEnhanced);
  CHECK(config.core.emulation_speed == doctest::Approx(2.0));
  CHECK(config.core.enhance_disk_speed == false);
  CHECK(config.core.boot_on_startup == true);
  CHECK(config.core.enable_debugger == false);

  CHECK(config.video.video_standard == VideoStandard_t::PAL);
  CHECK(config.video.video_emulation == VideoEmulation_t::ColorTextOptimized);
  CHECK(config.video.monochrome_color == "#33FF33");
  CHECK(config.video.screen_factor == doctest::Approx(1.5));
  CHECK(config.video.multithreaded == false);

  CHECK(config.audio.soundcard_type == SoundcardType_t::Phasor);
  CHECK(config.keyboard.layout == ConfigKeyboardLayout_t::UK);
  CHECK(config.keyboard.rocker_switch == KeyboardRocker_t::Local);
  CHECK(config.keyboard.mapping_mode == KeyMappingMode_t::Positional);
  CHECK(config.keyboard.caps_lock_mode == ConfigCapsLockMode_t::Emulated);

  CHECK(config.joystick.joy0_axis0 == 2);
  CHECK(config.joystick.joy0_axis1 == 3);
  CHECK(config.joystick.joy1_axis0 == 4);
  CHECK(config.joystick.joy1_axis1 == 5);
}

TEST_CASE("ConfigSchema: Modern Namespaced Sections [Frontend] and [Input]") {
  const std::string modern_toml = R"(
[Frontend]
ScreenFactor = 2.5
Fullscreen = true
Multithreaded = true
ShowLeds = false
RenderMode = "Block"

[Input]
KeyboardType = "German QWERTZ"
KeyboardRockerSwitch = "Local"
MappingMode = "Symbolic"
CapsLockMode = "Host"
QuickSaveModifier = "AltCtrl"
EnableHotkeys = true
ScrollLockToggle = false
Joystick0Mode = "Host Gamepad"
Joystick1Mode = "Disabled"
Joy0Index = 2
Joy0Axis0 = 4
Joy0Axis1 = 5
)";

  std::string err;
  auto doc = toml_document_parse(modern_toml, &err);
  REQUIRE(doc != nullptr);

  LinAppleConfig_t config;
  std::vector<ConfigValidationError_t> errors;
  CHECK(config_from_toml(doc.get(), &config, &errors));
  CHECK(errors.empty());

  CHECK(config.frontend.screen_factor == doctest::Approx(2.5));
  CHECK(config.frontend.fullscreen == true);
  CHECK(config.frontend.render_mode == ConfigTuiRenderMode_t::Block);

  // Backward compatibility mirrors
  CHECK(config.video.screen_factor == doctest::Approx(2.5));
  CHECK(config.video.fullscreen == true);
  CHECK(config.video.tui_render_mode == ConfigTuiRenderMode_t::Block);

  CHECK(config.input.keyboard_type == ConfigKeyboardLayout_t::German);
  CHECK(config.input.quick_save_modifier == QuickSaveModifier_t::AltCtrl);
  CHECK(config.input.joy0_mode == JoystickMode_t::HostGamepad);
  CHECK(config.input.joy0_index == 2);
  CHECK(config.input.joy0_axis0 == 4);
  CHECK(config.input.joy0_axis1 == 5);

  CHECK(config.keyboard.layout == ConfigKeyboardLayout_t::German);
  CHECK(config.joystick.joy0_mode == JoystickMode_t::HostGamepad);
  CHECK(config.joystick.joy0_index == 2);
  CHECK(config.joystick.joy0_axis0 == 4);
  CHECK(config.joystick.joy0_axis1 == 5);
}

TEST_CASE("ConfigSchema: Schema-Driven Peripheral Documentation") {
  const char* const allowed[] = {"Auto", "US", "UK", nullptr};
  PeripheralConfigOption_t opt = {"TestOpt", "Test option description",
                                  "Auto",    peripheral_config_enum,
                                  allowed,   0,
                                  0};
  const std::string comment = peripheral_format_option_comment(opt);
  CHECK(comment ==
        "Test option description:\n  Allowed: Auto, US, UK\n  Default: Auto");

  auto doc = toml_document_create();
  peripheral_populate_toml_table(doc.get(), "Peripheral.DiskII",
                                 "linapple.disk_II");
  const auto* tbl = toml_find_table(doc.get(), "Peripheral.DiskII");
  REQUIRE(tbl != nullptr);
  CHECK(toml_table_get_bool(tbl, "FastDisk") == true);
  CHECK(doc->section_comments.count("Peripheral.DiskII") == 1);

  const std::string serialized = toml_document_serialize(doc.get());
  CHECK(serialized.find("[Peripheral.DiskII]") != std::string::npos);
  CHECK(serialized.find("# Apple II floppy disk controller emulation") !=
        std::string::npos);
  CHECK(serialized.find("FastDisk = true") != std::string::npos);
}
