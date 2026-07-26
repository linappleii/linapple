#pragma once

#include "frontends/common/AppConfig.h"

/**
 * High-level controller for LinApple lifecycle.
 * Manages the initialization and shutdown of all core subsystems.
 */

/**
 * Initialize all core subsystems based on the provided configuration.
 * Handles path resolution, registry, logger, and hardware startup.
 *
 * @return 0 on success, non-zero on error.
 */
auto app_controller_initialize(AppConfig_t* config) -> int;

/**
 * Shut down all core subsystems in the correct order.
 */
void AppController_Shutdown();

/**
 * Check if the application has been marked for restart.
 */
auto app_controller_should_restart() -> bool;

/**
 * Set or clear the restart flag.
 */
void AppController_SetRestart(bool restart);

/**
 * Handle diagnostic and help commands that should execute before UI init.
 * @return true if a command was handled and the application should exit.
 */
auto app_controller_handle_diagnostic_commands(const AppConfig_t* config) -> bool;

/**
 * Perform initial media loading and optionally boot the machine.
 * Should be called after Initialize but before the main loop.
 */
void AppController_LoadInitialMedia(const AppConfig_t* config);
