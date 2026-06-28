// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/Peripheral_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LINAPPLE_ABI_VERSION 0

enum {
  PERIPHERAL_CMD_MAX_DATA = 512,
  PERIPHERAL_MASK_INTERNAL = 0x01,
  PERIPHERAL_MASK_EXPANSION = 0xFE
};

typedef uint8_t (*PeripheralIOHandler)(void* instance, uint16_t pc,
                                       uint16_t addr, uint8_t write,
                                       uint8_t val, uint32_t cycles_left);

typedef PeripheralIOHandler PeripheralIoHandler_t;

typedef struct {
  void (*Log)(void* instance, PeripheralLogLevel level, const char* fmt, ...);
  void (*AssertIrq)(int slot, bool assert);
  void (*RegisterIO)(int slot, PeripheralIOHandler readC0,
                     PeripheralIOHandler writeC0, PeripheralIOHandler readCx,
                     PeripheralIOHandler writeCx);
  void (*RegisterCxROM)(int slot, uint8_t* rom_ptr);
  void (*RegisterExpansionROM)(int slot, uint8_t* rom_ptr);
  void (*RegisterDirectIO)(void* instance, uint16_t addr,
                           PeripheralIOHandler read, PeripheralIOHandler write);
  uint8_t* (*GetMemPtr)(uint16_t addr);
  uint64_t (*GetCycles)(void);
  bool (*GetConfig)(const char* section, const char* key, char* buffer,
                    size_t buffer_size);
  void (*SetConfig)(const char* section, const char* key, const char* value);
  void (*NotifyStatusChanged)(int slot);
  void (*NotifyActivityChanged)(int slot, bool active);
  void (*RequestPreciseTiming)(void);
  void (*AudioPushSamples)(void* instance, const int16_t* buffer,
                           size_t num_samples);
  void (*ResetSystem)(void* instance);
  void (*PrinterPutChar)(void* instance, uint8_t c);
  uint8_t (*PrinterGetStatus)(void* instance);
  void (*SerialTransmitByte)(void* instance, uint8_t byte);
  void (*SerialUpdateState)(void* instance, uint32_t baud, uint32_t bits,
                            int parity, int stop);
} HostInterface_t;

// Forward declaration
struct Peripheral_t;

typedef struct Peripheral_t {
  int abi_version;
  const char* id;           // Namespaced ID (e.g. "linapple.disk_ii")
  const char* name;         // Human readable name
  const char* description;  // Short summary
  const char* author;       // Implementation author
  const char* version;      // Implementation version
  uint8_t compatible_slots;
  int8_t default_slot;  // Preferred slot (1-7), 0 for internal, or -1 for any
  void* (*init)(int slot, HostInterface_t* host);
  void (*reset)(void* instance);
  void (*shutdown)(void* instance);
  void (*think)(void* instance, uint32_t cycles);
  void (*on_vblank)(void* instance, bool vblank);
  PeripheralStatus (*save_state)(void* instance, void* buffer, size_t* size);
  PeripheralStatus (*load_state)(void* instance, const void* buffer,
                                 size_t size);
  PeripheralStatus (*command)(void* instance, uint32_t cmd_id, const void* data,
                              size_t size);
  PeripheralStatus (*query)(void* instance, uint32_t cmd_id, void* out,
                            size_t* out_size);
} Peripheral_t;

#ifdef BUILD_SHARED_PERIPHERAL
#define PERIPHERAL_REGISTER(peripheral_struct)                     \
  extern "C" {                                                     \
  Peripheral_t linapple_peripheral_descriptor = peripheral_struct; \
  }
#else
#ifdef __cplusplus
#define PERIPHERAL_REGISTER(peripheral_struct)              \
  namespace {                                               \
  struct PeripheralRegistration_##peripheral_struct {       \
    PeripheralRegistration_##peripheral_struct() noexcept { \
      Peripheral_Register_Builtin(                          \
          const_cast<Peripheral_t*>(&(peripheral_struct))); \
    }                                                       \
  } g_registration_##peripheral_struct;                     \
  }
#else
#define PERIPHERAL_REGISTER(peripheral_struct)                              \
  __attribute__((constructor)) static void Register_##peripheral_struct() { \
    Peripheral_Register_Builtin(&(peripheral_struct));                      \
  }
#endif
#endif

#define EXPORT_PERIPHERAL(peripheral_struct) \
  PERIPHERAL_REGISTER(peripheral_struct)

int Peripheral_Register(Peripheral_t* api, int slot);
void Peripheral_Register_Builtin(Peripheral_t* api);
int Peripheral_Unregister(int slot);
PeripheralStatus Peripheral_Command(int slot, uint32_t cmd_id, const void* data,
                                    size_t size);
PeripheralStatus Peripheral_Query(int slot, uint32_t cmd_id, void* out,
                                  size_t* out_size);
void Peripheral_SaveState(int slot, void* buffer, size_t* size);
void Peripheral_LoadState(int slot, const void* buffer, size_t size);
void Peripheral_SaveStateByName(int slot, const char* name, void* buffer,
                                size_t* size);
void Peripheral_LoadStateByName(int slot, const char* name, const void* buffer,
                                size_t size);
void Peripheral_GetManifest(void* manifest);
bool Peripheral_VerifyManifest(const void* manifest);

#ifdef __cplusplus
}
#endif
