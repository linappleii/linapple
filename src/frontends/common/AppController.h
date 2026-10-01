// SPDX-License-Identifier: GPL-2.0-only
#pragma once

struct Configuration_t;
using AppConfig_t = Configuration_t;

// High-level controller for LinApple lifecycle.
// Manages the initialization and shutdown of all core subsystems.

// Initialize all core subsystems based on the provided configuration.
// Handles path resolution, registry, logger, and hardware startup.
// Returns 0 on success, non-zero on error.
auto app_controller_initialize(AppConfig_t* config) -> int;

// Shut down all core subsystems in the correct order.
auto app_controller_shutdown() -> void;

// Check if the application has been marked for restart.
auto app_controller_should_restart() -> bool;

// Set or clear the restart flag.
auto app_controller_set_restart(bool restart) -> void;

// Handle diagnostic and help commands that should execute before UI init.
// Returns true if a command was handled and the application should exit.
auto app_controller_handle_diagnostic_commands(const AppConfig_t* config)
    -> bool;

// Perform initial media loading and optionally boot the machine.
// Should be called after initialize but before the main loop.
auto app_controller_load_initial_media(const AppConfig_t* config) -> void;

// Record the image currently in a Disk II drive as the user's choice, so the
// next run mounts it again. Call it after a command the user asked for
// succeeds; the card itself has no business knowing the configuration exists.
// Param drive: 0 for drive 1, 1 for drive 2.
auto app_controller_save_disk_config(int drive) -> void;
