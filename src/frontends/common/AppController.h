// SPDX-License-Identifier: GPL-2.0-only
#pragma once

struct Configuration_t;
using AppConfig_t = Configuration_t;

auto app_controller_initialize(AppConfig_t* config) -> int;
auto app_controller_shutdown() -> void;
auto app_controller_should_restart() -> bool;
auto app_controller_set_restart(bool restart) -> void;
auto app_controller_handle_diagnostic_commands(const AppConfig_t* config)
    -> bool;
auto app_controller_load_initial_media(const AppConfig_t* config) -> void;

// Record the image currently in a Disk II drive as the user's choice, so the
// next run mounts it again. Call it after a command the user asked for
// succeeds; the card itself has no business knowing the configuration exists.
auto app_controller_save_disk_config(int drive) -> void;
