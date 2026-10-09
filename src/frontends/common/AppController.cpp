// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/AppController.h"

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/Asset.h"
#include "core/BasicLiveSync.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/ProgramLoader.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppEnvironment.h"
#include "frontends/common/HarddiskFrontend.h"
#include "frontends/common/HostSink.h"
#include "frontends/common/JoystickConfig.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/PrinterFrontend.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/common/SuperSerialFrontend.h"

static bool s_initialized = false;

constexpr float min_screen_factor = 0.25F;
constexpr float max_screen_factor = 8.0F;
constexpr uint32_t clks_per_frame_pal = 20280;
constexpr uint32_t clks_per_frame_ntsc = 17030;
constexpr const char* harddisk_card_id = "linapple.harddisk";

// The drive index of the first hard disk image the command line named, or -1.
static auto first_harddisk_from_args(const AppConfig& config) -> int {
  for (size_t i = 0; i < config.harddisk_path.size(); ++i) {
    if (config.harddisk_path_from_args.at(i) &&
        config.harddisk_path.at(i).at(0) != '\0') {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Reads [Slots] the way the card registration does: a slot the file leaves
// out keeps its factory card, and slot 1's factory card is the printer. The
// file a printer sink writes is named from this, never from which card
// happened to print first.
static auto lowest_configured_printer_slot() -> int {
  for (int slot = 1; slot < NUM_SLOTS; ++slot) {
    const std::string key = "Slot " + std::to_string(slot);
    std::string name;
    if (!config_load_string("Slots", key.c_str(), &name)) {
      name = slot == 1 ? "linapple.printer" : "";
    }
    if (name.empty() || name == "None") {
      continue;
    }
    const Peripheral_t* card = peripheral_find_internal(name.c_str());
    if (card != nullptr && std::strcmp(card->id, "linapple.printer") == 0) {
      return slot;
    }
  }
  return 0;
}

// The printer is told its settings once the save-state directory is known,
// because the first byte printed is what opens the file, and a relative
// filename resolves against that directory.
static auto configure_printer_sink() -> void {
  PrinterFrontendSettings settings{};
  settings.filename = "Printer.txt";
  config_load_string("Configuration", cfg_pprinter_filename,
                     &settings.filename);
  uint32_t append = 1;
  config_load_int("Configuration", cfg_printer_append, &append);
  uint32_t eight_bit = 0;
  config_load_int("Configuration", cfg_printer_eight_bit, &eight_bit);
  settings.append = append != 0;
  settings.eight_bit = eight_bit != 0;
  settings.base_dir = system_state.save_state_dir.data();
  settings.primary_slot = lowest_configured_printer_slot();
  printer_frontend_install(settings);
}

// After the cards exist: the switch rows go through a card's command queue,
// and the device sits behind whichever slot took them.
static auto configure_serial_port() -> void {
  SuperSerialFrontendSettings settings{};
  config_load_string(cfg_sec_configuration, cfg_serial_port, &settings.port);
  config_load_string(cfg_sec_configuration, cfg_serial_switches_1,
                     &settings.switches_1);
  config_load_string(cfg_sec_configuration, cfg_serial_switches_2,
                     &settings.switches_2);
  settings.base_dir = system_state.save_state_dir.data();
  super_serial_frontend_configure(settings);
}

// The sink goes in first so a re-initialisation closes the previous run's
// devices through it before their settings change.
static auto install_host_sink() -> void {
  host_sink_install();
  configure_printer_sink();
  configure_serial_port();
}

static auto initialize_directory(const char* reg_key, char* target_buffer,
                                 size_t buffer_size) -> void {
  std::string path =
      Configuration::instance().get_string("Preferences", reg_key);
  if (path.empty()) {
    path = Path::get_user_data_dir();
  }
  if (path.empty()) {
    return;
  }

  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  util_safe_strcpy(target_buffer, path.c_str(), buffer_size);
  // The helper creates the directory before each separator, so the leaf is
  // only made when the path ends in one.
  Path::ensure_dir_exists(path + "/");
}

static auto load_custom_rom(const char* rom_path) -> bool {
  if (rom_path == nullptr || *rom_path == '\0') {
    mem_set_custom_rom_data(nullptr, 0);
    return true;
  }

  std::string path_to_open = rom_path;
  FilePtr f{std::fopen(path_to_open.c_str(), "rb"), std::fclose};
  if (!f) {
    std::string resolved = Path::find_data_file(rom_path);
    if (!resolved.empty()) {
      path_to_open = resolved;
      f = FilePtr{std::fopen(path_to_open.c_str(), "rb"), std::fclose};
    }
  }
  if (!f) {
    Logger::error("\nError: Unable to open custom ROM file: %s\n\n", rom_path);
    mem_set_custom_rom_data(nullptr, 0);
    return false;
  }

  constexpr int64_t max_rom_file_size = 65536;
  int64_t fsize = Path::file_size(f.get());
  if (fsize < static_cast<int64_t>(APPLE2_ROM_SIZE) ||
      fsize > max_rom_file_size) {
    Logger::error(
        "\nError: Invalid custom ROM file size (%ld bytes, expected at least "
        "%u bytes): %s\n\n",
        static_cast<long>(fsize), static_cast<unsigned int>(APPLE2_ROM_SIZE),
        rom_path);
    mem_set_custom_rom_data(nullptr, 0);
    return false;
  }

  std::vector<uint8_t> custom_rom_buffer(static_cast<size_t>(fsize));
  if (std::fread(custom_rom_buffer.data(), 1, custom_rom_buffer.size(),
                 f.get()) != custom_rom_buffer.size()) {
    Logger::error("\nError: Failed to read custom ROM file: %s\n\n", rom_path);
    mem_set_custom_rom_data(nullptr, 0);
    return false;
  }

  mem_set_custom_rom_data(custom_rom_buffer.data(), custom_rom_buffer.size());
  return true;
}

static auto apply_screen_factor() -> void {
  std::string factor_str;
  if (!config_load_string("Configuration", "Screen factor", &factor_str) &&
      !config_load_string("Configuration", "Screen Factor", &factor_str) &&
      !config_load_string("Preferences", "Screen factor", &factor_str) &&
      !config_load_string("Preferences", "Screen Factor", &factor_str)) {
    return;
  }

  try {
    float factor = std::stof(factor_str);
    if (factor >= k_min_screen_factor && factor <= k_max_screen_factor) {
      system_state.screen_width =
          static_cast<int>(static_cast<float>(SCREEN_WIDTH) * factor);
      system_state.screen_height =
          static_cast<int>(static_cast<float>(SCREEN_HEIGHT) * factor);
    }
  } catch (...) {
  }
}

static auto autoload_startup_disks(const AppConfig* config) -> void {
  uint32_t autoload = 0;
  bool has_autoload =
      config_load_int("Configuration", cfg_slot6_autoload, &autoload) ||
      config_load_int("Preferences", cfg_slot6_autoload, &autoload) ||
      config_load_int("Slots", cfg_slot6_autoload, &autoload);

  std::string disk1;
  bool has_disk1 =
      (config->disk_path.at(0).at(0) != '\0') ||
      config_load_string("Slots", cfg_disk_image1, &disk1) ||
      config_load_string("Configuration", cfg_disk_image1, &disk1) ||
      config_load_string("Preferences", cfg_disk_image1, &disk1);

  if (config->disk_path.at(0).at(0) != '\0') {
    return;
  }

  if (!has_autoload || autoload == 0 || !has_disk1 || disk1.empty()) {
    static_cast<void>(asset_insert_master_disk());
    return;
  }

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  util_safe_strcpy(cmd.path, disk1.c_str(), disk_insert_path_max);
  cmd.write_protected = 0;
  peripheral_command(disk_default_slot, disk_cmd_insert, &cmd, sizeof(cmd));

  std::string disk2;
  if (config_load_string("Slots", cfg_disk_image2, &disk2) ||
      config_load_string("Configuration", cfg_disk_image2, &disk2) ||
      config_load_string("Preferences", cfg_disk_image2, &disk2)) {
    if (!disk2.empty()) {
      DiskInsertCmd_t cmd2{};
      cmd2.drive = disk_drive_1;
      util_safe_strcpy(cmd2.path, disk2.c_str(), disk_insert_path_max);
      cmd2.write_protected = 0;
      peripheral_command(disk_default_slot, disk_cmd_insert, &cmd2,
                         sizeof(cmd2));
    }
  }
}

auto app_controller_initialize(AppConfig* config) -> int {
  if (config == nullptr) {
    return -1;
  }

  if (s_initialized) {
    app_controller_shutdown();
  }

  app_env_resolve_paths(config);

  current_apple2_type = config->apple2_type;

  if (!load_custom_rom(config->rom_path.data())) {
    return -1;
  }

  // Made before the cards are placed, and on every restart, since a request
  // lasts one registration.
  if (first_harddisk_from_args(*config) >= 0) {
    linapple_request_card_for_run(k_harddisk_card_id);
  }

  if (linapple_init() != 0) {
    mem_set_custom_rom_data(nullptr, 0);
    return -1;
  }
  s_initialized = true;

  // Reported here so every frontend and a headless machine start from the
  // configured plug; an SDL frontend reports again with the devices it opened.
  linapple_set_game_pulldowns(joystick_config_pulldown_mask());
  linapple_set_shift_key_mod(joystick_config_shift_key_mod());

  apply_screen_factor();

  if (config->is_pal) {
    g_videotype = VT_COLOR_TVEMU;
    system_state.video_scanner_ntsc = false;
    system_state.clks_per_frame = k_clks_per_frame_pal;
    current_clk_6502 = CLOCK_6502_PAL;
  } else {
    g_videotype = VT_COLOR_STANDARD;
    system_state.video_scanner_ntsc = true;
    system_state.clks_per_frame = k_clks_per_frame_ntsc;
    current_clk_6502 = CLOCK_6502_NTSC;
  }

  const int config_speed = Configuration::instance().get_int(
      "Configuration", "Emulation Speed",
      static_cast<int>(emulation_speed_normal));
  if (config_speed >= 0 &&
      static_cast<uint32_t>(config_speed) <= emulation_speed_max) {
    system_state.speed = static_cast<uint32_t>(config_speed);
  }

  if (config->snapshot_path.at(0) != '\0') {
    save_state_set_filename(config->snapshot_path.data());
  }
  save_state_startup();

  initialize_directory(cfg_pref_start_dir, &system_state.current_dir[0],
                       sizeof(system_state.current_dir));
  initialize_directory(cfg_pref_hdd_start_dir, &system_state.hdd_dir[0],
                       sizeof(system_state.hdd_dir));
  initialize_directory(cfg_pref_savestate_dir, &system_state.save_state_dir[0],
                       sizeof(system_state.save_state_dir));
  initialize_directory(cfg_ftp_local_dir, &system_state.ftp_local_dir[0],
                       sizeof(system_state.ftp_local_dir));

  install_host_sink();

  std::string ftp_server = Configuration::instance().get_string(
      "Preferences", cfg_ftp_dir,
      "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/");
  if (ftp_server.empty()) {
    ftp_server = "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/";
  }
  util_safe_strcpy(system_state.ftp_server.data(), ftp_server.c_str(),
                   system_state.ftp_server.size());

  std::string ftp_server_hdd = Configuration::instance().get_string(
      "Preferences", cfg_ftp_hdd_dir,
      "ftp://ftp.apple.asimov.net/pub/apple_II/images/");
  if (ftp_server_hdd.empty()) {
    ftp_server_hdd = "ftp://ftp.apple.asimov.net/pub/apple_II/images/";
  }
  util_safe_strcpy(system_state.ftp_server_hdd.data(), ftp_server_hdd.c_str(),
                   system_state.ftp_server_hdd.size());

  std::string ftp_userpass = Configuration::instance().get_string(
      "Preferences", cfg_ftp_userpass, "anonymous:my-mail@mail.com");
  if (ftp_userpass.empty()) {
    ftp_userpass = "anonymous:my-mail@mail.com";
  }
  util_safe_strcpy(system_state.ftp_user_pass.data(), ftp_userpass.c_str(),
                   system_state.ftp_user_pass.size());

  frontend_update_keyboard_mapping();
  if (config->caps_lock_mode >= 0) {
    keyboard_set_caps_mode(config->caps_lock_mode);
  }

  if (config->debugger_script.at(0) != '\0') {
    util_safe_strcpy(&system_state.debugger_script[0],
                     config->debugger_script.data(), path_max_len);
  }

  system_state.mode = app_mode_running;
  system_state.restart = false;
  system_state.fullscreen = config->is_fullscreen;
  system_state.disable_debugger = config->disable_debugger;

  harddisk_frontend_initialize();

  std::string sync_file = config->basic_sync_file.data();
  int line_mode = config->basic_line_mode < 0 ? 0 : config->basic_line_mode;
  if (!sync_file.empty()) {
    basic_sync_init(sync_file.c_str(), line_mode == 1
                                           ? basic_line_mode_positional
                                           : basic_line_mode_explicit);
  }

  autoload_startup_disks(config);

  return 0;
}

auto app_controller_handle_diagnostic_commands(const AppConfig* config)
    -> bool {
  if (config == nullptr) {
    return false;
  }

  if (config->intent == INTENT_HELP) {
    app_args_print_help();
    return true;
  }

  if (config->intent != INTENT_DIAGNOSTIC) {
    return false;
  }

  if (config->is_list_hardware) {
    linapple_list_hardware();
    return true;
  }

  if (config->hardware_info_name.at(0) != '\0') {
    const Peripheral_t* card =
        peripheral_find_internal(config->hardware_info_name.data());
    if (card == nullptr) {
      fprintf(stderr, "error: Unknown hardware '%s'\n",
              config->hardware_info_name.data());
      return true;
    }

    printf("Hardware info: %s\n", card->name);
    printf("ABI Version: %d\n", card->abi_version);
    printf("Compatible Slots: ");
    bool first = true;
    for (int i = 0; i < NUM_SLOTS; ++i) {
      if ((card->compatible_slots & (1U << static_cast<uint32_t>(i))) == 0) {
        continue;
      }
      if (!first) {
        printf(", ");
      }
      printf("%d", i);
      first = false;
    }
    printf("\n");
    const char* path =
        peripheral_get_plugin_path(config->hardware_info_name.data());
    if (path != nullptr) {
      printf("Plugin Path: %s\n", path);
    }
    return true;
  }

  if (config->test_cpu_file.at(0) != '\0') {
    linapple_cpu_test(config->test_cpu_file.data(), config->test_cpu_trap);
    return true;
  }

  return false;
}

static auto load_initial_disk(int drive, const char* path) -> void {
  if (path == nullptr || *path == '\0') {
    return;
  }

  std::string actual_path = path;
  if (access(actual_path.c_str(), R_OK) != 0) {
    size_t pos = actual_path.find_last_of('/');
    std::string filename =
        (pos != std::string::npos) ? actual_path.substr(pos + 1) : actual_path;
    std::string found = Path::find_data_file(filename);
    if (!found.empty()) {
      actual_path = found;
    }
  }

  int res = linapple_load_program(actual_path.c_str());
  if (res != program_load_not_a_program) {
    return;
  }

  DiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  util_safe_strcpy(&cmd.path[0], actual_path.c_str(), disk_insert_path_max);
  if (peripheral_command(disk_default_slot, disk_cmd_insert, &cmd,
                         sizeof(cmd)) == peripheral_ok) {
    app_controller_save_disk_config(drive);
  }
}

// Both drives belong to the one card, so a missing card is reported once
// whichever drives were named. Only the command line installs a card for the
// run, so a saved image with no card to go into stays unmounted. An error
// reaches the terminal through the log, so it is logged once and not printed.
static auto load_initial_harddisks(const AppConfig& config) -> void {
  int first_named = -1;
  for (size_t i = 0; i < config.harddisk_path.size(); ++i) {
    if (config.harddisk_path.at(i).at(0) != '\0') {
      first_named = static_cast<int>(i);
      break;
    }
  }
  if (first_named < 0) {
    return;
  }

  if (peripheral_slot_of(harddisk_card_id) < 0) {
    const int from_args = first_harddisk_from_args(config);
    if (from_args < 0) {
      Logger::warning(
          "Harddisk Image %d is set but no hard disk is configured; not "
          "mounted\n",
          first_named + 1);
      return;
    }
    Logger::error("--hd%d: %s; image not mounted\n", from_args + 1,
                  peripheral_find_internal(harddisk_card_id) == nullptr
                      ? "this build has no Harddisk card"
                      : "no free slot for the Harddisk");
    return;
  }

  for (size_t i = 0; i < config.harddisk_path.size(); ++i) {
    const char* path = config.harddisk_path.at(i).data();
    if (*path == '\0') {
      continue;
    }
    if (config.harddisk_path_from_args.at(i)) {
      harddisk_frontend_insert_for_run(static_cast<int>(i), path);
    } else {
      harddisk_frontend_insert(static_cast<int>(i), path, false);
    }
  }
}

auto app_controller_load_initial_media(const AppConfig* config) -> void {
  if (config == nullptr) {
    return;
  }

  for (size_t i = 0; i < disk_drive_count; ++i) {
    const char* path = config->disk_path.at(i).data();
    load_initial_disk(static_cast<int>(i), path);
  }

  if (config->program_path.at(0) != '\0') {
    if (linapple_load_program(config->program_path.data()) != 0) {
      fprintf(stderr, "error: Could not load program '%s'\n",
              config->program_path.data());
    }
  }

  load_initial_harddisks(*config);

  if (config->is_boot) {
    cpu_reset();
    peripheral_manager_reset();
    video_redraw_screen();
  }
}

auto app_controller_shutdown() -> void {
  mem_set_custom_rom_data(nullptr, 0);
  if (!s_initialized) {
    return;
  }

  basic_sync_shutdown();
  save_state_shutdown();
  linapple_shutdown();
  Logger::destroy();

  s_initialized = false;
}

auto app_controller_save_disk_config(int drive) -> void {
  if (drive != disk_drive_0 && drive != disk_drive_1) {
    return;
  }

  // Commands reach the card through a queue, so the drive holds what the user
  // asked for only once the queue has been drained. What the drive holds after
  // that is what the configuration records, including empty after a failure.
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  if (peripheral_query(disk_default_slot, disk_query_status, &status, &size) !=
      peripheral_ok) {
    return;
  }

  config_save_string(
      "Slots", (drive == disk_drive_0) ? cfg_disk_image1 : cfg_disk_image2,
      (drive == disk_drive_0) ? status.drive0_full_path
                              : status.drive1_full_path);
}

auto app_controller_save_harddisk_config(int drive) -> void {
  if (drive != harddisk_drive_0 && drive != harddisk_drive_1) {
    return;
  }

  peripheral_manager_think(0);

  HarddiskStatus_t status{};
  size_t size = sizeof(status);
  if (peripheral_query(harddisk_frontend_slot(), harddisk_query_status, &status,
                       &size) != peripheral_ok) {
    return;
  }

  // set_string keeps the harddisk_path field in step with the key, so what
  // the next save writes is what the drive holds.
  Configuration_t::instance().set_string(
      cfg_sec_preferences,
      (drive == harddisk_drive_0) ? REGVALUE_HDD_IMAGE1 : REGVALUE_HDD_IMAGE2,
      (drive == harddisk_drive_0) ? status.drive0_full_path
                                  : status.drive1_full_path);
}

auto app_controller_should_restart() -> bool { return system_state.restart; }

auto app_controller_set_restart(bool restart) -> void {
  system_state.restart = restart;
}
