#ifndef LINAPPLE_PERIPHERAL_INTERNAL_H
#define LINAPPLE_PERIPHERAL_INTERNAL_H

#include "Peripheral.h"

// Forward declarations for migrated peripherals
#if defined(ENABLE_PERIPHERAL_MOCKINGBOARD)
extern Peripheral_t g_mockingboard_peripheral;
#endif

#if defined(ENABLE_PERIPHERAL_SPEAKER)
extern Peripheral_t g_speaker_peripheral;
#endif

#if defined(ENABLE_PERIPHERAL_KEYBOARD)
extern Peripheral_t keyboard_peripheral;
#endif

#if defined(ENABLE_PERIPHERAL_DISK)
extern Peripheral_t g_disk_peripheral;
#endif

#if defined(ENABLE_PERIPHERAL_SSC)
extern Peripheral_t g_ssc_peripheral;
#endif

#if defined(ENABLE_PERIPHERAL_PRINTER)
extern Peripheral_t g_printer_peripheral;
#endif

#if defined(ENABLE_PERIPHERAL_HARDDISK)
extern Peripheral_t g_harddisk_peripheral;
#endif

#if defined(ENABLE_PERIPHERAL_MOUSE)
extern Peripheral_t g_mouse_peripheral;
#endif

#if defined(ENABLE_PERIPHERAL_CLOCK)
extern Peripheral_t g_clock_peripheral;
#endif

/**
 * @brief Register all built-in peripherals with the manager.
 */
void Peripheral_Register_Internal(void);

/**
 * @brief Initialize dynamic peripheral plugins.
 */
void Peripheral_Plugins_Init(void);

/**
 * @brief Shutdown and unload dynamic peripheral plugins.
 */
void Peripheral_Plugins_Shutdown(void);

/**
 * @brief Find a built-in or loaded peripheral by name.
 * @param name The name of the peripheral to find.
 * @return Pointer to the peripheral definition, or nullptr if not found.
 */
Peripheral_t* Peripheral_Find_Internal(const char* name);

/**
 * @brief Get the file path of a dynamically loaded peripheral.
 * @param name The name of the peripheral.
 * @return The file path, or nullptr if built-in or not found.
 */
const char* Peripheral_GetPluginPath(const char* name);

/**
 * @brief Check if any peripheral is currently active (e.g. activity LED on).
 * @return true if any peripheral has reported activity.
 */
#ifdef __cplusplus
extern "C" {
#endif
bool Peripheral_IsAnyActive(void);
#ifdef __cplusplus
}
#endif

#endif  // LINAPPLE_PERIPHERAL_INTERNAL_H
