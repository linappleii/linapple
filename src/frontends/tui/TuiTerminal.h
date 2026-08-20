#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the terminal for TUI mode.
 *
 * Saves current state, enters alternate buffer, enables raw mode, and hides
 * cursor. Also sets up signal handlers for SIGINT, SIGTERM, and SIGWINCH.
 *
 * @return 0 on success, non-zero on failure.
 */
auto tui_terminal_initialize() -> int;

/**
 * @brief Restore the terminal to its original state.
 */
auto tui_terminal_shutdown() -> void;

/**
 * @brief Check if a resize event (SIGWINCH) occurred.
 */
auto tui_terminal_was_resized() -> bool;

/**
 * @brief Clear the resize flag.
 */
auto tui_terminal_clear_resized() -> void;

/**
 * @brief Check if an interrupt signal (SIGINT/SIGTERM) was received.
 */
auto tui_terminal_is_interrupted() -> bool;

#ifdef __cplusplus
}
#endif
