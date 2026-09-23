// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/Peripheral_Types.h"

// NOLINTBEGIN(modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-macro-usage, modernize-use-trailing-return-type, modernize-redundant-void-arg)

#ifdef __cplusplus
extern "C" {
#endif

enum {
  LINAPPLE_ABI_VERSION = 1,
  PERIPHERAL_CMD_MAX_DATA = 512,
  PERIPHERAL_MASK_INTERNAL = 0x01,
  PERIPHERAL_MASK_EXPANSION = 0xFE
};

typedef uint8_t (*PeripheralIOHandler)(void* instance, uint16_t pc,
                                       uint16_t addr, uint8_t write,
                                       uint8_t val, uint32_t executed_cycles);

typedef PeripheralIOHandler PeripheralIoHandler_t;

// A device that is strobed but never drives the data bus. It needs no address,
// write flag, data byte, or cycle count: the bridge has already brought the
// cumulative cycle count up to the current instruction, so GetCycles() inside
// the handler is exact. executed_cycles is what a handler that answers a read
// with the floating bus cannot do without, because the byte the bus holds is
// whatever the video scanner is fetching on that very cycle.
typedef void (*PeripheralStrobeHandler_t)(void* instance);

// The host's clock as a card sees it. Every broken-down field is already
// local time: the host applied its zone before filling them, so a card copies
// them into its registers and applies no offset of its own. unix_seconds is
// the same instant as seconds since 1970-01-01 00:00:00 UTC, and
// utc_offset_seconds is local minus UTC for that instant, so a card that
// needs UTC can recover it. weekday counts from Sunday = 0 like tm_wday.
typedef struct {
  int64_t unix_seconds;
  int32_t utc_offset_seconds;
  uint16_t year;
  uint8_t month;        /* 1..12 */
  uint8_t day_of_month; /* 1..31 */
  uint8_t weekday;      /* 0 = Sunday .. 6 = Saturday */
  uint8_t hour;         /* 0..23 */
  uint8_t minute;       /* 0..59 */
  uint8_t second;       /* 0..59 */
} HostLocalTime_t;

typedef struct {
  void (*Log)(void* instance, PeripheralLogLevel_t level, const char* fmt, ...);
  void (*AssertIrq)(int slot, bool assert);
  void (*RegisterIO)(int slot, PeripheralIOHandler readC0,
                     PeripheralIOHandler writeC0, PeripheralIOHandler readCx,
                     PeripheralIOHandler writeCx);
  // The host copies the page out, so a card hands over its ROM image as the
  // constant it is.
  void (*RegisterCxROM)(int slot, const uint8_t* rom_ptr);
  void (*RegisterExpansionROM)(int slot, uint8_t* rom_ptr);
  void (*RegisterDirectIO)(void* instance, uint16_t addr,
                           PeripheralIOHandler read, PeripheralIOHandler write);
  void (*RegisterDirectIOStrobe)(void* instance, uint16_t addr,
                                 PeripheralStrobeHandler_t on_strobe);
  uint8_t* (*get_mem_ptr)(uint16_t addr);
  uint64_t (*GetCycles)(void);
  double (*GetClockHz)(void);
  bool (*GetConfig)(const char* section, const char* key, char* buffer,
                    size_t buffer_size);
  void (*SetConfig)(const char* section, const char* key, const char* value);
  void (*NotifyStatusChanged)(int slot);
  void (*NotifyActivityChanged)(int slot, bool active);
  void (*AudioPushChannels)(void* instance, const float* const* channel_buffers,
                            size_t num_channels, size_t num_samples);
  void (*ResetSystem)(void* instance);
  void (*PrinterPutChar)(void* instance, uint8_t c);
  uint8_t (*PrinterGetStatus)(void* instance);
  void (*SerialTransmitByte)(void* instance, uint8_t byte);
  void (*SerialUpdateState)(void* instance, uint32_t baud, uint32_t bits,
                            int parity, int stop);
  // The byte an undriven data bus holds is whatever the video scanner is
  // fetching on that cycle, so a card that answers a read without driving the
  // bus has to ask the motherboard what is on it. Appended last: a prebuilt
  // plugin compiled against an older header still finds every member it knows
  // at the offset it expects.
  uint8_t (*ReadFloatingBus)(uint32_t executed_cycles);
  // A clock card cannot know the host's time; it can only be told. Routing it
  // through the host is what lets a test freeze the clock the card reads.
  // Returns false, leaving *out untouched, when the host has no clock to
  // offer. Appended last for the same reason as ReadFloatingBus.
  bool (*GetLocalTime)(HostLocalTime_t* out);
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
  PeripheralStatus_t (*save_state)(void* instance, void* buffer, size_t* size);
  PeripheralStatus_t (*load_state)(void* instance, const void* buffer,
                                   size_t size);
  PeripheralStatus_t (*command)(void* instance, uint32_t cmd_id,
                                const void* data, size_t size);
  PeripheralStatus_t (*query)(void* instance, uint32_t cmd_id, void* out,
                              size_t* out_size);
} Peripheral_t;

#ifdef BUILD_SHARED_PERIPHERAL
#define PERIPHERAL_REGISTER(peripheral_struct)                     \
  extern "C" {                                                     \
  Peripheral_t linapple_peripheral_descriptor = peripheral_struct; \
  }
#else
#ifdef __cplusplus
#define PERIPHERAL_REGISTER(peripheral_struct)               \
  namespace {                                                \
  struct PeripheralRegistration_t##peripheral_struct {       \
    PeripheralRegistration_t##peripheral_struct() noexcept { \
      peripheral_register_builtin(                           \
          const_cast<Peripheral_t*>(&(peripheral_struct)));  \
    }                                                        \
  } g_registration_##peripheral_struct;                      \
  }
#else
#define PERIPHERAL_REGISTER(peripheral_struct)                              \
  __attribute__((constructor)) static void Register_##peripheral_struct() { \
    peripheral_register_builtin(&(peripheral_struct));                      \
  }
#endif
#endif

#define EXPORT_PERIPHERAL(peripheral_struct) \
  PERIPHERAL_REGISTER(peripheral_struct)

int peripheral_register(Peripheral_t* api, int slot);
void peripheral_register_builtin(Peripheral_t* api);
int peripheral_unregister(int slot);
PeripheralStatus_t peripheral_command(int slot, uint32_t cmd_id,
                                      const void* data, size_t size);
PeripheralStatus_t peripheral_query(int slot, uint32_t cmd_id, void* out,
                                    size_t* out_size);
/* Slot 0 holds several peripherals, so the target is named by descriptor
 * id; the slot stays because two slots can hold the same card. Commands are
 * queued and delivered on the emulation thread. The lookup of the name reads
 * the peripheral table unlocked, so call from the emulation thread. */
PeripheralStatus_t peripheral_command_by_id(int slot, const char* peripheral_id,
                                            uint32_t cmd_id, const void* data,
                                            size_t size);
PeripheralStatus_t peripheral_query_by_id(int slot, const char* peripheral_id,
                                          uint32_t cmd_id, void* out,
                                          size_t* out_size);
void peripheral_save_state(int slot, void* buffer, size_t* size);
void peripheral_load_state(int slot, const void* buffer, size_t size);
void peripheral_save_state_by_name(int slot, const char* name, void* buffer,
                                   size_t* size);
void peripheral_load_state_by_name(int slot, const char* name,
                                   const void* buffer, size_t size);
void peripheral_get_manifest(void* manifest);
bool peripheral_verify_manifest(const void* manifest);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-macro-usage, modernize-use-trailing-return-type, modernize-redundant-void-arg)
