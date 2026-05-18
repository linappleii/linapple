#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the TUI input system.
 */
auto TuiInput_Initialize() -> void;

/**
 * @brief Poll for terminal and joystick input.
 */
auto TuiInput_Poll() -> void;

/**
 * @brief Shutdown the TUI input system.
 */
auto TuiInput_Shutdown() -> void;

#ifdef __cplusplus
}
#endif
