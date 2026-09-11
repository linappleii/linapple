// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>

#include "Apple2Types.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/keyboard/Keyboard.h"
#include "apple2/peripherals/keyboard/Keyboard_Maps.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"
#include "frontends/common/KeyboardTranslator.h"

namespace {

constexpr size_t MEMORY_SIZE_64K = 65536;

constexpr uint16_t ADDR_KBD = 0xC000;
constexpr uint16_t ADDR_KBDSTRB = 0xC010;
constexpr uint16_t ADDR_OPEN_APPLE = 0xC061;
constexpr uint16_t ADDR_CLOSED_APPLE = 0xC062;
constexpr uint16_t ADDR_SHIFT_KEY = 0xC063;

struct MockHandler {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockHandler() = default;
  MockHandler(void* inst, PeripheralIOHandler r, PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

class KeyboardTestHarness {
 public:
  explicit KeyboardTestHarness(int slot = 0) {
    s_active_harness = this;
    scoped_mem_.fill(0);
    prev_mem_ = mem;
    mem = scoped_mem_.data();

    host_.RegisterDirectIO = Mock_RegisterDirectIO;

    instance_ = keyboard_get_descriptor()->init(slot, &host_);
  }

  ~KeyboardTestHarness() {
    if (instance_ != nullptr) {
      keyboard_get_descriptor()->shutdown(instance_);
      instance_ = nullptr;
    }
    mem = prev_mem_;
    s_active_harness = nullptr;
  }

  KeyboardTestHarness(const KeyboardTestHarness&) = delete;
  auto operator=(const KeyboardTestHarness&) -> KeyboardTestHarness& = delete;
  KeyboardTestHarness(KeyboardTestHarness&&) = delete;
  auto operator=(KeyboardTestHarness&&) -> KeyboardTestHarness& = delete;

  auto instance() const -> void* { return instance_; }

  auto has_handler(uint16_t addr) const -> bool {
    return handlers_.find(addr) != handlers_.end();
  }

  auto get_handler(uint16_t addr) const -> const MockHandler& {
    return handlers_.at(addr);
  }

  auto read_io(uint16_t addr, uint8_t floating_bus = 0) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      void* target = (instance_ != nullptr) ? instance_ : it->second.instance;
      return it->second.read(target, 0, addr, 0, floating_bus, 0);
    }
    return floating_bus;
  }

  auto write_io(uint16_t addr, uint8_t val) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      void* target = (instance_ != nullptr) ? instance_ : it->second.instance;
      return it->second.write(target, 0, addr, 1, val, 0);
    }
    return 0;
  }

  auto read_c000() -> uint8_t { return read_io(ADDR_KBD); }

  auto read_c010() -> uint8_t { return read_io(ADDR_KBDSTRB); }

  auto send_event(const KeyboardEvent_t& ev) -> PeripheralStatus_t {
    return keyboard_get_descriptor()->command(instance_, keyboard_cmd_event,
                                              &ev, sizeof(ev));
  }

  auto set_caps(uint8_t caps) -> PeripheralStatus_t {
    return keyboard_get_descriptor()->command(instance_, keyboard_cmd_set_caps,
                                              &caps, sizeof(caps));
  }

  auto set_layout(uint8_t layout) -> PeripheralStatus_t {
    return keyboard_get_descriptor()->command(
        instance_, keyboard_cmd_set_layout, &layout, sizeof(layout));
  }

  auto set_rocker(uint8_t rocker) -> PeripheralStatus_t {
    return keyboard_get_descriptor()->command(
        instance_, keyboard_cmd_set_rocker, &rocker, sizeof(rocker));
  }

  auto set_mods(const KeyboardModifiers_t& mods) -> PeripheralStatus_t {
    return keyboard_get_descriptor()->command(instance_, keyboard_cmd_set_mods,
                                              &mods, sizeof(mods));
  }

  auto set_custom_key(const KeyboardCustomKeyPayload_t& payload)
      -> PeripheralStatus_t {
    return keyboard_get_descriptor()->command(
        instance_, keyboard_cmd_set_custom_key, &payload, sizeof(payload));
  }

  auto clear_custom_keys() -> PeripheralStatus_t {
    return keyboard_get_descriptor()->command(
        instance_, keyboard_cmd_clear_custom_keys, nullptr, 0);
  }

  auto think(uint32_t cycles) -> void {
    if (keyboard_get_descriptor()->think != nullptr && instance_ != nullptr) {
      keyboard_get_descriptor()->think(instance_, cycles);
    }
  }

 private:
  HostInterface_t host_{};
  std::map<uint16_t, MockHandler> handlers_;
  void* instance_ = nullptr;
  std::array<uint8_t, MEMORY_SIZE_64K> scoped_mem_{};
  uint8_t* prev_mem_ = nullptr;

  static KeyboardTestHarness* s_active_harness;

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  static auto Mock_RegisterDirectIO(void* instance, uint16_t addr,
                                    PeripheralIOHandler read,
                                    PeripheralIOHandler write) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->handlers_[addr] = {instance, read, write};
    }
  }
  // NOLINTEND(bugprone-easily-swappable-parameters)
};

KeyboardTestHarness* KeyboardTestHarness::s_active_harness = nullptr;

}  // namespace

TEST_CASE("Keyboard Peripheral: Lifecycle and I/O Registration") {
  KeyboardTestHarness harness;
  REQUIRE(harness.instance() != nullptr);

  // Verify $C000-$C01F are registered
  CHECK(harness.has_handler(0xC000));
  CHECK(harness.has_handler(0xC010));
}

TEST_CASE("Keyboard Peripheral: Strobe and Latch Behavior") {
  KeyboardTestHarness harness;

  // 1. Initially, strobe should be clear
  uint8_t val = harness.read_c000();
  CHECK((val & 0x80) == 0);

  // 2. Disable caps lock and simulate 'A' key down
  harness.set_caps(0);
  KeyboardEvent_t ev = {'a', 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev);

  // 3. Read $C000: expect 'a' (0x61) | Strobe (0x80) = 0xE1
  val = harness.read_c000();
  CHECK(val == 0xE1);

  // 4. Any access to $C010 should clear the strobe
  harness.read_c010();

  // 5. Read $C000 again: strobe should be clear, latch remains 'a'
  val = harness.read_c000();
  CHECK(val == 0x61);

  // 6. Check Any-Key-Down flag at $C010 (Bit 7)
  val = harness.read_c010();
  CHECK((val & 0x80) != 0);  // 'a' is still down

  // 7. Release 'a'
  ev.is_down = 0;
  harness.send_event(ev);

  // 8. Bit 7 of $C010 should now be clear
  val = harness.read_c010();
  CHECK((val & 0x80) == 0);
}

TEST_CASE("Keyboard Peripheral: Multiple keys and ASCII 0") {
  KeyboardTestHarness harness;

  // 1. Press 'A'
  harness.set_caps(0);
  KeyboardEvent_t ev_a = {'a', 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_a);
  CHECK((harness.read_c010() & 0x80) != 0);

  // 2. Press 'B'
  KeyboardEvent_t ev_b = {'b', 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_b);
  CHECK((harness.read_c010() & 0x80) != 0);

  // 3. Release 'A' - Bit 7 should STILL be set because 'B' is down
  ev_a.is_down = 0;
  harness.send_event(ev_a);
  CHECK((harness.read_c010() & 0x80) != 0);

  // 4. Release 'B' - Bit 7 should now be clear
  ev_b.is_down = 0;
  harness.send_event(ev_b);
  CHECK((harness.read_c010() & 0x80) == 0);

  // 5. Test ASCII 0 (Ctrl-@)
  KeyboardEvent_t ev_ctrl_at = {0, 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_ctrl_at);
  // Bit 7 should be set for ASCII 0 too
  CHECK((harness.read_c010() & 0x80) != 0);

  ev_ctrl_at.is_down = 0;
  harness.send_event(ev_ctrl_at);
  CHECK((harness.read_c010() & 0x80) == 0);
}

TEST_CASE("Keyboard Peripheral: Repeat key logic") {
  KeyboardTestHarness harness;

  // 1. Press 'A'
  harness.set_caps(0);
  KeyboardEvent_t ev_a = {'a', 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_a);

  // 2. Press 'B'
  KeyboardEvent_t ev_b = {'b', 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_b);

  // 3. Wait for repeat (KEY_REPEAT_INITIAL_DELAY = 512000 cycles)
  // First, clear the strobe so we can see it being set again
  harness.read_c010();

  harness.think(600000);
  // Strobe should be set now (repeating 'B')
  uint8_t val = harness.read_c000();
  CHECK((val & 0x80) != 0);
  CHECK((val & 0x7F) == 'b');

  // 4. Release 'A' (while 'B' is still held)
  ev_a.is_down = 0;
  harness.send_event(ev_a);

  // 5. Clear strobe and wait for another repeat
  harness.read_c010();
  harness.think(100000);  // Repeat rate is 68000

  // In BUGGY code, 'B' will NO LONGER REPEAT because release of 'A' cleared
  // repeat_key!
  val = harness.read_c000();
  CHECK((val & 0x80) != 0);  // Fails in buggy code
  CHECK((val & 0x7F) == 'b');
}

TEST_CASE("Keyboard Peripheral: International character safety") {
  KeyboardTestHarness harness;
  harness.set_caps(0);

  // 1. Press a valid code (0x7B = '{')
  KeyboardEvent_t ev = {0x7B, 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev);

  uint8_t val = harness.read_c000();
  CHECK((val & 0x7F) == 0x7B);
  CHECK((val & 0x80) != 0);

  // 2. Press a stray Latin-1 code (0xE9) - should be rejected/ignored
  harness.read_c010();  // clear strobe
  ev.key = 0xE9;
  harness.send_event(ev);

  val = harness.read_c000();
  CHECK((val & 0x80) == 0);  // Strobe NOT set because event was ignored

  // 3. Positional mapping test (e.g. LINAPPLE_KEY_POS_A = 0x504)
  harness.read_c010();  // clear strobe
  ev.key = 0x504;
  ev.mod_shift = 0;
  ev.mod_ctrl = 0;
  harness.send_event(ev);

  val = harness.read_c000();
  CHECK((val & 0x7F) == 'a');
  CHECK((val & 0x80) != 0);

  // 4. Positional mapping with Shift (A -> 0x504 + Shift)
  harness.read_c010();  // clear strobe
  ev.key = 0x504;
  ev.mod_shift = 1;
  harness.send_event(ev);

  val = harness.read_c000();
  CHECK((val & 0x7F) == 'A');
  CHECK((val & 0x80) != 0);

  // 5. Positional mapping with Ctrl (Ctrl-A -> 0x504 + Ctrl)
  harness.read_c010();  // clear strobe
  ev.key = 0x504;
  ev.mod_shift = 0;
  ev.mod_ctrl = 1;
  harness.send_event(ev);

  val = harness.read_c000();
  CHECK((val & 0x7F) == 0x01);
  CHECK((val & 0x80) != 0);

  // 6. Symbolic Arrow key tests
  // Up Arrow (LINAPPLE_KEY_UP = 0x100)
  harness.read_c010();
  ev.key = 0x100;
  ev.mod_shift = 0;
  ev.mod_ctrl = 0;
  harness.send_event(ev);
  val = harness.read_c000();
  CHECK((val & 0x7F) == 0x0B);

  // Down Arrow (LINAPPLE_KEY_DOWN = 0x101)
  harness.read_c010();
  ev.key = 0x101;
  harness.send_event(ev);
  val = harness.read_c000();
  CHECK((val & 0x7F) == 0x0A);

  // Left Arrow (LINAPPLE_KEY_LEFT = 0x102)
  harness.read_c010();
  ev.key = 0x102;
  harness.send_event(ev);
  val = harness.read_c000();
  CHECK((val & 0x7F) == 0x08);

  // Right Arrow (LINAPPLE_KEY_RIGHT = 0x103)
  harness.read_c010();
  ev.key = 0x103;
  harness.send_event(ev);
  val = harness.read_c000();
  CHECK((val & 0x7F) == 0x15);
}

TEST_CASE("Keyboard Peripheral: Repeat timer overflow and large batch safety") {
  const eApple2Type prev_apple2_type = g_apple2_type;
  // Ensure we are in a mode that supports auto-repeat
  g_apple2_type = A2TYPE_APPLE2EENHANCED;

  KeyboardTestHarness harness;

  // Press 'A'
  harness.set_caps(0);
  KeyboardEvent_t ev = {'a', 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev);

  // 1. Verify basic wrap-around safety (what was in issue 288)
  // Clear strobe so we can detect the repeat
  harness.read_c010();

  // Pass a huge cycle count that would cause wrap-around if added naively.
  // Result should trigger a repeat if correctly clamped or handled.
  harness.think(400000);
  const uint32_t huge_cycles = 0xFFFFFFFFU - 300000U;
  harness.think(huge_cycles);

  uint8_t val = harness.read_c000();
  CHECK((val & 0x80) != 0);  // Strobe should be set by repeat

  // 2. Verify large batch performance/safety (O(1) modulo)
  // Even if we passed a huge number without clamping, modulo would keep it
  // fast. Since we still clamp in Think, this is mostly checking the state is
  // valid.
  harness.read_c010();  // clear strobe
  harness.think(0xFFFFFFFFU);
  val = harness.read_c000();
  CHECK((val & 0x80) != 0);  // Should fire again

  g_apple2_type = prev_apple2_type;
}

TEST_CASE("Keyboard Peripheral: Ctrl+@ (NUL) handling") {
  KeyboardTestHarness harness;

  // Verify NUL (Ctrl+@) works through direct command
  KeyboardEvent_t ev = {0x00, 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev);

  // Bit 7 should be set (strobe), bits 0-6 should be 0
  uint8_t val = harness.read_c000();
  CHECK((val & 0x80) != 0);
  CHECK((val & 0x7F) == 0x00);
}

TEST_CASE("Keyboard Peripheral: Apple Keys and Modifiers Hardware Read") {
  KeyboardTestHarness harness;

  KeyboardModifiers_t mods = {0, 0, 0, 0, 0, {0, 0, 0}};

  // 1. Initially all should be clear
  harness.set_mods(mods);
  CHECK((harness.read_io(ADDR_OPEN_APPLE) & 0x80) == 0);
  CHECK((harness.read_io(ADDR_CLOSED_APPLE) & 0x80) == 0);
  CHECK((harness.read_io(ADDR_SHIFT_KEY) & 0x80) == 0);

  // 2. Set GUI -> Open Apple ($C061)
  mods.gui = 1;
  harness.set_mods(mods);
  CHECK((harness.read_io(ADDR_OPEN_APPLE) & 0x80) != 0);
  CHECK((harness.read_io(ADDR_CLOSED_APPLE) & 0x80) == 0);

  // 3. Set Alt -> Both Open and Closed Apple
  mods.gui = 0;
  mods.alt = 1;
  harness.set_mods(mods);
  CHECK((harness.read_io(ADDR_OPEN_APPLE) & 0x80) != 0);
  CHECK((harness.read_io(ADDR_CLOSED_APPLE) & 0x80) != 0);

  // 4. Set Shift -> $C063
  mods.alt = 0;
  mods.shift = 1;
  harness.set_mods(mods);
  CHECK((harness.read_io(ADDR_OPEN_APPLE) & 0x80) == 0);
  CHECK((harness.read_io(ADDR_CLOSED_APPLE) & 0x80) == 0);
  CHECK((harness.read_io(ADDR_SHIFT_KEY) & 0x80) != 0);
}

TEST_CASE("Keyboard Peripheral: Rocker Switch and Alternate Layout") {
  KeyboardTestHarness harness;

  // Clear Caps Lock for accurate testing
  harness.set_caps(0);

  // 1. Set alternate layout to French
  harness.set_layout(keyboard_layout_fr);

  // 2. Enable rocker switch
  harness.set_rocker(1);

  // 3. Press positional keyb_idx_2 (LINAPPLE_KEY_2 = 0x51F)
  KeyboardEvent_t ev = {0x51F, 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev);

  // Verify 'é' (0x7B)
  uint8_t val = harness.read_c000();
  CHECK((val & 0x7F) == 0x7B);

  // Release key and clear strobe
  ev.is_down = 0;
  harness.send_event(ev);
  harness.read_c010();

  // 4. Disable rocker switch
  harness.set_rocker(0);

  // 5. Press positional keyb_idx_2 again
  ev.is_down = 1;
  harness.send_event(ev);

  // Verify fallback to US '2' (0x32)
  val = harness.read_c000();
  CHECK((val & 0x7F) == 0x32);
}

TEST_CASE("Keyboard Peripheral: Caps Lock Behavior") {
  KeyboardTestHarness harness;

  // 1. Enable Caps Lock
  harness.set_caps(1);

  // Symbolic 'a'
  KeyboardEvent_t ev_sym = {'a', 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_sym);
  uint8_t val = harness.read_c000();
  CHECK((val & 0x7F) == 'A');

  ev_sym.is_down = 0;
  harness.send_event(ev_sym);
  harness.read_c010();

  // Positional 'a' (LINAPPLE_KEY_A = 0x504)
  KeyboardEvent_t ev_pos = {0x504, 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_pos);
  val = harness.read_c000();
  CHECK((val & 0x7F) == 'A');

  ev_pos.is_down = 0;
  harness.send_event(ev_pos);
  harness.read_c010();

  // 2. Disable Caps Lock
  harness.set_caps(0);

  // Positional 'a' again
  ev_pos.is_down = 1;
  harness.send_event(ev_pos);
  val = harness.read_c000();
  CHECK((val & 0x7F) == 'a');
}

TEST_CASE(
    "Keyboard Peripheral: Custom Key Mapping Overrides (e.g. WASD -> Arrows)") {
  KeyboardTestHarness harness;

  // W key is scancode 26 (0x1A), LINAPPLE_KEY_POS_W = 0x51A
  // Set custom override for W -> Up Arrow (0x0B)
  KeyboardCustomKeyPayload_t payload = {};
  payload.scancode = 26;      // keyb_idx_w
  payload.normal_val = 0x0B;  // Up Arrow
  payload.shift_val = 0x0B;
  payload.flags = 1;  // Custom active

  harness.set_custom_key(payload);

  // Press W in positional mode (0x500 + 26 = 0x51A)
  KeyboardEvent_t ev = {0x51A, 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev);

  uint8_t val = harness.read_c000();
  CHECK((val & 0x7F) == 0x0B);  // Verify Up Arrow was received

  ev.is_down = 0;
  harness.send_event(ev);
  harness.read_c010();

  // Clear custom keys and verify W reverts to 'w' (with caps lock disabled)
  harness.clear_custom_keys();
  harness.set_caps(0);

  ev.is_down = 1;
  harness.send_event(ev);
  val = harness.read_c000();
  CHECK((val & 0x7F) == 'w');
}

TEST_CASE("Keyboard Peripheral: Custom Key Open/Closed Apple Modifiers") {
  KeyboardTestHarness harness;

  // Tab key is scancode 43 (0x2B), LINAPPLE_KEY_POS_TAB = 0x52B
  KeyboardCustomKeyPayload_t payload = {};
  payload.scancode = 43;  // keyb_idx_tab
  payload.flags = 1 | 2;  // Active + OpenApple

  harness.set_custom_key(payload);

  // Verify $C061 initial (open apple button up)
  uint8_t oa_val = harness.read_io(ADDR_OPEN_APPLE);
  CHECK((oa_val & 0x80) == 0);

  // Press Tab
  KeyboardEvent_t ev = {0x52B, 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev);

  oa_val = harness.read_io(ADDR_OPEN_APPLE);
  CHECK((oa_val & 0x80) != 0);  // Open Apple is pressed!

  // Release Tab
  ev.is_down = 0;
  harness.send_event(ev);
  oa_val = harness.read_io(ADDR_OPEN_APPLE);
  CHECK((oa_val & 0x80) == 0);  // Open Apple released!
}

TEST_CASE("Keyboard Custom Mapping: Parsing Host Keys") {
  CHECK(keyboard_parse_host_key("w") == keyb_idx_w);
  CHECK(keyboard_parse_host_key("W") == keyb_idx_w);
  CHECK(keyboard_parse_host_key("Up") == keyb_idx_up);
  CHECK(keyboard_parse_host_key("Return") == keyb_idx_return);
  CHECK(keyboard_parse_host_key("Space") == keyb_idx_space);
  CHECK(keyboard_parse_host_key("Tab") == keyb_idx_tab);
  CHECK(keyboard_parse_host_key("F5") == keyb_idx_f5);
  CHECK(keyboard_parse_host_key("Minus") == keyb_idx_minus);
  CHECK(keyboard_parse_host_key("InvalidKeyXYZ") == keyb_idx_unknown);
}

TEST_CASE("Keyboard Custom Mapping: Parsing Apple II Target Values") {
  uint8_t flags = 0;
  CHECK(keyboard_parse_apple2_val("Up", &flags) == 0x0B);
  CHECK(flags == 0);

  CHECK(keyboard_parse_apple2_val("Down", &flags) == 0x0A);
  CHECK(keyboard_parse_apple2_val("Left", &flags) == 0x08);
  CHECK(keyboard_parse_apple2_val("Right", &flags) == 0x15);
  CHECK(keyboard_parse_apple2_val("0x0B", &flags) == 0x0B);
  CHECK(keyboard_parse_apple2_val("$15", &flags) == 0x15);
  CHECK(keyboard_parse_apple2_val("'a'", &flags) == 'a');
  CHECK(keyboard_parse_apple2_val("OpenApple", &flags) == 0);
  CHECK((flags & 2) != 0);

  CHECK(keyboard_parse_apple2_val("ClosedApple", &flags) == 0);
  CHECK((flags & 4) != 0);
}

TEST_CASE("Keyboard: QuickSave Key Combos and Hotkey Modes") {
  int slot = -1;
  bool is_save = false;

  // 1. Default Mode (QUICKSAVE_MODE_ALT)
  keyboard_set_quicksave_mode(QUICKSAVE_MODE_ALT);

  // Alt+2 (Load slot 2)
  CHECK(keyboard_is_quicksave_combo('2', 0x0100 /* ALT */, &slot, &is_save));
  CHECK(slot == 2);
  CHECK(is_save == false);

  // Alt+Shift+6 (Save slot 6)
  CHECK(keyboard_is_quicksave_combo('6', 0x0101 /* ALT | SHIFT */, &slot,
                                    &is_save));
  CHECK(slot == 6);
  CHECK(is_save == true);

  // Ctrl+Shift+2 (Lode Runner combo) - must NOT trigger quicksave!
  CHECK_FALSE(keyboard_is_quicksave_combo('2', 0x0041 /* CTRL | SHIFT */, &slot,
                                          &is_save));

  // Ctrl+Shift+6 (Lode Runner combo) - must NOT trigger quicksave!
  CHECK_FALSE(keyboard_is_quicksave_combo('6', 0x0041 /* CTRL | SHIFT */, &slot,
                                          &is_save));

  // 2. Legacy Mode (QUICKSAVE_MODE_CTRL)
  keyboard_set_quicksave_mode(QUICKSAVE_MODE_CTRL);
  CHECK(keyboard_is_quicksave_combo('2', 0x0040 /* CTRL */, &slot, &is_save));
  CHECK(slot == 2);
  CHECK(is_save == false);

  // 3. Disabled Mode
  keyboard_set_quicksave_mode(QUICKSAVE_MODE_DISABLED);
  CHECK_FALSE(
      keyboard_is_quicksave_combo('2', 0x0100 /* ALT */, &slot, &is_save));
  CHECK_FALSE(
      keyboard_is_quicksave_combo('2', 0x0040 /* CTRL */, &slot, &is_save));

  // 4. Hotkey Enable / Disable
  keyboard_set_hotkeys_enabled(true);
  CHECK(keyboard_get_hotkeys_enabled() == true);
  keyboard_set_hotkeys_enabled(false);
  CHECK(keyboard_get_hotkeys_enabled() == false);
}

TEST_CASE("Keyboard: Symbolic Shift and Punctuation Mapping") {
  KeyboardTestHarness harness;
  REQUIRE(harness.instance() != nullptr);

  // 1. Shift + '/' should produce '?' (0x3F | 0x80 = 0xBF)
  KeyboardEvent_t ev_slash = {'/', 1, 1, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_slash);
  uint8_t val = harness.read_c000();
  CHECK((val & 0x7F) == '?');

  // 2. Shift + '1' should produce '!' (0x21)
  KeyboardEvent_t ev_one = {'1', 1, 1, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_one);
  val = harness.read_c000();
  CHECK((val & 0x7F) == '!');

  // 3. Shift + ';' should produce ':' (0x3A)
  KeyboardEvent_t ev_semi = {';', 1, 1, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_semi);
  val = harness.read_c000();
  CHECK((val & 0x7F) == ':');

  // 4. Shift + '=' should produce '+' (0x2B)
  KeyboardEvent_t ev_equal = {'=', 1, 1, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_equal);
  val = harness.read_c000();
  CHECK((val & 0x7F) == '+');

  // 5. Shift + '-' should produce '_' (0x5F)
  KeyboardEvent_t ev_minus = {'-', 1, 1, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_minus);
  val = harness.read_c000();
  CHECK((val & 0x7F) == '_');

  // 6. Unshifted '/' should produce '/' (0x2F)
  KeyboardEvent_t ev_slash_unshifted = {'/', 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev_slash_unshifted);
  val = harness.read_c000();
  CHECK((val & 0x7F) == '/');
}

TEST_CASE("Keyboard: Caps Lock Bridge API (Get, Set, Toggle)") {
  // peripheral slot 0 is keyboard in LinAppleCore
  peripheral_register(keyboard_get_descriptor(), 0);

  // Default on reset is true (Caps Lock ON)
  CHECK(linapple_get_caps_lock_state() == true);

  // Set to false
  linapple_set_caps_lock_state(false);
  CHECK(linapple_get_caps_lock_state() == false);

  // Toggle
  bool toggled = linapple_toggle_caps_lock_state();
  CHECK(toggled == true);
  CHECK(linapple_get_caps_lock_state() == true);

  toggled = linapple_toggle_caps_lock_state();
  CHECK(toggled == false);
  CHECK(linapple_get_caps_lock_state() == false);

  peripheral_unregister(0);
}

TEST_CASE("Keyboard: Auto-repeat Paused in Full Speed Mode") {
  KeyboardTestHarness harness;
  REQUIRE(harness.instance() != nullptr);

  // Press Return key
  KeyboardEvent_t ev = {0x0D, 1, 0, 0, 0, 0, {0, 0, 0}};
  harness.send_event(ev);

  // Initial read sets and clears strobe
  uint8_t val = harness.read_c000();
  CHECK((val & 0x80) != 0);
  harness.read_c010();

  // When g_full_speed is true, large cycle steps (e.g. 1,000,000 cycles during
  // disk load) must NOT trigger auto-repeat
  const bool prev_full_speed = g_full_speed;
  g_full_speed = true;
  harness.think(1000000);
  val = harness.read_c000();
  CHECK((val & 0x80) == 0);  // Strobe must remain cleared

  // When g_full_speed is false, normal cycles trigger repeat after threshold
  g_full_speed = false;
  harness.think(600000);
  val = harness.read_c000();
  CHECK((val & 0x80) != 0);  // Strobe triggered by auto-repeat
  g_full_speed = prev_full_speed;
}
