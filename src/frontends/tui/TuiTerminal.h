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
auto TuiTerminal_Initialize() -> int;

/**
 * @brief Restore the terminal to its original state.
 */
auto TuiTerminal_Shutdown() -> void;

/**
 * @brief Check if a resize event (SIGWINCH) occurred.
 */
auto TuiTerminal_WasResized() -> bool;

/**
 * @brief Clear the resize flag.
 */
auto TuiTerminal_ClearResized() -> void;

/**
 * @brief Check if an interrupt signal (SIGINT/SIGTERM) was received.
 */
auto TuiTerminal_IsInterrupted() -> bool;

#ifdef __cplusplus
}
#endif
