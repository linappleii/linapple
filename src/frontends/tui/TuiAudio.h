#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the TUI audio system.
 */
auto tui_audio_initialize() -> void;

/**
 * @brief Process audio samples from core.
 */
auto tui_audio_process_samples(const int16_t* samples, size_t num_samples)
    -> void;

/**
 * @brief Shutdown the TUI audio system.
 */
auto tui_audio_shutdown() -> void;

#ifdef __cplusplus
}
#endif
