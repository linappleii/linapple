// SPDX-License-Identifier: GPL-2.0-only
#pragma once

/**
 * @brief Initialize the TUI input system.
 */
auto tui_input_initialize() -> void;

/**
 * @brief Poll for terminal and joystick input.
 */
auto tui_input_poll() -> void;

/**
 * @brief Shutdown the TUI input system.
 */
auto tui_input_shutdown() -> void;
