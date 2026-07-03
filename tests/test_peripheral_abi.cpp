#include "doctest.h"
#include "core/Peripheral.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "apple2/Memory.h"

// Define the global peripheral provided by the C file
extern "C" Peripheral_t g_test_c_peripheral;

TEST_CASE("Peripheral ABI: Registration and I/O") {
    // We need to initialize memory system for IOMap_Dispatch to work
    // though for this specific test we might just want to test Peripheral_Register
    // and the proxies.

    Peripheral_Manager_Init();

    // Register the C peripheral in Slot 1
    int result = Peripheral_Register(&g_test_c_peripheral, 1);
    CHECK(result == 0);

    // Verify I/O dispatch
    // In Slot 1, C090-C09F are the addresses
    // We wrote a simple handler that returns what was written.

    // Write 0x42 to $C090
    IOMap_Dispatch(0x0000, 0xC090, 1, 0x42, 0);

    // Read from $C090
    uint8_t val = IOMap_Dispatch(0x0000, 0xC090, 0, 0, 0);
    CHECK(val == 0x42);

    // Reset and check it cleared
    Peripheral_Manager_Reset();
    val = IOMap_Dispatch(0x0000, 0xC090, 0, 0, 0);
    CHECK(val == 0);

    Peripheral_Manager_Shutdown();
}
