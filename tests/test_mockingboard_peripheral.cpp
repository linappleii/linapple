// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "LinAppleCore.h"
#include "core/Peripheral.h"

// Mock Host Interface
static bool irq_asserted = false;
static void Mock_AssertIrq(int slot, bool assert) {
  (void)slot;
  irq_asserted = assert;
}

static bool Mock_GetConfig(const char* section, const char* key, char* buffer,
                           size_t buffer_size) {
  (void)section;
  (void)key;
  (void)buffer;
  (void)buffer_size;
  return false;
}

static void Mock_RegisterIO(int slot, PeripheralIOHandler r,
                            PeripheralIOHandler w, PeripheralIOHandler cr,
                            PeripheralIOHandler cw) {
  (void)slot;
  (void)r;
  (void)w;
  (void)cr;
  (void)cw;
}

static HostInterface_t mock_host = {
    nullptr,        // Log
    Mock_AssertIrq, // AssertIrq
    Mock_RegisterIO, // RegisterIO
    nullptr,        // RegisterCxROM
    nullptr,        // RegisterExpansionROM
    nullptr,        // RegisterDirectIO
    nullptr,        // GetMemPtr
    nullptr,        // GetCycles
    Mock_GetConfig, // GetConfig
    nullptr,        // SetConfig
    nullptr,        // NotifyStatusChanged
    nullptr,        // NotifyActivityChanged
    nullptr,        // RequestPreciseTiming
    nullptr,        // RiffInitWriteFile
    nullptr,        // RiffFinishWriteFile
    nullptr,        // RiffPutSamples
    nullptr,        // AudioPushSamples
    nullptr         // ResetSystem
};

extern Peripheral_t g_mockingboard_peripheral;

TEST_CASE("Mockingboard Peripheral ABI") {
  irq_asserted = false;
  void* instance = g_mockingboard_peripheral.init(4, &mock_host);
  REQUIRE(instance != nullptr);

  SUBCASE("Reset") {
    g_mockingboard_peripheral.reset(instance);
    CHECK(irq_asserted == false);
  }

  SUBCASE("Lifecycle") {
    g_mockingboard_peripheral.shutdown(instance);
  }
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
