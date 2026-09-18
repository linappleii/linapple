// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(modernize-use-trailing-return-type,
// cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "apple2/chips/6522.h"

namespace {

// One register access and one run of the timers.
constexpr size_t record_size = 5;

// The interrupt line as the 6502 sees it: bit 7 of IFR, which the register
// read derives rather than stores. Reading IFR and IER has no side effect, so
// polling them cannot perturb what is being measured.
auto bus_visible_irq(Via6522_t* v) -> bool {
  return (via_read(v, via_reg::ifr) & 0x80) != 0;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Via6522_t via;
  via_reset(&via);

  for (size_t offset = 0; offset + record_size <= size; offset += record_size) {
    const uint8_t* record = data + offset;
    const uint8_t op = record[0];

    if ((op & 0x80) != 0) {
      via_reset(&via);
    }
    if ((op & 0x01) != 0) {
      via_write(&via, record[1], record[2]);
    } else {
      via_read(&via, record[1]);
    }

    const bool line_before = bus_visible_irq(&via);
    const uint8_t flags_before = via_read(&via, via_reg::ifr) & 0x7F;
    const uint32_t cycles =
        static_cast<uint32_t>(record[3]) | (static_cast<uint32_t>(record[4])
                                            << 8);
    const bool changed = via_step(&via, cycles);
    const bool line_after = bus_visible_irq(&via);
    const uint8_t flags_after = via_read(&via, via_reg::ifr) & 0x7F;

    // The card calls the host only on the value this returns, so a return that
    // disagrees with the line the bus shows is a lost or a spurious interrupt.
    assert(changed == (line_before != line_after));

    // Only the 6502 clears a flag, by reading a counter or writing IFR. Time
    // passing can raise one and can never lower one.
    assert((flags_before & ~flags_after) == 0);

    // Bit 7 of IER always reads set; the rest is whatever was enabled.
    assert((via_read(&via, via_reg::ier) & 0x80) != 0);
  }

  return 0;
}
// NOLINTEND(modernize-use-trailing-return-type,
// cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
