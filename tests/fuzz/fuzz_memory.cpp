// SPDX-License-Identifier: GPL-2.0-only
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"

extern "C" const char* __asan_default_options() { return "detect_leaks=1"; }

// NOLINTNEXTLINE(modernize-use-trailing-return-type) - libFuzzer C entrypoint
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static bool initialized = false;
  if (!initialized) {
    current_apple2_type = A2TYPE_APPLE2EENHANCED;
    video_initialize();
    mem_initialize();
    cpu_initialize();
    initialized = true;
  }

  // Consume byte stream as a sequence of 3-byte softswitch commands:
  // [uint8_t ss_offset, uint8_t val, uint8_t write_flag]
  constexpr size_t cmd_size = 3;
  for (size_t i = 0; i + cmd_size <= size; i += cmd_size) {
    uint16_t addr = 0xC000 | data[i];
    uint8_t val = data[i + 1];
    uint8_t write_flag = data[i + 2] & 1;

    io_map_dispatch(0x1000, addr, write_flag, val, 0);

    // Invariant: Base RAM (0x00..0xBF) must always remain valid and non-null
    for (uint16_t page = 0; page < PAGE_C0; ++page) {
      assert(mem != nullptr);
      assert(memwrite != nullptr);
      assert(memwrite[page] != nullptr);
    }
  }

  return 0;
}
