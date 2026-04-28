#include "doctest.h"
#include "core/Peripheral.h"
#include "apple2/KeyboardCommands.h"
#include <cstring>
#include <map>

extern Peripheral_t g_keyboard_peripheral;

// Mock host interface for testing
struct MockHandler {
    void* instance;
    PeripheralIOHandler read;
    PeripheralIOHandler write;
};

static std::map<uint16_t, MockHandler> g_mock_handlers;

static void Mock_RegisterDirectIO(void* instance, uint16_t addr, PeripheralIOHandler read, PeripheralIOHandler write) {
    g_mock_handlers[addr] = {instance, read, write};
}

static HostInterface_t mock_host = {
    nullptr, nullptr, nullptr, nullptr, nullptr,
    Mock_RegisterDirectIO,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

TEST_CASE("Keyboard Peripheral: Lifecycle and I/O Registration") {
    g_mock_handlers.clear();
    void* instance = g_keyboard_peripheral.init(0, &mock_host);
    REQUIRE(instance != nullptr);

    // Verify $C000-$C01F are registered
    CHECK(g_mock_handlers.count(0xC000) > 0);
    CHECK(g_mock_handlers.count(0xC010) > 0);

    g_keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Strobe and Latch Behavior") {
    g_mock_handlers.clear();
    void* instance = g_keyboard_peripheral.init(0, &mock_host);

    // 1. Initially, strobe should be clear
    uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK((val & 0x80) == 0);

    // 2. Simulate 'A' key down
    KeyboardEvent_t ev = {'a', 1};
    g_keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &ev, sizeof(ev));

    // 3. Read $C000: expect 'a' (0x61) | Strobe (0x80) = 0xE1
    val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK(val == 0xE1);

    // 4. Any access to $C010 should clear the strobe
    g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);

    // 5. Read $C000 again: strobe should be clear, latch remains 'a'
    val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK(val == 0x61);

    // 6. Check Any-Key-Down flag at $C010 (Bit 7)
    val = g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);
    CHECK((val & 0x80) != 0); // 'a' is still down

    // 7. Release 'a'
    ev.is_down = 0;
    g_keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &ev, sizeof(ev));

    // 8. Bit 7 of $C010 should now be clear
    val = g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);
    CHECK((val & 0x80) == 0);

    g_keyboard_peripheral.shutdown(instance);
}
