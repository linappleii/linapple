#include "doctest.h"
#include "core/Peripheral.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "apple2/Memory.h"
#include <vector>
#include <cstdio>

// Mock peripherals for testing Slot 0
static int p1_resets = 0;
static int p2_resets = 0;
static int p1_thinks = 0;
static int p2_thinks = 0;

static void* Mock1_Init(int slot, HostInterface_t* host) {
    (void)slot; (void)host;
    return (void*)0x1111;
}
static void Mock1_Reset(void* instance) {
    if (instance == (void*)0x1111) p1_resets++;
}
static void Mock1_Think(void* instance, uint32_t cycles) {
    (void)cycles;
    if (instance == (void*)0x1111) p1_thinks++;
}

static void* Mock2_Init(int slot, HostInterface_t* host) {
    (void)slot; (void)host;
    return (void*)0x2222;
}
static void Mock2_Reset(void* instance) {
    if (instance == (void*)0x2222) p2_resets++;
}
static void Mock2_Think(void* instance, uint32_t cycles) {
    (void)cycles;
    if (instance == (void*)0x2222) p2_thinks++;
}

static Peripheral_t g_mock1 = {
    LINAPPLE_ABI_VERSION,
    "test.mock1",
    "Mock1",
    "Desc",
    "Author",
    "1.0.0",
    0x01, // Compatible with Slot 0
    0,
    Mock1_Init,
    Mock1_Reset,
    nullptr, // Shutdown
    Mock1_Think,
    nullptr, // VBlank
    nullptr, // Save
    nullptr, // Load
    nullptr, // Command
    nullptr  // Query
};

static Peripheral_t g_mock2 = {
    LINAPPLE_ABI_VERSION,
    "test.mock2",
    "Mock2",
    "Desc",
    "Author",
    "1.0.0",
    0x01, // Compatible with Slot 0
    0,
    Mock2_Init,
    Mock2_Reset,
    nullptr, // Shutdown
    Mock2_Think,
    nullptr, // VBlank
    nullptr, // Save
    nullptr, // Load
    nullptr, // Command
    nullptr  // Query
};

TEST_CASE("Peripheral Slot 0: Multi-Occupancy") {
    p1_resets = p2_resets = 0;
    p1_thinks = p2_thinks = 0;
    g_mock1.compatible_slots = PERIPHERAL_MASK_INTERNAL;
    g_mock2.compatible_slots = PERIPHERAL_MASK_INTERNAL;

    peripheral_manager_init();

    // Register first peripheral in Slot 0
    CHECK(peripheral_register(&g_mock1, 0) == 0);
    // Register second peripheral in Slot 0
    CHECK(peripheral_register(&g_mock2, 0) == 0);

    // Verify both receive Reset
    peripheral_manager_reset();
    CHECK(p1_resets == 1);
    CHECK(p2_resets == 1);

    // Verify both receive Think
    peripheral_manager_think(100);
    CHECK(p1_thinks == 1);
    CHECK(p2_thinks == 1);

    // Verify Slot 1 does NOT support multi-occupancy
    Peripheral_t local_mock1 = g_mock1;
    local_mock1.compatible_slots = 0x02; // Slot 1
    Peripheral_t local_mock2 = g_mock2;
    local_mock2.compatible_slots = 0x02; // Slot 1

    CHECK(peripheral_register(&local_mock1, 1) == 0);
    CHECK(peripheral_register(&local_mock2, 1) == -1); // Should fail

    peripheral_manager_shutdown();
}

static HostInterface_t* captured_host = nullptr;

TEST_CASE("Peripheral ABI: host_reset_system") {
    p1_resets = 0;
    captured_host = nullptr;
    peripheral_manager_init();

    Peripheral_t trigger_p = g_mock1;
    trigger_p.compatible_slots = PERIPHERAL_MASK_INTERNAL;
    trigger_p.init = [](int slot, HostInterface_t* host) -> void* {
        (void)slot;
        captured_host = host;
        return (void*)0x1111;
    };
    trigger_p.reset = Mock1_Reset;
    trigger_p.shutdown = nullptr;

    CHECK(peripheral_register(&trigger_p, 0) == 0);

    REQUIRE(captured_host != nullptr);
    REQUIRE(captured_host->ResetSystem != nullptr);

    // Trigger reset via host interface
    captured_host->ResetSystem((void*)0x1111);

    // ResetSystem calls peripheral_manager_reset which calls our mock reset
    CHECK(p1_resets == 1);

    peripheral_manager_shutdown();
}
