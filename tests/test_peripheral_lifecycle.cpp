#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
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
    "test.mock",
    "MockPeripheral",
    "Description",
    "Author",
    "1.0.0",
    0xFF,
    -1,
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
    linapple_init();

    // 1. Initial setup
    peripheral_manager_init();
    peripheral_register(&g_mock_peripheral, 1);

    // Verify it works
    CHECK(io_map_dispatch(0, 0xC000, 0, 0, 0) == 0xAA);

    // 2. Re-init
    // The bug is that peripheral_manager_init calls clear_all_peripherals()
    // which frees instances, but hasn't yet zeroed g_num_direct_handlers.
    // If we call io_map_dispatch after clear_all_peripherals() but before
    // g_num_direct_handlers = 0, we get a UAF or access to stale instance.

    peripheral_manager_init();

    // After Init, the old handler should be gone.
    // In the buggy version, if we hadn't called the second part of Init,
    // this would hit the lambda with a stale instance or 0xDEADBEEF but g_mock_shutdown_called=true.

    // Actually, io_map_dispatch should return floating bus (0) or default io_null if no handler is found.
    // io_null returns mem_read_floating_bus which might be non-zero but usually predictable in tests.
    uint8_t val = io_map_dispatch(0, 0xC000, 0, 0, 0);
    CHECK(val != 0xAA);
    CHECK(val != 0xEE); // 0xEE would mean it called the old handler after shutdown

    linapple_shutdown();
}

TEST_CASE("Peripheral Manager: Direct IO handlers are cleared when a peripheral is unregistered") {
    linapple_init();
    peripheral_manager_init();

    // 1. Register
    peripheral_register(&g_mock_peripheral, 1);
    CHECK(io_map_dispatch(0, 0xC000, 0, 0, 0) == 0xAA);

    // 2. Unregister
    peripheral_unregister(1);

    // After unregistering slot 1, the mock peripheral is gone.
    // Any direct IO handlers it registered should also be gone.
    // In the buggy version, the handler remains and will call the lambda with shutdown=true.
    uint8_t val = io_map_dispatch(0, 0xC000, 0, 0, 0);
    CHECK(val != 0xAA);
    CHECK(val != 0xEE); // 0xEE would mean it called the old handler after shutdown

    linapple_shutdown();
}

TEST_CASE("Peripheral Manager: host_get_config lifetime") {
    linapple_init();
    peripheral_manager_init();

    // We need a way to get the HostInterface_t.
    // We can use a dummy peripheral and register it.
    static char captured_val1[32];
    static char captured_val2[32];

    static Peripheral_t test_api_config = {
        LINAPPLE_ABI_VERSION,
        "test.config",
        "ConfigTest",
        "Desc",
        "Author",
        "1.0.0",
        0xFF,
        -1,
        [](int slot, HostInterface_t* host) -> void* {
            (void)slot;
            host->GetConfig("Peripheral", "TestKey1", captured_val1, sizeof(captured_val1));
            host->GetConfig("Peripheral", "TestKey2", captured_val2, sizeof(captured_val2));
            return (void*)0x1234;
        },
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
    };

    // Set some config values
    config_save_string("Peripheral", "TestKey1", "Value1");
    config_save_string("Peripheral", "TestKey2", "Value2");

    peripheral_register(&test_api_config, 1);

    // Now they should both be correct because they have their own buffers.
    CHECK(std::string(captured_val2) == "Value2");
    CHECK(std::string(captured_val1) == "Value1");

    linapple_shutdown();
}

#include <dlfcn.h>
#include "core/Util_Path.h"

TEST_CASE("Peripheral Manager: Plugin path construction") {
    // This test verifies that we can construct a valid path even if the directory
    // doesn't have a trailing slash.
    std::string dir = "/tmp/linapple-test";
    std::string file = "plugin.so";

    std::string fullPath = Path::join(dir, file);
    CHECK(fullPath == "/tmp/linapple-test/plugin.so");

    // Test with trailing slash already present
    dir = "/tmp/linapple-test/";
    fullPath = Path::join(dir, file);
    CHECK(fullPath == "/tmp/linapple-test/plugin.so");

    // Test with empty dir
    CHECK(Path::join("", file) == file);

    // Test with empty file
    CHECK(Path::join(dir, "") == dir);
}

TEST_CASE("Peripheral Manager: Command payload capacity") {
    linapple_init();
    peripheral_manager_init();

    static size_t captured_size = 0;
    static uint8_t last_byte = 0;

    static Peripheral_t test_api = {
        LINAPPLE_ABI_VERSION,
        "test.max_payload",
        "MaxPayloadTest",
        "Desc",
        "Author",
        "1.0.0",
        0xFF,
        -1,
        [](int, HostInterface_t*) -> void* { return (void*)0x1; },
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        [](void*, uint32_t, const void* data, size_t size) -> PeripheralStatus_t {
            captured_size = size;
            if (size > 0) {
                last_byte = static_cast<const uint8_t*>(data)[size-1];
            }
            return peripheral_ok;
        },
        nullptr
    };

    peripheral_register(&test_api, 1);

    // 1. Send exactly PERIPHERAL_CMD_MAX_DATA (512) bytes
    std::vector<uint8_t> payload(512, 0xAA);
    payload.back() = 0xBB;

    PeripheralStatus_t status = peripheral_command(1, 0x123, payload.data(), payload.size());
    CHECK(status == peripheral_ok);

    // Commands are queued and processed during Think(0)
    peripheral_manager_think(0);

    CHECK(captured_size == 512);
    CHECK(last_byte == 0xBB);

    // 2. Send 513 bytes - should be rejected
    std::vector<uint8_t> huge_payload(513, 0xCC);
    status = peripheral_command(1, 0x124, huge_payload.data(), huge_payload.size());
    CHECK(status == peripheral_error);

    linapple_shutdown();
}
