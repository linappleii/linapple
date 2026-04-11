/*
 * LinAppleCore.h - The public API for the LinApple Emulator Core.
 *
 * This header defines the contract between the platform-agnostic emulation
 * core and any frontend (SDL3, GTK4, Headless, etc.).
 */

#ifndef LINAPPLE_CORE_H
#define LINAPPLE_CORE_H

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// --- Lifecycle Management ---

/**
 * @brief Initialize the emulator core and its internal state.
 * 
 * This must be called before any other Linapple_ function.
 * It sets up the virtual Apple II hardware, memory, and CPU.
 */
void Linapple_Init();

/**
 * @brief Clean up resources and shut down the emulator core.
 */
void Linapple_Shutdown();


// --- Execution Control ---

/**
 * @brief Run the emulator for a specific number of cycles.
 * 
 * Typically called once per frame (approx. 17,030 cycles for Apple II).
 * 
 * @param cycles Requested number of cycles to execute.
 * @return uint32_t Actual number of cycles executed.
 */
uint32_t Linapple_RunFrame(uint32_t cycles);


// --- Input Handling ---

/**
 * @brief Set the state of a specific Apple II key code.
 * 
 * @param apple_code The 7-bit Apple II keyboard code.
 * @param down True if the key is pressed, false if released.
 */
void Linapple_SetKeyState(uint8_t apple_code, bool down);

/**
 * @brief Set the state of a special Apple key (Open/Closed Apple).
 * 
 * @param key Key identifier (e.g., 0 for Open Apple, 1 for Closed Apple).
 * @param down True if pressed, false if released.
 */
void Linapple_SetAppleKey(int key, bool down);

/**
 * @brief Update joystick axis position.
 * 
 * @param axis Axis index (0 for X, 1 for Y).
 * @param value Axis value, typically ranging from -32768 to 32767.
 */
void Linapple_SetJoystickAxis(int axis, int value);

/**
 * @brief Set the state of a joystick button.
 * 
 * @param button Button index (0 or 1).
 * @param down True if pressed, false if released.
 */
void Linapple_SetJoystickButton(int button, bool down);


// --- Output Callbacks ---

/**
 * @brief Callback signature for video frame updates.
 * 
 * @param pixels Pointer to the 32-bit RGBA pixel data.
 * @param width Width of the frame in pixels.
 * @param height Height of the frame in pixels.
 * @param pitch Number of bytes per row of pixels.
 */
typedef void (*LinappleVideoCallback)(const uint32_t* pixels, int width, int height, int pitch);

/**
 * @brief Callback signature for audio sample updates.
 * 
 * @param samples Pointer to interleaved 16-bit signed stereo samples.
 * @param num_samples Number of samples (frames * channels).
 */
typedef void (*LinappleAudioCallback)(const int16_t* samples, size_t num_samples);

/**
 * @brief Register a callback for video frame rendering.
 */
void Linapple_SetVideoCallback(LinappleVideoCallback cb);

/**
 * @brief Register a callback for audio sample output.
 */
void Linapple_SetAudioCallback(LinappleAudioCallback cb);


#ifdef __cplusplus
}
#endif

#endif // LINAPPLE_CORE_H
