#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the TUI audio system.
 */
auto TuiAudio_Initialize() -> void;

/**
 * @brief Process audio samples from core.
 */
auto TuiAudio_ProcessSamples(const int16_t* samples, size_t num_samples)
    -> void;

/**
 * @brief Shutdown the TUI audio system.
 */
auto TuiAudio_Shutdown() -> void;

#ifdef __cplusplus
}
#endif
