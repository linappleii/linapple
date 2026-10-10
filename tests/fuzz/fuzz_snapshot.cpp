// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Snapshot.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/Peripheral.h"
#include "core/LinAppleCore.h"

// Disable LeakSanitizer leak detection: snapshot deserialization fuzzing
// exercises the entire emulator core via linapple_init(). Full static
// subsystems (ROM assets, color mix tables, audio mixer buffers, and internal
// peripheral singletons) are initialized once globally for the fuzzer process
// lifecycle and persist across iterations without per-iteration teardown.
// detect_leaks=0 suppresses process-exit leak warnings for these persistent
// singletons while retaining full ASan and UBSan memory safety validation
// during execution.
extern "C" const char* __asan_default_options() { return "detect_leaks=0"; }

// NOLINTNEXTLINE(modernize-use-trailing-return-type) - libFuzzer C entrypoint
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Fixed-body input receives zeroed trailer; longer input exercises trailer
  // deserializer.
  if (size < snapshot_size_fixed_body) {
    return 0;
  }

  static bool initialized = false;
  if (!initialized) {
    linapple_init();
    initialized = true;
  }

  auto snapshot = std::unique_ptr<Snapshot>(new Snapshot());
  std::memcpy(snapshot.get(), data, std::min(size, sizeof(Snapshot)));

  // Verify deserializing arbitrary/corrupted memory snapshots does not crash or
  // corrupt host state
  static_cast<void>(snapshot_deserialize(snapshot.get()));

  return 0;
}
