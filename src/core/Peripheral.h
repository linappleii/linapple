/*
 * Peripheral.h - The LinApple Peripheral ABI
 *
 * This header defines the stable binary interface for emulator peripherals.
 * Peripherals implemented against this ABI can be statically linked or
 * loaded as shared objects.
 */

#ifndef LINAPPLE_PERIPHERAL_H
#define LINAPPLE_PERIPHERAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINAPPLE_ABI_VERSION 1

enum { PERIPHERAL_CMD_MAX_DATA = 512 };

/**
 * @brief Standard return codes for ABI functions.
 */
typedef enum {
    PERIPHERAL_OK = 0,
    PERIPHERAL_ERROR = -1,
    PERIPHERAL_INCOMPATIBLE = -2
} PeripheralStatus;

/**
 * @brief Log levels for peripherals.
 */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} PeripheralLogLevel;

/**
 * @brief Mask indicating compatibility with any slot (0-7).
 */
#define LINAPPLE_ANY_SLOT_MASK 0xFF

/**
 * @brief Standard I/O handler signature.
 */
typedef uint8_t (*PeripheralIOHandler)(void* instance, uint16_t pc, uint16_t addr, uint8_t write, uint8_t val, uint32_t cycles_left);

/**
 * @brief Services provided by the emulator core to the peripheral.
 */
typedef struct {
    void (*Log)(void* instance, PeripheralLogLevel level, const char* fmt, ...);
    void (*AssertIrq)(int slot, bool assert);
    void (*RegisterIO)(int slot, PeripheralIOHandler readC0, PeripheralIOHandler writeC0,
                                 PeripheralIOHandler readCx, PeripheralIOHandler writeCx);
    void (*RegisterCxROM)(int slot, uint8_t* rom_ptr);
    void (*RegisterExpansionROM)(int slot, uint8_t* rom_ptr);
    void (*RegisterDirectIO)(void* instance, uint16_t addr, PeripheralIOHandler read, PeripheralIOHandler write);
    uint8_t* (*GetMemPtr)(uint16_t addr);
    uint64_t (*GetCycles)(void);

    // The returned pointer is valid until the next GetConfig call.
    const char* (*GetConfig)(const char* section, const char* key);

    void (*SetConfig)(const char* section, const char* key, const char* value);

    void (*NotifyStatusChanged)(int slot);

    // Call only on state changes.
    void (*NotifyActivityChanged)(int slot, bool active);

    void (*RequestPreciseTiming)(void);

    // Audio Logging (Riff)
    int (*RiffInitWriteFile)(char *pszFile, uint32_t sample_rate, uint32_t NumChannels);
    int (*RiffFinishWriteFile)(void);
    int (*RiffPutSamples)(short *buf, uint32_t uSamples);

    // Modern Audio Routing
    void (*AudioPushSamples)(void* instance, const int16_t* buffer, size_t num_samples);
} HostInterface_t;

/**
 * @brief The interface a peripheral must implement.
 */
typedef struct {
    int abi_version;
    const char* name;
    uint32_t compatible_slots;
    void* (*init)(int slot, HostInterface_t* host);
    void (*reset)(void* instance);
    void (*shutdown)(void* instance);
    void (*think)(void* instance, uint32_t cycles);
    void (*on_vblank)(void* instance, bool vblank);
    PeripheralStatus (*save_state)(void* instance, void* buffer, size_t* size);
    PeripheralStatus (*load_state)(void* instance, const void* buffer, size_t size);
    // NULL if the peripheral does not accept commands; called via Peripheral_Command().
    PeripheralStatus (*command)(void* instance, uint32_t cmd_id, const void* data, size_t size);
    // NULL if the peripheral does not support queries; called via Peripheral_Query().
    PeripheralStatus (*query)(void* instance, uint32_t cmd_id, void* out, size_t* out_size);
} Peripheral_t;

#define EXPORT_PERIPHERAL(peripheral_struct) \
    extern "C" { \
        Peripheral_t linapple_peripheral_descriptor = peripheral_struct; \
    }

#ifdef __cplusplus
}
#endif

#endif // LINAPPLE_PERIPHERAL_H
