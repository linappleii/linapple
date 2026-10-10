// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/HostSink.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "frontends/common/PrinterFrontend.h"
#include "frontends/common/SuperSerialFrontend.h"

namespace {

constexpr int slot_count = 7;

std::array<PeripheralSinkKind, slot_count> active_sink_kinds{};

auto slot_index(int slot) -> size_t { return static_cast<size_t>(slot - 1); }

auto slot_is_valid(int slot) -> bool {
  return slot >= 1 && slot <= slot_count;
}

auto sink_for(int slot) -> const ByteSink* {
  if (!slot_is_valid(slot)) {
    return nullptr;
  }
  switch (active_sink_kinds.at(slot_index(slot))) {
    case peripheral_sink_printer:
      return &printer_frontend_sink();
    case peripheral_sink_serial:
      return &super_serial_frontend_sink();
    default:
      return nullptr;
  }
}

auto dispatch_open(void* ctx, int slot, PeripheralSinkKind kind) -> void {
  if (!slot_is_valid(slot)) {
    return;
  }
  active_sink_kinds.at(slot_index(slot)) = kind;
  const ByteSink* sink = sink_for(slot);
  if (sink != nullptr && sink->open != nullptr) {
    sink->open(ctx, slot, kind);
  }
}

auto dispatch_write(void* ctx, int slot, uint8_t byte) -> void {
  const ByteSink* sink = sink_for(slot);
  if (sink != nullptr && sink->write != nullptr) {
    sink->write(ctx, slot, byte);
  }
}

auto dispatch_ready(void* ctx, int slot) -> bool {
  const ByteSink* sink = sink_for(slot);
  return sink != nullptr && sink->ready != nullptr && sink->ready(ctx, slot);
}

auto dispatch_close(void* ctx, int slot) -> void {
  const ByteSink* sink = sink_for(slot);
  if (sink != nullptr && sink->close != nullptr) {
    sink->close(ctx, slot);
  }
  if (slot_is_valid(slot)) {
    active_sink_kinds.at(slot_index(slot)) = static_cast<PeripheralSinkKind>(0);
  }
}

// Open slots or not: the tick is where a device that fell over retries.
auto dispatch_tick(void* ctx) -> void {
  if (printer_frontend_sink().tick != nullptr) {
    printer_frontend_sink().tick(ctx);
  }
  if (super_serial_frontend_sink().tick != nullptr) {
    super_serial_frontend_sink().tick(ctx);
  }
}

auto dispatch_read(void* ctx, int slot, uint8_t* byte) -> bool {
  const ByteSink* sink = sink_for(slot);
  return sink != nullptr && sink->read != nullptr &&
         sink->read(ctx, slot, byte);
}

auto dispatch_set_line(void* ctx, int slot, const PeripheralSerialLine* line)
    -> void {
  const ByteSink* sink = sink_for(slot);
  if (sink != nullptr && sink->set_line != nullptr) {
    sink->set_line(ctx, slot, line);
  }
}

auto dispatch_get_lines(void* ctx, int slot, uint8_t* lines) -> bool {
  const ByteSink* sink = sink_for(slot);
  return sink != nullptr && sink->get_lines != nullptr &&
         sink->get_lines(ctx, slot, lines);
}

const ByteSink host_byte_sink = {
    .open = dispatch_open,
    .write = dispatch_write,
    .ready = dispatch_ready,
    .close = dispatch_close,
    .tick = dispatch_tick,
    .read = dispatch_read,
    .set_line = dispatch_set_line,
    .get_lines = dispatch_get_lines,
};

}  // namespace

auto host_sink_install() -> void {
  // The bridge closes every open slot through the outgoing sink, so on a
  // re-initialisation the previous run's devices close before their settings
  // change.
  linapple_set_byte_sink(&host_byte_sink, nullptr);
  active_sink_kinds.fill(static_cast<PeripheralSinkKind>(0));
}
