#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the TUI video system.
 */
auto tui_video_initialize() -> void;

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

#ifdef __cplusplus
}
#endif
