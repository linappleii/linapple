#include "doctest.h"
#include "core/Peripheral.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "apple2/Memory.h"
#include "apple2/KeyboardCommands.h"
#include <cstring>
#include <map>

extern Peripheral_t keyboard_peripheral;

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
    void* instance = keyboard_peripheral.init(0, &mock_host);
    REQUIRE(instance != nullptr);

    // Verify $C000-$C01F are registered
    CHECK(g_mock_handlers.count(0xC000) > 0);
    CHECK(g_mock_handlers.count(0xC010) > 0);

    keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Strobe and Latch Behavior") {
    g_mock_handlers.clear();
    void* instance = keyboard_peripheral.init(0, &mock_host);

    // 1. Initially, strobe should be clear
    uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK((val & 0x80) == 0);

    // 2. Simulate 'A' key down
    KeyboardEvent_t ev = {'a', 1};
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &ev, sizeof(ev));

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
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &ev, sizeof(ev));

    // 8. Bit 7 of $C010 should now be clear
    val = g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);
    CHECK((val & 0x80) == 0);

    keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Multiple keys and ASCII 0") {
    g_mock_handlers.clear();
    void* instance = keyboard_peripheral.init(0, &mock_host);

    // 1. Press 'A'
    KeyboardEvent_t evA = {'a', 1};
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &evA, sizeof(evA));
    CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) != 0);

    // 2. Press 'B'
    KeyboardEvent_t evB = {'b', 1};
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &evB, sizeof(evB));
    CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) != 0);

    // 3. Release 'A' - Bit 7 should STILL be set because 'B' is down
    evA.is_down = 0;
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &evA, sizeof(evA));
    CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) != 0);

    // 4. Release 'B' - Bit 7 should now be clear
    evB.is_down = 0;
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &evB, sizeof(evB));
    CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) == 0);

    // 5. Test ASCII 0 (Ctrl-@)
    KeyboardEvent_t evCtrlAt = {0, 1};
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &evCtrlAt, sizeof(evCtrlAt));
    // Bit 7 should be set for ASCII 0 too
    CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) != 0);

    evCtrlAt.is_down = 0;
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &evCtrlAt, sizeof(evCtrlAt));
    CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) == 0);

    keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Repeat key logic") {
    g_mock_handlers.clear();
    void* instance = keyboard_peripheral.init(0, &mock_host);

    // 1. Press 'A'
    KeyboardEvent_t evA = {'a', 1};
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &evA, sizeof(evA));

    // 2. Press 'B'
    KeyboardEvent_t evB = {'b', 1};
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &evB, sizeof(evB));

    // 3. Wait for repeat (KEY_REPEAT_INITIAL_DELAY = 512000 cycles)
    // First, clear the strobe so we can see it being set again
    g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);

    keyboard_peripheral.think(instance, 600000);
    // Strobe should be set now (repeating 'B')
    uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK((val & 0x80) != 0);
    CHECK((val & 0x7F) == 'b');

    // 4. Release 'A' (while 'B' is still held)
    evA.is_down = 0;
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &evA, sizeof(evA));

    // 5. Clear strobe and wait for another repeat
    g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);
    keyboard_peripheral.think(instance, 100000); // Repeat rate is 68000

    // In BUGGY code, 'B' will NO LONGER REPEAT because release of 'A' cleared repeat_key!
    val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK((val & 0x80) != 0); // Fails in buggy code
    CHECK((val & 0x7F) == 'b');

    keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: International character safety") {
    g_mock_handlers.clear();
    void* instance = keyboard_peripheral.init(0, &mock_host);

    // 1. Press 'é' as a hardware-accurate 7-bit code (0x7B)
    // This is how Map_FR now stores it.
    KeyboardEvent_t ev = {0x7B, 1};
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &ev, sizeof(ev));

    uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK((val & 0x7F) == 0x7B);

    // 2. Press a stray Latin-1 code (0xE9) - should be rejected/ignored
    // This verifies we don't accidentally mask it to 0x69 ('i').
    g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0); // clear strobe
    ev.ascii = 0xE9;
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &ev, sizeof(ev));

    val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK((val & 0x80) == 0); // Strobe NOT set because event was ignored
    CHECK((val & 0x7F) == 0x7B); // Latch still holds previous valid key

    keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Repeat timer overflow and large batch safety") {
    g_mock_handlers.clear();
    // Ensure we are in a mode that supports auto-repeat
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;

    void* instance = keyboard_peripheral.init(0, &mock_host);

    // Press 'A'
    KeyboardEvent_t ev = {'a', 1};
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &ev, sizeof(ev));

    // 1. Verify basic wrap-around safety (what was in issue 288)
    // Clear strobe so we can detect the repeat
    g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);

    // Pass a huge cycle count that would cause wrap-around if added naively.
    // Result should trigger a repeat if correctly clamped or handled.
    keyboard_peripheral.think(instance, 400000);
    uint32_t huge_cycles = 0xFFFFFFFFU - 300000U;
    keyboard_peripheral.think(instance, huge_cycles);

    uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK((val & 0x80) != 0); // Strobe should be set by repeat

    // 2. Verify large batch performance/safety (O(1) modulo)
    // Even if we passed a huge number without clamping, modulo would keep it fast.
    // Since we still clamp in Think, this is mostly checking the state is valid.
    g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0); // clear strobe
    keyboard_peripheral.think(instance, 0xFFFFFFFFU);
    val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK((val & 0x80) != 0); // Should fire again

    keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Ctrl+@ (NUL) handling") {
    g_mock_handlers.clear();
    void* instance = keyboard_peripheral.init(0, &mock_host);

    // Verify NUL (Ctrl+@) works through direct command
    KeyboardEvent_t ev = {0x00, 1};
    keyboard_peripheral.command(instance, KEYB_CMD_EVENT, &ev, sizeof(ev));

    // Bit 7 should be set (strobe), bits 0-6 should be 0
    uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
    CHECK((val & 0x80) != 0);
    CHECK((val & 0x7F) == 0x00);

    keyboard_peripheral.shutdown(instance);
}
