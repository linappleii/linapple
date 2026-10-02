// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/SuperSerialFrontend.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"

namespace {

constexpr int k_slot_count = 7;

struct SlotLine_t {
  bool in_use = false;
};

std::array<SlotLine_t, k_slot_count> g_slots{};

auto slot_line(int slot) -> SlotLine_t* {
  if (slot < 1 || slot > k_slot_count) {
    return nullptr;
  }
  return &g_slots.at(static_cast<size_t>(slot - 1));
}

auto sink_open(void* ctx, int slot, PeripheralSinkKind_t kind) -> void {
  (void)ctx;
  SlotLine_t* line = slot_line(slot);
  if (line != nullptr) {
    line->in_use = kind == peripheral_sink_serial;
  }
}

auto sink_write(void* ctx, int slot, uint8_t byte) -> void {
  (void)ctx;
  (void)slot;
  (void)byte;
}

// On a serial line "ready" is "the device is open"; there is none yet.
auto sink_ready(void* ctx, int slot) -> bool {
  (void)ctx;
  (void)slot;
  return false;
}

auto sink_close(void* ctx, int slot) -> void {
  (void)ctx;
  SlotLine_t* line = slot_line(slot);
  if (line != nullptr) {
    line->in_use = false;
  }
}

auto sink_tick(void* ctx) -> void { (void)ctx; }

auto sink_read(void* ctx, int slot, uint8_t* byte) -> bool {
  (void)ctx;
  (void)slot;
  (void)byte;
  return false;
}

auto sink_set_line(void* ctx, int slot, const PeripheralSerialLine_t* line)
    -> void {
  (void)ctx;
  (void)slot;
  (void)line;
}

auto sink_get_lines(void* ctx, int slot, uint8_t* lines) -> bool {
  (void)ctx;
  (void)slot;
  (void)lines;
  return false;
}

const ByteSink_t g_serial_sink = {.open = sink_open,
                                  .write = sink_write,
                                  .ready = sink_ready,
                                  .close = sink_close,
                                  .tick = sink_tick,
                                  .read = sink_read,
                                  .set_line = sink_set_line,
                                  .get_lines = sink_get_lines};

}  // namespace

auto super_serial_frontend_sink() -> const ByteSink_t& { return g_serial_sink; }
