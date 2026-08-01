#include <cstring>
#include <map>

#include "apple2/Memory.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Peripheral.h"
#include "doctest.h"

#include "apple2/peripherals/keyboard/Keyboard.h"
static auto& keyboard_peripheral = *keyboard_get_descriptor();

// Mock host interface for testing
struct MockHandler {
  void* instance;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
};

static std::map<uint16_t, MockHandler> g_mock_handlers;

static void Mock_RegisterDirectIO(void* instance, uint16_t addr,
                                  PeripheralIOHandler read,
                                  PeripheralIOHandler write) {
  g_mock_handlers[addr] = {instance, read, write};
}

static HostInterface_t mock_host = [] {
    HostInterface_t h{};
    h.RegisterDirectIO = Mock_RegisterDirectIO;
    return h;
}();

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

  // 2. Disable caps lock and simulate 'A' key down
  uint8_t caps = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_set_caps, &caps, 1);
  KeyboardEvent_t ev = {'a', 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

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
  CHECK((val & 0x80) != 0);  // 'a' is still down

  // 7. Release 'a'
  ev.is_down = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

  // 8. Bit 7 of $C010 should now be clear
  val = g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);
  CHECK((val & 0x80) == 0);

  keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Multiple keys and ASCII 0") {
  g_mock_handlers.clear();
  void* instance = keyboard_peripheral.init(0, &mock_host);

  // 1. Press 'A'
  uint8_t caps = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_set_caps, &caps, 1);
  KeyboardEvent_t evA = {'a', 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &evA, sizeof(evA));
  CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) !=
        0);

  // 2. Press 'B'
  KeyboardEvent_t evB = {'b', 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &evB, sizeof(evB));
  CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) !=
        0);

  // 3. Release 'A' - Bit 7 should STILL be set because 'B' is down
  evA.is_down = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &evA, sizeof(evA));
  CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) !=
        0);

  // 4. Release 'B' - Bit 7 should now be clear
  evB.is_down = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &evB, sizeof(evB));
  CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) ==
        0);

  // 5. Test ASCII 0 (Ctrl-@)
  KeyboardEvent_t evCtrlAt = {0, 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &evCtrlAt,
                              sizeof(evCtrlAt));
  // Bit 7 should be set for ASCII 0 too
  CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) !=
        0);

  evCtrlAt.is_down = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &evCtrlAt,
                              sizeof(evCtrlAt));
  CHECK((g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0) & 0x80) ==
        0);

  keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Repeat key logic") {
  g_mock_handlers.clear();
  void* instance = keyboard_peripheral.init(0, &mock_host);

  // 1. Press 'A'
  uint8_t caps = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_set_caps, &caps, 1);
  KeyboardEvent_t evA = {'a', 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &evA, sizeof(evA));

  // 2. Press 'B'
  KeyboardEvent_t evB = {'b', 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &evB, sizeof(evB));

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
  keyboard_peripheral.command(instance, keyboard_cmd_event, &evA, sizeof(evA));

  // 5. Clear strobe and wait for another repeat
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);
  keyboard_peripheral.think(instance, 100000);  // Repeat rate is 68000

  // In BUGGY code, 'B' will NO LONGER REPEAT because release of 'A' cleared
  // repeat_key!
  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x80) != 0);  // Fails in buggy code
  CHECK((val & 0x7F) == 'b');

  keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: International character safety") {
  g_mock_handlers.clear();
  void* instance = keyboard_peripheral.init(0, &mock_host);
  uint8_t caps = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_set_caps, &caps, 1);

  // 1. Press a valid code (0x7B = '{')
  KeyboardEvent_t ev = {0x7B, 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

  uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 0x7B);
  CHECK((val & 0x80) != 0);

  // 2. Press a stray Latin-1 code (0xE9) - should be rejected/ignored
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);  // clear strobe
  ev.key = 0xE9;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x80) == 0);  // Strobe NOT set because event was ignored

  // 3. Positional mapping test (e.g. LINAPPLE_KEY_POS_A = 0x504)
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);  // clear strobe
  ev.key = 0x504;
  ev.mod_shift = 0;
  ev.mod_ctrl = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 'a');
  CHECK((val & 0x80) != 0);

  // 4. Positional mapping with Shift (A -> 0x504 + Shift)
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);  // clear strobe
  ev.key = 0x504;
  ev.mod_shift = 1;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 'A');
  CHECK((val & 0x80) != 0);

  // 5. Positional mapping with Ctrl (Ctrl-A -> 0x504 + Ctrl)
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);  // clear strobe
  ev.key = 0x504;
  ev.mod_shift = 0;
  ev.mod_ctrl = 1;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 0x01);
  CHECK((val & 0x80) != 0);

  // 6. Symbolic Arrow key tests
  // Up Arrow (LINAPPLE_KEY_UP = 0x100)
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);
  ev.key = 0x100;
  ev.mod_shift = 0;
  ev.mod_ctrl = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));
  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 0x0B);

  // Down Arrow (LINAPPLE_KEY_DOWN = 0x101)
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);
  ev.key = 0x101;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));
  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 0x0A);

  // Left Arrow (LINAPPLE_KEY_LEFT = 0x102)
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);
  ev.key = 0x102;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));
  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 0x08);

  // Right Arrow (LINAPPLE_KEY_RIGHT = 0x103)
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);
  ev.key = 0x103;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));
  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 0x15);

  keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Repeat timer overflow and large batch safety") {
  g_mock_handlers.clear();
  // Ensure we are in a mode that supports auto-repeat
  g_apple2_type = A2TYPE_APPLE2EENHANCED;

  void* instance = keyboard_peripheral.init(0, &mock_host);

  // Press 'A'
  uint8_t caps = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_set_caps, &caps, 1);
  KeyboardEvent_t ev = {'a', 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

  // 1. Verify basic wrap-around safety (what was in issue 288)
  // Clear strobe so we can detect the repeat
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);

  // Pass a huge cycle count that would cause wrap-around if added naively.
  // Result should trigger a repeat if correctly clamped or handled.
  keyboard_peripheral.think(instance, 400000);
  uint32_t huge_cycles = 0xFFFFFFFFU - 300000U;
  keyboard_peripheral.think(instance, huge_cycles);

  uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x80) != 0);  // Strobe should be set by repeat

  // 2. Verify large batch performance/safety (O(1) modulo)
  // Even if we passed a huge number without clamping, modulo would keep it
  // fast. Since we still clamp in Think, this is mostly checking the state is
  // valid.
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);  // clear strobe
  keyboard_peripheral.think(instance, 0xFFFFFFFFU);
  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x80) != 0);  // Should fire again

  keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Ctrl+@ (NUL) handling") {
  g_mock_handlers.clear();
  void* instance = keyboard_peripheral.init(0, &mock_host);

  // Verify NUL (Ctrl+@) works through direct command
  KeyboardEvent_t ev = {0x00, 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

  // Bit 7 should be set (strobe), bits 0-6 should be 0
  uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x80) != 0);
  CHECK((val & 0x7F) == 0x00);

  keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Apple Keys and Modifiers Hardware Read") {
  // Ensure 'mem' is allocated so mem_read_floating_bus doesn't segfault
  mem = new uint8_t[65536]();

  g_mock_handlers.clear();
  void* instance = keyboard_peripheral.init(0, &mock_host);

  KeyboardModifiers_t mods = {0, 0, 0, 0, 0, {0, 0, 0}};

  // 1. Initially all should be clear
  keyboard_peripheral.command(instance, keyboard_cmd_set_mods, &mods, sizeof(mods));
  CHECK((g_mock_handlers[0xC061].read(instance, 0, 0xC061, 0, 0, 0) & 0x80) == 0);
  CHECK((g_mock_handlers[0xC062].read(instance, 0, 0xC062, 0, 0, 0) & 0x80) == 0);
  CHECK((g_mock_handlers[0xC063].read(instance, 0, 0xC063, 0, 0, 0) & 0x80) == 0);

  // 2. Set GUI -> Open Apple ($C061)
  mods.gui = 1;
  keyboard_peripheral.command(instance, keyboard_cmd_set_mods, &mods, sizeof(mods));
  CHECK((g_mock_handlers[0xC061].read(instance, 0, 0xC061, 0, 0, 0) & 0x80) != 0);
  CHECK((g_mock_handlers[0xC062].read(instance, 0, 0xC062, 0, 0, 0) & 0x80) == 0);

  // 3. Set Alt -> Both Open and Closed Apple
  mods.gui = 0;
  mods.alt = 1;
  keyboard_peripheral.command(instance, keyboard_cmd_set_mods, &mods, sizeof(mods));
  CHECK((g_mock_handlers[0xC061].read(instance, 0, 0xC061, 0, 0, 0) & 0x80) != 0);
  CHECK((g_mock_handlers[0xC062].read(instance, 0, 0xC062, 0, 0, 0) & 0x80) != 0);

  // 4. Set Shift -> $C063
  mods.alt = 0;
  mods.shift = 1;
  keyboard_peripheral.command(instance, keyboard_cmd_set_mods, &mods, sizeof(mods));
  CHECK((g_mock_handlers[0xC061].read(instance, 0, 0xC061, 0, 0, 0) & 0x80) == 0);
  CHECK((g_mock_handlers[0xC062].read(instance, 0, 0xC062, 0, 0, 0) & 0x80) == 0);
  CHECK((g_mock_handlers[0xC063].read(instance, 0, 0xC063, 0, 0, 0) & 0x80) != 0);

  keyboard_peripheral.shutdown(instance);
  delete[] mem;
  mem = nullptr;
}

TEST_CASE("Keyboard Peripheral: Rocker Switch and Alternate Layout") {
  g_mock_handlers.clear();
  void* instance = keyboard_peripheral.init(0, &mock_host);

  // Clear Caps Lock for accurate testing
  uint8_t caps = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_set_caps, &caps, 1);

  // 1. Set alternate layout to French
  uint8_t layout = keyboard_layout_fr;
  keyboard_peripheral.command(instance, keyboard_cmd_set_layout, &layout, 1);

  // 2. Enable rocker switch
  uint8_t rocker = 1;
  keyboard_peripheral.command(instance, keyboard_cmd_set_rocker, &rocker, 1);

  // 3. Press positional keyb_idx_2 (LINAPPLE_KEY_2 = 0x51F)
  KeyboardEvent_t ev = {0x51F, 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

  // Verify 'é' (0x7B)
  uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 0x7B);

  // Release key and clear strobe
  ev.is_down = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);

  // 4. Disable rocker switch
  rocker = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_set_rocker, &rocker, 1);

  // 5. Press positional keyb_idx_2 again
  ev.is_down = 1;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev, sizeof(ev));

  // Verify fallback to US '2' (0x32)
  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 0x32);

  keyboard_peripheral.shutdown(instance);
}

TEST_CASE("Keyboard Peripheral: Caps Lock Behavior") {
  g_mock_handlers.clear();
  void* instance = keyboard_peripheral.init(0, &mock_host);

  // 1. Enable Caps Lock
  uint8_t caps = 1;
  keyboard_peripheral.command(instance, keyboard_cmd_set_caps, &caps, 1);

  // Symbolic 'a'
  KeyboardEvent_t ev_sym = {'a', 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev_sym, sizeof(ev_sym));
  uint8_t val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 'A');
  
  ev_sym.is_down = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev_sym, sizeof(ev_sym));
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);

  // Positional 'a' (LINAPPLE_KEY_A = 0x504)
  KeyboardEvent_t ev_pos = {0x504, 1, 0, 0, 0, 0, {0, 0, 0}};
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev_pos, sizeof(ev_pos));
  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 'A');

  ev_pos.is_down = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev_pos, sizeof(ev_pos));
  g_mock_handlers[0xC010].read(instance, 0, 0xC010, 0, 0, 0);

  // 2. Disable Caps Lock
  caps = 0;
  keyboard_peripheral.command(instance, keyboard_cmd_set_caps, &caps, 1);

  // Positional 'a' again
  ev_pos.is_down = 1;
  keyboard_peripheral.command(instance, keyboard_cmd_event, &ev_pos, sizeof(ev_pos));
  val = g_mock_handlers[0xC000].read(instance, 0, 0xC000, 0, 0, 0);
  CHECK((val & 0x7F) == 'a');

  keyboard_peripheral.shutdown(instance);
}
