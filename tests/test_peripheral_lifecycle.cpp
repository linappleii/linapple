#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "apple2/Memory.h"

static bool g_mock_shutdown_called = false;

static auto Mock_Init(int slot, HostInterface_t* host) -> void* {
    (void)slot;
    g_mock_shutdown_called = false;
    // We'll use a dummy pointer as the instance.
    void* instance = (void*)0xDEADBEEF;
    host->RegisterDirectIO(instance, 0xC000,
        [](void* instance, uint16_t, uint16_t, uint8_t, uint8_t, uint32_t) -> uint8_t {
            if (instance == (void*)0xDEADBEEF && !g_mock_shutdown_called) {
                return 0xAA;
            }
            return 0xEE;
        }, nullptr);
    return instance;
}

static auto Mock_Shutdown(void* instance) -> void {
    if (instance == (void*)0xDEADBEEF) {
        g_mock_shutdown_called = true;
    }
}

static Peripheral_t g_mock_peripheral = {
    LINAPPLE_ABI_VERSION,
    "MockPeripheral",
    0xFF,
    Mock_Init,
    nullptr, // reset
    Mock_Shutdown,
    nullptr, // think
    nullptr, // on_vblank
    nullptr, // save_state
    nullptr, // load_state
    nullptr, // command
    nullptr  // query
};

TEST_CASE("Peripheral Manager: Direct IO handlers are cleared during re-init") {
    Linapple_Init();

    // 1. Initial setup
    Peripheral_Manager_Init();
    Peripheral_Register(&g_mock_peripheral, 1);

    // Verify it works
    CHECK(IOMap_Dispatch(0, 0xC000, 0, 0, 0) == 0xAA);

    // 2. Re-init
    // The bug is that Peripheral_Manager_Init calls ClearAllPeripherals()
    // which frees instances, but hasn't yet zeroed g_num_direct_handlers.
    // If we call IOMap_Dispatch after ClearAllPeripherals() but before
    // g_num_direct_handlers = 0, we get a UAF or access to stale instance.

    Peripheral_Manager_Init();

    // After Init, the old handler should be gone.
    // In the buggy version, if we hadn't called the second part of Init,
    // this would hit the lambda with a stale instance or 0xDEADBEEF but g_mock_shutdown_called=true.

    // Actually, IOMap_Dispatch should return floating bus (0) or default IO_Null if no handler is found.
    // IO_Null returns MemReadFloatingBus which might be non-zero but usually predictable in tests.
    uint8_t val = IOMap_Dispatch(0, 0xC000, 0, 0, 0);
    CHECK(val != 0xAA);
    CHECK(val != 0xEE); // 0xEE would mean it called the old handler after shutdown
}
