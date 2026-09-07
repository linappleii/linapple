// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>

#include <fstream>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/config/ConfigMigration.h"
#include "core/config/ConfigSchema.h"
#include "core/config/Toml.h"
#include "doctest.h"

static const char* SAMPLE_LEGACY_CONF = R"(
# Sample Legacy linapple.conf
[Configuration]
Computer Emulation = 3
Emulation Speed = 20
Enhance Disk Speed = 1
Boot at Startup = 1
Save State On Exit = 1
Disable Debugger = 1
Basic Live Sync File = /tmp/sync.bas
Basic Line Numbering = 1
TUI Render Mode = block
Video Standard = 1
Video Emulation = 3
Monochrome Color = #A0FFA0
Screen factor = 1.5
Screen Width = 800
Screen Height = 600
Fullscreen = 1
Singlethreaded = 1
Show Leds = 0
Sound Emulation = 1
Speaker Volume = 15
Mockingboard Volume = 31
Soundcard Type = 2
Keyboard Type = 3
Keyboard Rocker Switch = 1
Mapping Mode = 1
Caps Lock Mode = 1
Quick Save Modifier = Ctrl
Enable Hotkeys = 0
ScrollLock Toggle = 0
Joystick 0 = 1
Joystick 1 = 2
Parallel Printer Filename = /tmp/my_printer.txt
Printer idle limit = 20
Append to printer file = 0
Serial Port = /dev/ttyS0
Serial Baud = 19200
Serial Loopback = 1

[Slots]
Slot 1 = Parallel Printer
Slot 2 = Super Serial Card
Slot 3 = None
Slot 4 = Mockingboard
Slot 5 = Mockingboard
Slot 6 = Disk II
Slot 7 = Harddisk

[Preferences]
Disk Image 1 = /tmp/game_side_a.dsk
Disk Image 2 = /tmp/game_side_b.dsk
Harddisk Image 1 = /tmp/hd1.hdv
Harddisk Image 2 = /tmp/hd2.hdv
FTP Server = ftp://example.com/apple2/
Save State Filename = /tmp/quicksave.state

[Custom ROM]
Master = /tmp/custom_master.rom

[Keyboard.Custom]
W = Up
A = Left
S = Down
D = Right
)";

TEST_CASE("ConfigMigration: Migrate to TOML Document") {
  std::string err;
  auto doc = config_migrate_legacy_to_toml(SAMPLE_LEGACY_CONF, &err);
  REQUIRE(doc != nullptr);
  CHECK(err.empty());

  // Core
  auto* core = toml_find_table(doc.get(), "Core");
  REQUIRE(core != nullptr);
  CHECK(toml_table_get_string(core, "Machine") == "Apple //e Enhanced");
  CHECK(toml_table_get_double(core, "EmulationSpeed") == doctest::Approx(2.0));
  CHECK(toml_table_get_bool(core, "EnhanceDiskSpeed") == true);
  CHECK(toml_table_get_bool(core, "BootOnStartup") == true);
  CHECK(toml_table_get_bool(core, "SaveStateOnExit") == true);
  CHECK(toml_table_get_bool(core, "EnableDebugger") == false);
  CHECK(toml_table_get_string(core, "BasicLiveSyncFile") == "/tmp/sync.bas");
  CHECK(toml_table_get_string(core, "BasicLineNumbering") == "Positional");
  // Video
  auto* video = toml_find_table(doc.get(), "Video");
  REQUIRE(video != nullptr);
  CHECK(toml_table_get_string(video, "VideoStandard") == "PAL");
  CHECK(toml_table_get_string(video, "VideoEmulation") == "Color TV Emulation");
  CHECK(toml_table_get_string(video, "MonochromeColor") == "#A0FFA0");

  // Frontend
  auto* frontend = toml_find_table(doc.get(), "Frontend");
  REQUIRE(frontend != nullptr);
  CHECK(toml_table_get_double(frontend, "ScreenFactor") ==
        doctest::Approx(1.5));
  CHECK(toml_table_get_int(frontend, "ScreenWidth") == 800);
  CHECK(toml_table_get_int(frontend, "ScreenHeight") == 600);
  CHECK(toml_table_get_bool(frontend, "Fullscreen") == true);
  CHECK(toml_table_get_bool(frontend, "Multithreaded") == false);
  CHECK(toml_table_get_bool(frontend, "ShowLeds") == false);
  CHECK(toml_table_get_string(frontend, "RenderMode") == "Block");
  CHECK(toml_table_get_bool(frontend, "EnableHotkeys") == false);

  // Audio
  auto* audio = toml_find_table(doc.get(), "Audio");
  REQUIRE(audio != nullptr);
  CHECK(toml_table_get_bool(audio, "SoundEmulation") == true);
  CHECK(toml_table_get_int(audio, "SpeakerVolume") == 48);

  // Input
  auto* input = toml_find_table(doc.get(), "Input");
  REQUIRE(input != nullptr);
  CHECK(toml_table_get_string(input, "KeyboardType") == "French");
  CHECK(toml_table_get_string(input, "KeyboardRockerSwitch") == "Local");
  CHECK(toml_table_get_string(input, "MappingMode") == "Positional");
  CHECK(toml_table_get_string(input, "CapsLockMode") == "Emulated");
  CHECK(toml_table_get_string(input, "QuickSaveModifier") == "Ctrl");
  CHECK(toml_table_get_bool(input, "ScrollLockToggle") == false);
  CHECK(toml_table_get_string(input, "Joystick0Mode") == "Host Gamepad");
  CHECK(toml_table_get_string(input, "Joystick1Mode") == "Keyboard Standard");

  // Network
  auto* net = toml_find_table(doc.get(), "Network");
  REQUIRE(net != nullptr);
  CHECK(toml_table_get_string(net, "FtpServer") == "ftp://example.com/apple2/");

  // Slots
  auto* slots = toml_find_table(doc.get(), "Slots");
  REQUIRE(slots != nullptr);
  CHECK(toml_table_get_string(slots, "Slot1") == "Parallel Printer");
  CHECK(toml_table_get_string(slots, "Slot2") == "Super Serial Card");
  CHECK(toml_table_get_string(slots, "Slot3") == "None");
  CHECK(toml_table_get_string(slots, "Slot4") == "Mockingboard");
  CHECK(toml_table_get_string(slots, "Slot6") == "Disk II");
  CHECK(toml_table_get_string(slots, "Slot7") == "Harddisk");

  // Peripherals
  auto* printer = toml_find_table(doc.get(), "Peripheral.ParallelPrinter");
  REQUIRE(printer != nullptr);
  CHECK(toml_table_get_string(printer, "Filename") == "/tmp/my_printer.txt");
  CHECK(toml_table_get_int(printer, "IdleLimit") == 20);
  CHECK(toml_table_get_bool(printer, "Append") == false);

  auto* ssc = toml_find_table(doc.get(), "Peripheral.SuperSerial");
  REQUIRE(ssc != nullptr);
  CHECK(toml_table_get_string(ssc, "Port") == "/dev/ttyS0");
  CHECK(toml_table_get_int(ssc, "Baud") == 19200);
  CHECK(toml_table_get_bool(ssc, "Loopback") == true);

  auto* disk = toml_find_table(doc.get(), "Peripheral.DiskII");
  REQUIRE(disk != nullptr);
  CHECK(toml_table_get_string(disk, "Drive1") == "/tmp/game_side_a.dsk");
  CHECK(toml_table_get_string(disk, "Drive2") == "/tmp/game_side_b.dsk");
  CHECK(toml_table_get_bool(disk, "FastDisk") == true);

  auto* hd = toml_find_table(doc.get(), "Peripheral.Harddisk");
  REQUIRE(hd != nullptr);
  CHECK(toml_table_get_string(hd, "Drive1") == "/tmp/hd1.hdv");
  CHECK(toml_table_get_string(hd, "Drive2") == "/tmp/hd2.hdv");

  auto* mb = toml_find_table(doc.get(), "Peripheral.Mockingboard");
  REQUIRE(mb != nullptr);
  CHECK(toml_table_get_int(mb, "Volume") == 100);
  CHECK(toml_table_get_string(mb, "Type") == "Mockingboard");

  // Custom Sections Preserved
  auto* custom_rom = toml_find_table(doc.get(), "Custom ROM");
  REQUIRE(custom_rom != nullptr);
  CHECK(toml_table_get_string(custom_rom, "Master") ==
        "/tmp/custom_master.rom");

  auto* custom_kbd = toml_find_table(doc.get(), "Keyboard.Custom");
  REQUIRE(custom_kbd != nullptr);
  CHECK(toml_table_get_string(custom_kbd, "W") == "Up");
  CHECK(toml_table_get_string(custom_kbd, "A") == "Left");
  CHECK(toml_table_get_string(custom_kbd, "S") == "Down");
  CHECK(toml_table_get_string(custom_kbd, "D") == "Right");
}

TEST_CASE("ConfigMigration: Migrate to LinAppleConfig_t Directly") {
  LinAppleConfig_t config;
  std::string err;
  bool ok = config_migrate_legacy_ini(SAMPLE_LEGACY_CONF, &config, &err);
  CHECK(ok);
  CHECK(err.empty());

  CHECK(config.core.machine == MachineType_t::Apple2eEnhanced);
  CHECK(config.core.emulation_speed == doctest::Approx(2.0));
  CHECK(config.core.enhance_disk_speed == true);
  CHECK(config.video.video_standard == VideoStandard_t::PAL);
  CHECK(config.video.video_emulation == VideoEmulation_t::ColorTvEmulation);
  CHECK(config.video.screen_factor == doctest::Approx(1.5));
  CHECK(config.video.fullscreen == true);
  CHECK(config.video.multithreaded == false);
  CHECK(config.audio.sound_emulation == true);
  CHECK(config.audio.speaker_volume == 48);
  CHECK(config.audio.mockingboard_volume == 100);
  CHECK(config.keyboard.layout == ConfigKeyboardLayout_t::French);
  CHECK(config.keyboard.rocker_switch == KeyboardRocker_t::Local);
  CHECK(config.keyboard.mapping_mode == KeyMappingMode_t::Positional);
  CHECK(config.slots.cards[1] == PeripheralCardType_t::ParallelPrinter);
  CHECK(config.slots.cards[6] == PeripheralCardType_t::DiskII);
}

TEST_CASE("ConfigMigration: Legacy Slot Fallback Rules") {
  const std::string conf_no_slots = R"(
[Configuration]
Mouse in slot 4 = 1
Harddisk Enable = 0
Clock Enable = 5
)";

  LinAppleConfig_t config;
  CHECK(config_migrate_legacy_ini(conf_no_slots, &config));

  CHECK(config.slots.cards[1] == PeripheralCardType_t::ParallelPrinter);
  CHECK(config.slots.cards[2] == PeripheralCardType_t::SuperSerial);
  CHECK(config.slots.cards[4] == PeripheralCardType_t::Mouse);
  CHECK(config.slots.cards[5] == PeripheralCardType_t::Clock);
  CHECK(config.slots.cards[6] == PeripheralCardType_t::DiskII);
  CHECK(config.slots.cards[7] == PeripheralCardType_t::Empty);
  CHECK(config.audio.speaker_volume == 50);
  CHECK(config.audio.mockingboard_volume == 50);

  const std::string conf_slots_with_disk = R"(
[Configuration]
Mouse in slot 4 = 1
Harddisk Enable = 0

[Slots]
Disk Image 1 = /path/to/game.dsk
)";

  std::string err;
  auto doc = config_migrate_legacy_to_toml(conf_slots_with_disk, &err);
  REQUIRE(doc != nullptr);
  auto* slots_tbl = toml_find_table(doc.get(), "Slots");
  REQUIRE(slots_tbl != nullptr);
  CHECK(toml_table_get_string(slots_tbl, "Slot4") == "Mouse");
  CHECK(toml_table_get_string(slots_tbl, "Slot7") == "None");

  auto* disk_tbl = toml_find_table(doc.get(), "Peripheral.DiskII");
  REQUIRE(disk_tbl != nullptr);
  CHECK(toml_table_get_string(disk_tbl, "Drive1") == "/path/to/game.dsk");
}

TEST_CASE("ConfigMigration: File Upgrade End-to-End") {
  const std::string temp_conf = "/tmp/test_migrate_input.conf";
  const std::string temp_toml = "/tmp/test_migrate_output.toml";
  unlink(temp_conf.c_str());
  unlink(temp_toml.c_str());

  {
    std::ofstream out(temp_conf);
    out << SAMPLE_LEGACY_CONF;
  }

  std::string err;
  bool ok = config_upgrade_legacy(temp_conf, temp_toml, &err);
  CHECK(ok);
  CHECK(err.empty());
  CHECK(access(temp_toml.c_str(), R_OK) == 0);

  // Load and verify upgraded TOML
  std::string parse_err;
  auto doc = toml_document_load_file(temp_toml, &parse_err);
  REQUIRE(doc != nullptr);
  CHECK(parse_err.empty());

  auto* core = toml_find_table(doc.get(), "Core");
  REQUIRE(core != nullptr);
  CHECK(toml_table_get_string(core, "Machine") == "Apple //e Enhanced");

  unlink(temp_conf.c_str());
  unlink(temp_toml.c_str());
}

TEST_CASE("ConfigMigration: Defensive Checks") {
  CHECK_FALSE(config_migrate_legacy_ini("", nullptr));

  std::string err;
  CHECK_FALSE(config_upgrade_legacy("/tmp/nonexistent_file_987654.conf",
                                    "/tmp/output.toml", &err));
  CHECK_FALSE(err.empty());
}

TEST_CASE("ConfigMigration: Schema-Driven Comments in Migrated TOML") {
  std::string err;
  auto doc = config_migrate_legacy_to_toml(SAMPLE_LEGACY_CONF, &err);
  REQUIRE(doc != nullptr);
  CHECK(err.empty());

  const std::string serialized = toml_document_serialize(doc.get());
  CHECK(serialized.find("[Core]") != std::string::npos);
  CHECK(serialized.find("# LinApple Core Emulation Configuration") !=
        std::string::npos);
  CHECK(serialized.find("# Apple II machine model:") != std::string::npos);
  CHECK(serialized.find("[Peripheral.ParallelPrinter]") != std::string::npos);
  CHECK(serialized.find("# Standard parallel printer interface emulation") !=
        std::string::npos);
  CHECK(serialized.find("# Output file for printed characters:") !=
        std::string::npos);
}
