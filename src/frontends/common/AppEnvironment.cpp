// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/AppEnvironment.h"

#include <cstdio>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "core/config/ConfigDiscovery.h"
#include "core/config/ConfigMigration.h"
#include "core/config/ConfigSchema.h"
#include "frontends/common/AppConfig.h"

static LinAppleConfig_t s_active_config;

auto app_env_get_config() -> const LinAppleConfig_t& { return s_active_config; }

auto app_env_get_config_mut() -> LinAppleConfig_t& { return s_active_config; }

auto app_env_resolve_paths(AppConfig_t* config) -> void {
  if (config == nullptr) {
    return;
  }

  Logger::initialize();
  if (config->is_verbose) {
    Logger::set_verbosity(LogLevel_t::k_perf);
  } else if (config->is_log) {
    Logger::set_verbosity(LogLevel_t::k_info);
  } else {
    Logger::set_verbosity(LogLevel_t::k_warning);
  }

  ConfigSearchOptions_t options;
  if (config->config_path.at(0) != '\0') {
    options.cli_override = config->config_path.data();
  }
  options.create_default_if_missing = true;

  std::string loaded_path;
  bool is_legacy = false;
  std::string err;

  if (config_load_active(options, &s_active_config, &loaded_path, &is_legacy,
                         &err)) {
    if (is_legacy) {
      // Legacy INI configuration detected: load into Configuration_t for
      // backward compatibility and migrate into modern in-memory schema.
      Configuration_t::instance().load(loaded_path);
      std::ifstream stream(loaded_path, std::ios::in | std::ios::binary);
      if (stream.is_open()) {
        std::string content((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());
        config_migrate_legacy_ini(content, &s_active_config);
      }
      fprintf(stderr,
              "WARNING: Legacy configuration file '%s' detected.\n"
              "Please upgrade to modern TOML schema using: linapple "
              "--upgrade-config\n",
              loaded_path.c_str());
    } else {
      // Modern TOML configuration loaded: populate Configuration_t paths and
      // keys for any legacy subsystems still querying Configuration_t directly.
      Configuration_t::instance().set_path(loaded_path);
      Configuration_t::instance().set_int(
          "Configuration", "Computer Emulation",
          static_cast<int>(s_active_config.core.machine));
      Configuration_t::instance().set_int(
          "Configuration", "Emulation Speed",
          static_cast<int>(s_active_config.core.emulation_speed *
                           SPEED_NORMAL));
      Configuration_t::instance().set_string(
          "Configuration", "Screen factor",
          std::to_string(s_active_config.video.screen_factor));
      Configuration_t::instance().set_int(
          "Configuration", "Video Standard",
          s_active_config.video.video_standard == VideoStandard_t::PAL ? 1 : 0);
      Configuration_t::instance().set_int(
          "Configuration", "Enhance Disk Speed",
          s_active_config.core.enhance_disk_speed ? 1 : 0);
      Configuration_t::instance().set_int(
          "Configuration", "Boot at Startup",
          s_active_config.core.boot_on_startup ? 1 : 0);
      Configuration_t::instance().set_int(
          "Configuration", "Save State On Exit",
          s_active_config.core.save_state_on_exit ? 1 : 0);
      Configuration_t::instance().set_int(
          "Configuration", "Disable Debugger",
          s_active_config.core.enable_debugger ? 0 : 1);
      Configuration_t::instance().set_string(
          "Configuration", "Basic Live Sync File",
          s_active_config.core.basic_sync_file);
      Configuration_t::instance().set_int(
          "Configuration", "Basic Line Numbering",
          s_active_config.core.basic_line_numbering ==
                  BasicLineNumbering_t::Positional
              ? 1
              : 0);
      Configuration_t::instance().set_int(
          "Configuration", "Fullscreen",
          (s_active_config.frontend.fullscreen ||
           s_active_config.video.fullscreen)
              ? 1
              : 0);
      Configuration_t::instance().set_int(
          "Configuration", "Show Leds",
          s_active_config.video.show_leds ? 1 : 0);
      Configuration_t::instance().set_string(
          "Configuration", "Monochrome Color",
          s_active_config.video.monochrome_color);
    }

    util_safe_strcpy(config->config_path.data(), loaded_path.c_str(),
                     path_max_len);
  } else if (!err.empty()) {
    Logger::warning("Configuration load failed: %s. Using default schema.\n",
                    err.c_str());
  }

  // Precedence resolution: CLI Switch > Config File Value > System Default

  if (!config->is_boot_explicit) {
    config->is_boot = s_active_config.core.boot_on_startup;
  }

  if (!config->is_fullscreen_explicit) {
    config->is_fullscreen = (s_active_config.frontend.fullscreen ||
                             s_active_config.video.fullscreen);
  }

  if (!config->is_pal_explicit) {
    config->is_pal =
        (s_active_config.video.video_standard == VideoStandard_t::PAL);
  }

  if (!config->disable_debugger_explicit) {
    config->disable_debugger = !s_active_config.core.enable_debugger;
  }

  if (config->caps_lock_mode < 0) {
    config->caps_lock_mode = (s_active_config.keyboard.caps_lock_mode ==
                              ConfigCapsLockMode_t::Emulated)
                                 ? 1
                                 : 0;
  }

  if (config->basic_line_mode < 0) {
    config->basic_line_mode = (s_active_config.core.basic_line_numbering ==
                               BasicLineNumbering_t::Positional)
                                  ? 1
                                  : 0;
  }

  if (config->basic_sync_file.at(0) == '\0' &&
      !s_active_config.core.basic_sync_file.empty()) {
    util_safe_strcpy(config->basic_sync_file.data(),
                     s_active_config.core.basic_sync_file.c_str(),
                     path_max_len);
  }

  if (!config->tui_render_mode_explicit) {
    config->tui_render_mode =
        (s_active_config.frontend.render_mode == ConfigTuiRenderMode_t::Block ||
         s_active_config.video.tui_render_mode == ConfigTuiRenderMode_t::Block)
            ? TUI_RENDER_BLOCK
            : TUI_RENDER_SMART;
  }

  if (!config->apple2_type_explicit) {
    switch (s_active_config.core.machine) {
      case MachineType_t::Apple2:
        config->apple2_type = A2TYPE_APPLE2;
        break;
      case MachineType_t::Apple2Plus:
      case MachineType_t::Apple2JPlus:
        config->apple2_type = A2TYPE_APPLE2PLUS;
        break;
      case MachineType_t::Apple2e:
        config->apple2_type = A2TYPE_APPLE2E;
        break;
      case MachineType_t::Apple2eEnhanced:
      case MachineType_t::CloneBase64A:
      case MachineType_t::ClonePravets82:
      case MachineType_t::CloneTK3000e:
      default:
        config->apple2_type = A2TYPE_APPLE2EENHANCED;
        break;
    }
  }
}
