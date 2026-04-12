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

/**
 * @brief Run a headless CPU test from a file.
 */
void Linapple_CpuTest(const char* szTestFile);

/**
 * @brief Get the number of milliseconds since the emulator started.
 */
uint32_t Linapple_GetTicks();


// --- Execution Control ---

/**
 * @brief Run the emulator for a specific number of cycles.
 *
 * Typically called once per frame (approx. 17,030 cycles for Apple II).
 *
 * @param cycles Requested number of cycles to execute.
 * @return uint32_t Actual number of cycles executed.
 */
auto Linapple_RunFrame(uint32_t cycles) -> uint32_t;


// --- Input Handling ---

/**
 * @brief LinApple agnostic key codes.
 */
enum LinAppleKey {
    LINAPPLE_KEY_UNKNOWN = 0,
    // ASCII range 0x01 - 0x7F used for standard characters
    LINAPPLE_KEY_RETURN = 0x0D,
    LINAPPLE_KEY_ESCAPE = 0x1B,
    LINAPPLE_KEY_BACKSPACE = 0x08,
    LINAPPLE_KEY_TAB = 0x09,
    LINAPPLE_KEY_SPACE = 0x20,
    
    // Extended keys start at 0x100
    LINAPPLE_KEY_UP = 0x100,
    LINAPPLE_KEY_DOWN,
    LINAPPLE_KEY_LEFT,
    LINAPPLE_KEY_RIGHT,
    LINAPPLE_KEY_PAGEUP,
    LINAPPLE_KEY_PAGEDOWN,
    LINAPPLE_KEY_HOME,
    LINAPPLE_KEY_END,
    LINAPPLE_KEY_INSERT,
    LINAPPLE_KEY_DELETE,
    LINAPPLE_KEY_PAUSE,
    LINAPPLE_KEY_SCROLLLOCK,
    LINAPPLE_KEY_CAPSLOCK,
    LINAPPLE_KEY_PRINT,
    
    LINAPPLE_KEY_F1 = 0x200,
    LINAPPLE_KEY_F2,
    LINAPPLE_KEY_F3,
    LINAPPLE_KEY_F4,
    LINAPPLE_KEY_F5,
    LINAPPLE_KEY_F6,
    LINAPPLE_KEY_F7,
    LINAPPLE_KEY_F8,
    LINAPPLE_KEY_F9,
    LINAPPLE_KEY_F10,
    LINAPPLE_KEY_F11,
    LINAPPLE_KEY_F12,

    // Numpad
    LINAPPLE_KEY_KP_0 = 0x300,
    LINAPPLE_KEY_KP_1,
    LINAPPLE_KEY_KP_2,
    LINAPPLE_KEY_KP_3,
    LINAPPLE_KEY_KP_4,
    LINAPPLE_KEY_KP_5,
    LINAPPLE_KEY_KP_6,
    LINAPPLE_KEY_KP_7,
    LINAPPLE_KEY_KP_8,
    LINAPPLE_KEY_KP_9,
    LINAPPLE_KEY_KP_PLUS,
    LINAPPLE_KEY_KP_MINUS,
    LINAPPLE_KEY_KP_MULTIPLY,
    LINAPPLE_KEY_KP_DIVIDE,
    LINAPPLE_KEY_KP_ENTER,
    LINAPPLE_KEY_KP_PERIOD,

    // Modifiers (when needed as individual keys)
    LINAPPLE_KEY_LSHIFT = 0x400,
    LINAPPLE_KEY_RSHIFT,
    LINAPPLE_KEY_LCTRL,
    LINAPPLE_KEY_RCTRL,
    LINAPPLE_KEY_LALT,
    LINAPPLE_KEY_RALT,
    LINAPPLE_KEY_LGUI,
    LINAPPLE_KEY_RGUI,
    LINAPPLE_KEY_MENU
};

/**
 * @brief Set the state of a specific Apple II key code.
 *
 * @param apple_code The 7-bit Apple II keyboard code.
 * @param down True if the key is pressed, false if released.
 */
void Linapple_SetKeyState(uint8_t apple_code, bool down);

/**
 * @brief Set the state of the Apple II Caps Lock.
 */
void Linapple_SetCapsLockState(bool enabled);

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
using LinappleVideoCallback = void (*)(const uint32_t* pixels, int width, int height, int pitch);

/**
 * @brief Callback signature for audio sample updates.
 *
 * @param samples Pointer to interleaved 16-bit signed stereo samples.
 * @param num_samples Number of samples (frames * channels).
 */
using LinappleAudioCallback = void (*)(const int16_t* samples, size_t num_samples);

/**
 * @brief Callback signature for window title updates.
 *
 * @param title New window title.
 */
using LinappleTitleCallback = void (*)(const char* title);

/**
 * @brief Register a callback for video frame rendering.
 */
void Linapple_SetVideoCallback(LinappleVideoCallback cb);

/**
 * @brief Register a callback for audio sample output.
 */
void Linapple_SetAudioCallback(LinappleAudioCallback cb);

/**
 * @brief Register a callback for window title updates.
 */
void Linapple_SetTitleCallback(LinappleTitleCallback cb);


#ifdef __cplusplus
}
#endif

#endif // LINAPPLE_CORE_H
