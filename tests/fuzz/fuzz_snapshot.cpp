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

static_assert(sizeof(ApplewinSnapshot_t) == 131824,
              "ApplewinSnapshot_t size must match expected snapshot bounds");

extern "C" const char* __asan_default_options() {
  return "detect_leaks=0";
}

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
