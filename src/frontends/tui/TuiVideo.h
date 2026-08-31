#pragma once

#include <cstdint>

#include "frontends/common/AppConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the TUI video system.
 */
auto tui_video_initialize() -> void;

/**
 * @brief Set the TUI graphics rendering mode (smart shape detector vs classic
 * block).
 */
auto tui_video_set_render_mode(TuiRenderMode_t mode) -> void;

/**
 * @brief Toggle the TUI graphics rendering mode (smart shape vs classic block).
 */
auto tui_video_toggle_render_mode() -> void;

/**
 * @brief Get current TUI graphics rendering mode.
 */
auto tui_video_get_render_mode() -> TuiRenderMode_t;

/**
 * @brief Render the current frame to the terminal.
 *
 * @param pixels Pointer to the current 32-bit RGBA pixel buffer from core.
 * @param width Width of the pixel buffer.
 * @param height Height of the pixel buffer.
 * @param pitch Pitch of the pixel buffer.
 */
auto tui_video_render_frame(const uint32_t* pixels, int width, int height,
                            int pitch) -> void;

/**
 * @brief Notify the video system of a terminal resize.
 */
auto tui_video_on_resize() -> void;

/**
 * @brief Toggle the TUI help screen overlay.
 */
auto tui_video_toggle_help() -> void;

/**
 * @brief Check if the TUI help screen is currently visible.
 */
auto tui_video_is_help_visible() -> bool;

/**
 * @brief Close the TUI help screen overlay.
 */
auto tui_video_close_help() -> void;

/**
 * @brief Toggle TUI fullscreen mode (hides/shows bottom status bar).
 */
auto tui_video_toggle_fullscreen() -> void;

/**
 * @brief Check if TUI fullscreen mode is active.
 */
auto tui_video_is_fullscreen() -> bool;

/**
 * @brief Save screenshot of current TUI screen to .ans and .txt files.
 */
auto tui_video_save_screenshot() -> void;

#ifdef __cplusplus
}
#endif
