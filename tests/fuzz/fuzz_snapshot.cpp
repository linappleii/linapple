// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Snapshot.h"
#include "apple2/SnapshotTypes.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"

// Disable LeakSanitizer leak detection: snapshot deserialization fuzzing
// exercises the entire emulator core via linapple_init(). Full static
// subsystems (ROM assets, color mix tables, audio mixer buffers, and internal
// peripheral singletons) are initialized once globally for the fuzzer process
// lifecycle and persist across iterations without per-iteration teardown.
// detect_leaks=0 suppresses process-exit leak warnings for these persistent
// singletons while retaining full ASan and UBSan memory safety validation
// during execution.
extern "C" const char* __asan_default_options() { return "detect_leaks=0"; }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < sizeof(ApplewinSnapshot_t)) {
    return 0;
  }

  static bool s_initialized = false;
  if (!s_initialized) {
    linapple_init();
    s_initialized = true;
  }

  auto snapshot = std::unique_ptr<ApplewinSnapshot_t>(new ApplewinSnapshot_t());
  std::memcpy(snapshot.get(), data, sizeof(ApplewinSnapshot_t));

  // Verify deserializing arbitrary/corrupted memory snapshots does not crash or
  // corrupt host state
  snapshot_deserialize(snapshot.get());

  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
