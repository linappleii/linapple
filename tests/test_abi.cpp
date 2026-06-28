#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/Peripheral.h"
#include "core/LinAppleCore.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include <cstring>
#include <thread>
#include <atomic>

// --- Dummy Peripheral Implementation ---

static bool g_dummy_reset_called = false;
static bool g_dummy_shutdown_called = false;
static HostInterface_t* g_captured_host = nullptr;

static uint32_t g_last_cmd_id = 0;
static uint8_t g_last_cmd_data[PERIPHERAL_CMD_MAX_DATA]{};
static size_t g_last_cmd_data_size = 0;
static std::atomic<int> g_cmd_call_count{0};

using DummyInstance_t = struct {
    uint8_t last_val;
    HostInterface_t* host;
};

static auto Dummy_IORead(void* instance, uint16_t pc, uint16_t addr, uint8_t write, uint8_t val, uint32_t cycles) -> uint8_t {
    (void)pc; (void)addr; (void)write; (void)val; (void)cycles;
    return (static_cast<DummyInstance_t*>(instance))->last_val;
}

static auto Dummy_IOWrite(void* instance, uint16_t pc, uint16_t addr, uint8_t write, uint8_t val, uint32_t cycles) -> uint8_t {
    (void)pc; (void)addr; (void)write; (void)cycles;
    (static_cast<DummyInstance_t*>(instance))->last_val = val;
    (static_cast<DummyInstance_t*>(instance))->host->Log(instance, LOG_INFO, "Wrote %02X", val);
    return 0;
}

static auto Dummy_Init(int slot, HostInterface_t* host) -> void* {
    g_captured_host = host;
    auto* inst = new DummyInstance_t();
    inst->last_val = 0;
    inst->host = host;

    // Register I/O for $C0nX
    host->RegisterIO(slot, Dummy_IORead, Dummy_IOWrite, nullptr, nullptr);

    // Register expansion ROM $Cn00
    static uint8_t dummy_rom[256];
    memset(dummy_rom, 0xA5, 256);
    host->RegisterCxROM(slot, dummy_rom);

    return inst;
}

static void Dummy_Reset(void* instance) {
    (void)instance;
    g_dummy_reset_called = true;
}

static void Dummy_Shutdown(void* instance) {
    g_dummy_shutdown_called = true;
    delete static_cast<DummyInstance_t*>(instance);
}

static auto Dummy_Command(void* /*instance*/, uint32_t cmd_id, const void* data, size_t size) -> PeripheralStatus {
    g_last_cmd_id = cmd_id;
    g_last_cmd_data_size = size;
    if (size > 0 && data) {
        memcpy(g_last_cmd_data, data, size);
    }
    ++g_cmd_call_count;
    return PERIPHERAL_OK;
}

static auto Dummy_Query(void* /*instance*/, uint32_t /*cmd_id*/, void* out, size_t* out_size) -> PeripheralStatus {
    const uint32_t response = 0xDEADBEEF;
    if (out && out_size && *out_size >= sizeof(response)) {
        memcpy(out, &response, sizeof(response));
        *out_size = sizeof(response);
        return PERIPHERAL_OK;
    }
    return PERIPHERAL_ERROR;
}

static Peripheral_t g_dummy_peripheral = {
    LINAPPLE_ABI_VERSION,
    "test.dummy",
    "Dummy Peripheral",
    "A test dummy peripheral",
    "Test Author",
    "1.0.0",
    0xFE, // Slots 1-7
    -1,   // No default
    Dummy_Init,
    Dummy_Reset,
    Dummy_Shutdown,
    nullptr, // think
    nullptr, // on_vblank
    nullptr, // save
    nullptr, // load
    Dummy_Command,
    Dummy_Query,
};

// --- Test Cases ---

TEST_CASE("ABI: [ABI-01] Peripheral Registration and Lifecycle") {
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;
    MemInitialize();
    SetMemMode(GetMemMode() | MF_SLOTCXROM); // Enable slot ROM
    Peripheral_Manager_Init();

    g_dummy_reset_called = false;
    g_dummy_shutdown_called = false;

    int result = Peripheral_Register(&g_dummy_peripheral, 2);
    CHECK(result == 0);

    // Verify I/O works
    // Slot 2 I/O is at $C0A0 - $C0AF
    IOMap_Dispatch(0, 0xC0A0, 1, 0x42, 0); // Write $42
    uint8_t val = IOMap_Dispatch(0, 0xC0A0, 0, 0, 0); // Read
    CHECK(val == 0x42);

    // Verify Expansion ROM works
    // Slot 2 ROM is at $C200 - $C2FF.
    uint8_t* pCxRom = MemGetCxRomPeripheral();
    CHECK(pCxRom[0x200] == 0xA5);

    // Verify Reset propagation
    Peripheral_Manager_Reset();
    CHECK(g_dummy_reset_called == true);

    // Verify Shutdown
    Peripheral_Manager_Shutdown();
    CHECK(g_dummy_shutdown_called == true);
}

TEST_CASE("ABI: [ABI-02] ABI Version Validation") {
    Peripheral_t bad_abi = g_dummy_peripheral;
    bad_abi.abi_version = 999;

    Peripheral_Manager_Init();
    int result = Peripheral_Register(&bad_abi, 3);
    CHECK(result == -1);
}

TEST_CASE("ABI: [ABI-03] Slot Compatibility Validation") {
    Peripheral_t slot_specific = g_dummy_peripheral;
    slot_specific.compatible_slots = (1 << 4); // Only Slot 4

    Peripheral_Manager_Init();

    // Register in Slot 2 (Should fail)
    int result = Peripheral_Register(&slot_specific, 2);
    CHECK(result == -1);

    // Register in Slot 4 (Should succeed)
    result = Peripheral_Register(&slot_specific, 4);
    CHECK(result == 0);

    Peripheral_Manager_Shutdown();
}

TEST_CASE("ABI: [ABI-04] HostInterface GetConfig stub returns false") {
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;
    MemInitialize();
    Peripheral_Manager_Init();
    Peripheral_Register(&g_dummy_peripheral, 2);

    REQUIRE(g_captured_host != nullptr);
    char buf[32];
    CHECK(g_captured_host->GetConfig("section", "key", buf, sizeof(buf)) == false);
    CHECK(g_captured_host->GetConfig("", "", buf, sizeof(buf)) == false);

    Peripheral_Manager_Shutdown();
}

TEST_CASE("ABI: [ABI-05] HostInterface new callbacks are callable without crashing") {
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;
    MemInitialize();
    Peripheral_Manager_Init();
    Peripheral_Register(&g_dummy_peripheral, 2);

    REQUIRE(g_captured_host != nullptr);
    g_captured_host->SetConfig("section", "key", "value");
    g_captured_host->NotifyStatusChanged(2);
    g_captured_host->NotifyActivityChanged(2, true);
    g_captured_host->NotifyActivityChanged(2, false);
    g_captured_host->RequestPreciseTiming();

    Peripheral_Manager_Shutdown();
}

TEST_CASE("ABI: [ABI-06] Peripheral_Command dispatches to peripheral on Think") {
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;
    MemInitialize();
    Peripheral_Manager_Init();
    Peripheral_Register(&g_dummy_peripheral, 2);

    g_last_cmd_id = 0;
    g_last_cmd_data_size = 0;
    g_cmd_call_count = 0;

    const uint32_t payload = 0xCAFEBABE;
    PeripheralStatus status = Peripheral_Command(2, 0x0001, &payload, sizeof(payload));
    CHECK(status == PERIPHERAL_OK);
    CHECK(g_cmd_call_count == 0); // not yet delivered

    Peripheral_Manager_Think(0);
    CHECK(g_cmd_call_count == 1);
    CHECK(g_last_cmd_id == 0x0001);
    CHECK(g_last_cmd_data_size == sizeof(payload));
    uint32_t received = 0;
    memcpy(&received, g_last_cmd_data, sizeof(received));
    CHECK(received == 0xCAFEBABE);

    Peripheral_Manager_Shutdown();
}

TEST_CASE("ABI: [ABI-07] Peripheral_Command rejects oversized payload") {
    Peripheral_Manager_Init();

    const size_t oversized = PERIPHERAL_CMD_MAX_DATA + 1;
    uint8_t buf[oversized]{};
    PeripheralStatus status = Peripheral_Command(2, 0x0001, buf, oversized);
    CHECK(status == PERIPHERAL_ERROR);
}

TEST_CASE("ABI: [ABI-08] Peripheral_Command silently skips peripheral with no command handler") {
    Peripheral_t no_cmd = g_dummy_peripheral;
    no_cmd.command = nullptr;

    g_Apple2Type = A2TYPE_APPLE2EENHANCED;
    MemInitialize();
    Peripheral_Manager_Init();
    Peripheral_Register(&no_cmd, 2);

    g_cmd_call_count = 0;
    const uint32_t payload = 0x01;
    Peripheral_Command(2, 0x0001, &payload, sizeof(payload));
    Peripheral_Manager_Think(0); // must not crash
    CHECK(g_cmd_call_count == 0);

    Peripheral_Manager_Shutdown();
}

TEST_CASE("ABI: [ABI-09] Peripheral_Query returns peripheral data synchronously") {
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;
    MemInitialize();
    Peripheral_Manager_Init();
    Peripheral_Register(&g_dummy_peripheral, 2);

    uint32_t result = 0;
    size_t out_size = sizeof(result);
    PeripheralStatus status = Peripheral_Query(2, 0x0005, &result, &out_size);
    CHECK(status == PERIPHERAL_OK);
    CHECK(result == 0xDEADBEEF);

    Peripheral_Manager_Shutdown();
}

TEST_CASE("ABI: [ABI-10] Peripheral_Command is thread-safe") {
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;
    MemInitialize();
    Peripheral_Manager_Init();
    Peripheral_Register(&g_dummy_peripheral, 2);

    g_cmd_call_count = 0;
    const int THREADS = 4;
    const int CMDS_PER_THREAD = 50;
    const uint32_t payload = 0x01;

    std::vector<std::thread> threads;
    threads.reserve(THREADS);
    for (int i = 0; i < THREADS; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < CMDS_PER_THREAD; ++j) {
                Peripheral_Command(2, 0x0001, &payload, sizeof(payload));
            }
        });
    }
    for (auto& t : threads) t.join();

    Peripheral_Manager_Think(0);
    CHECK(g_cmd_call_count == THREADS * CMDS_PER_THREAD);

    Peripheral_Manager_Shutdown();
}
