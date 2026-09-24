// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

auto peripheral_register_internal() -> void;
auto peripheral_plugins_init(const char* plugin_dir = nullptr) -> void;
auto peripheral_plugins_shutdown() -> void;
auto peripheral_find_internal(const char* name) -> Peripheral_t*;
auto peripheral_get_plugin_path(const char* name) -> const char*;
auto peripheral_is_any_active() -> bool;

// Test hook: inject frozen host clock provider.
typedef bool (*LocalTimeProvider_t)(void* ctx, HostLocalTime_t* out);
auto linapple_set_local_time_provider(LocalTimeProvider_t provider, void* ctx)
    -> void;

// The frontend behind HostInterface_t's sink members. The tokens cards hold
// are the bridge's own and keyed by slot, so one vtable serves every card and
// can be installed or replaced while cards hold them: the bridge opens a slot
// through the installed vtable on its first write or readiness poll, closes
// every open slot through the outgoing vtable when another is installed, and
// reopens lazily under the new one. open is bound to no I/O: it only tells the
// sink which slot and kind will follow, so ready stays a state query; the
// destination is opened inside write, and a failure there is what makes ready
// false. tick runs once per batch of emulated cycles, before any card's think,
// and is where a sink that fell over retries; it may be NULL. A missing member
// otherwise reads as a sink that is not there for that call: the write is
// dropped and the slot is not ready.
typedef struct {
  void (*open)(void* ctx, int slot, PeripheralSinkKind_t kind);
  void (*write)(void* ctx, int slot, uint8_t byte);
  bool (*ready)(void* ctx, int slot);
  void (*close)(void* ctx, int slot);
  void (*tick)(void* ctx);
} ByteSink_t;

// A vtable and the context it was installed with, returned together so that a
// guard putting the previous sink back restores both.
typedef struct {
  const ByteSink_t* vtable;
  void* ctx;
} ByteSinkBinding_t;

// Installs the sink behind the host interface and returns what it replaced.
// nullptr uninstalls: every token stays valid, writes are dropped and no slot
// is ready.
auto linapple_set_byte_sink(const ByteSink_t* vtable, void* ctx)
    -> ByteSinkBinding_t;

#ifdef __cplusplus
}
#endif
