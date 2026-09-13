// SPDX-License-Identifier: GPL-2.0-only

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "apple2/peripherals/keyboard/Keyboard.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

constexpr uint16_t ADDR_KBD = 0xC000;
constexpr uint16_t ADDR_KBDSTRB = 0xC010;
constexpr uint16_t ADDR_OPEN_APPLE = 0xC061;
constexpr uint16_t ADDR_CLOSED_APPLE = 0xC062;
constexpr uint16_t ADDR_SHIFT_KEY = 0xC063;

struct MockHandler_t {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockHandler_t() = default;
  MockHandler_t(void* inst, PeripheralIOHandler r, PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

class KeyboardTestHarness_t {
 public:
  explicit KeyboardTestHarness_t(int slot = 0, bool auto_init = true) {
    s_active_harness = this;
    host_.RegisterDirectIO = Mock_RegisterDirectIO;
    if (auto_init) {
      init(slot);
    }
  }

  ~KeyboardTestHarness_t() {
    shutdown();
    handlers_.clear();
    s_active_harness = nullptr;
  }

  KeyboardTestHarness_t(const KeyboardTestHarness_t&) = delete;
  auto operator=(const KeyboardTestHarness_t&)
      -> KeyboardTestHarness_t& = delete;
  KeyboardTestHarness_t(KeyboardTestHarness_t&&) = delete;
  auto operator=(KeyboardTestHarness_t&&) -> KeyboardTestHarness_t& = delete;

  auto host() -> HostInterface_t* { return &host_; }
  auto instance() const -> void* { return instance_; }

  auto init(int slot = 0) -> void* {
    if (instance_ != nullptr) {
      shutdown();
    }
    instance_ = keyboard_get_descriptor()->init(slot, &host_);
    return instance_;
  }

  auto shutdown() -> void {
    if (instance_ != nullptr) {
      keyboard_get_descriptor()->shutdown(instance_);
      instance_ = nullptr;
    }
  }

  auto reset() -> void {
    if (instance_ != nullptr) {
      keyboard_get_descriptor()->reset(instance_);
    }
  }

  auto has_handler(uint16_t addr) const -> bool {
    return handlers_.find(addr) != handlers_.end();
  }

  auto get_handler(uint16_t addr) const -> const MockHandler_t& {
    return handlers_.at(addr);
  }

  auto read_io(uint16_t addr, uint32_t cycles_left = 0) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      void* target = (instance_ != nullptr) ? instance_ : it->second.instance;
      return it->second.read(target, 0, addr, 0, 0, cycles_left);
    }
    return mem_read_floating_bus(cycles_left);
  }

  auto write_io(uint16_t addr, uint8_t val, uint32_t cycles_left = 0)
      -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      void* target = (instance_ != nullptr) ? instance_ : it->second.instance;
      return it->second.write(target, 0, addr, 1, val, cycles_left);
    }
    return 0;
  }

  auto read_c000() -> uint8_t { return read_io(ADDR_KBD); }
  auto read_c010() -> uint8_t { return read_io(ADDR_KBDSTRB); }
  auto write_c010(uint8_t val) -> uint8_t {
    return write_io(ADDR_KBDSTRB, val);
  }

  auto send_event(const KeyboardEvent_t& ev) -> PeripheralStatus_t {
    return command(keyboard_cmd_event, &ev, sizeof(ev));
  }

  auto set_caps(uint8_t caps) -> PeripheralStatus_t {
    return command(keyboard_cmd_set_caps, &caps, sizeof(caps));
  }

  auto set_layout(uint8_t layout) -> PeripheralStatus_t {
    return command(keyboard_cmd_set_layout, &layout, sizeof(layout));
  }

  auto set_rocker(uint8_t rocker) -> PeripheralStatus_t {
    return command(keyboard_cmd_set_rocker, &rocker, sizeof(rocker));
  }

  auto set_mods(const KeyboardModifiers_t& mods) -> PeripheralStatus_t {
    return command(keyboard_cmd_set_mods, &mods, sizeof(mods));
  }

  auto set_custom_key(const KeyboardCustomKeyPayload_t& payload)
      -> PeripheralStatus_t {
    return command(keyboard_cmd_set_custom_key, &payload, sizeof(payload));
  }

  auto clear_custom_keys() -> PeripheralStatus_t {
    return command(keyboard_cmd_clear_custom_keys, nullptr, 0);
  }

  auto set_auto_repeat(uint8_t enable) -> PeripheralStatus_t {
    return command(keyboard_cmd_set_auto_repeat, &enable, sizeof(enable));
  }

  auto think(uint32_t cycles) -> void {
    if (keyboard_get_descriptor()->think != nullptr && instance_ != nullptr) {
      keyboard_get_descriptor()->think(instance_, cycles);
    }
  }

  auto command(uint32_t cmd, const void* data, size_t size)
      -> PeripheralStatus_t {
    return keyboard_get_descriptor()->command(instance_, cmd, data, size);
  }

  auto query(uint32_t cmd, void* out, size_t* size) -> PeripheralStatus_t {
    return keyboard_get_descriptor()->query(instance_, cmd, out, size);
  }

  auto save_state(void* buffer, size_t* size) -> PeripheralStatus_t {
    return keyboard_get_descriptor()->save_state(instance_, buffer, size);
  }

  auto load_state(const void* buffer, size_t size) -> PeripheralStatus_t {
    return keyboard_get_descriptor()->load_state(instance_, buffer, size);
  }

 private:
  static auto Mock_RegisterDirectIO(void* instance, uint16_t addr,
                                    PeripheralIOHandler read,
                                    PeripheralIOHandler write) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->handlers_[addr] = MockHandler_t(instance, read, write);
    }
  }

  HostInterface_t host_{};
  std::map<uint16_t, MockHandler_t> handlers_{};
  void* instance_ = nullptr;

  static KeyboardTestHarness_t* s_active_harness;
};

KeyboardTestHarness_t* KeyboardTestHarness_t::s_active_harness = nullptr;

}  // namespace

TEST_CASE("KBD-01: Descriptor and Registration") {
  Peripheral_t* desc = keyboard_get_descriptor();
  REQUIRE_NE(desc, nullptr);
  CHECK_EQ(desc->abi_version, LINAPPLE_ABI_VERSION);
  CHECK_EQ(std::string(desc->id), "linapple.keyboard");
  CHECK_EQ(std::string(desc->name), "Keyboard");
  CHECK_EQ(desc->compatible_slots, PERIPHERAL_MASK_INTERNAL);
  CHECK_EQ(desc->default_slot, 0);

  CHECK_NE(desc->init, nullptr);
  CHECK_NE(desc->reset, nullptr);
  CHECK_NE(desc->shutdown, nullptr);
  CHECK_NE(desc->think, nullptr);
  CHECK_NE(desc->save_state, nullptr);
  CHECK_NE(desc->load_state, nullptr);
  CHECK_NE(desc->command, nullptr);
  CHECK_NE(desc->query, nullptr);
}

TEST_CASE("KBD-02: Lifecycle and Defensive Null Guards") {
  Peripheral_t* desc = keyboard_get_descriptor();
  REQUIRE_NE(desc, nullptr);

  // Null host returns nullptr
  void* inst = desc->init(0, nullptr);
  CHECK_EQ(inst, nullptr);

  // Safe null operations
  desc->reset(nullptr);
  desc->shutdown(nullptr);
  desc->think(nullptr, 1000);

  // Proper initialization via harness
  KeyboardTestHarness_t harness(0, true);
  CHECK_NE(harness.instance(), nullptr);
}

TEST_CASE("KBD-03: Direct I/O Registration") {
  KeyboardTestHarness_t harness;
  REQUIRE_NE(harness.instance(), nullptr);

  // $C000-$C00F registered read-only
  for (uint16_t addr = 0xC000; addr <= 0xC00F; ++addr) {
    CHECK_EQ(harness.has_handler(addr), true);
    CHECK_NE(harness.get_handler(addr).read, nullptr);
    CHECK_EQ(harness.get_handler(addr).write, nullptr);
  }

  // $C010 registered read/write
  CHECK_EQ(harness.has_handler(0xC010), true);
  CHECK_NE(harness.get_handler(0xC010).read, nullptr);
  CHECK_NE(harness.get_handler(0xC010).write, nullptr);

  // $C011-$C01F registered write-only
  for (uint16_t addr = 0xC011; addr <= 0xC01F; ++addr) {
    CHECK_EQ(harness.has_handler(addr), true);
    CHECK_EQ(harness.get_handler(addr).read, nullptr);
    CHECK_NE(harness.get_handler(addr).write, nullptr);
  }

  // $C061, $C062, $C063 registered read-only
  CHECK_EQ(harness.has_handler(ADDR_OPEN_APPLE), true);
  CHECK_NE(harness.get_handler(ADDR_OPEN_APPLE).read, nullptr);
  CHECK_EQ(harness.has_handler(ADDR_CLOSED_APPLE), true);
  CHECK_NE(harness.get_handler(ADDR_CLOSED_APPLE).read, nullptr);
  CHECK_EQ(harness.has_handler(ADDR_SHIFT_KEY), true);
  CHECK_NE(harness.get_handler(ADDR_SHIFT_KEY).read, nullptr);
}

TEST_CASE("KBD-04: Strobe Latch and Any-Key-Down at $C000 / $C010") {
  KeyboardTestHarness_t harness;

  // 1. Initial state: strobe bit 7 is clear
  uint8_t val = harness.read_c000();
  CHECK_EQ(val & 0x80, 0);

  // 2. Press 'A' key (disable caps lock first)
  harness.set_caps(0);
  KeyboardEvent_t ev = {'a', 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);

  // 3. Read $C000: 'a' (0x61) | Strobe (0x80) = 0xE1
  val = harness.read_c000();
  CHECK_EQ(val, 0xE1);

  // 4. Access $C010: clears strobe bit
  harness.read_c010();

  // 5. Read $C000 again: strobe is clear, latch remains 'a' (0x61)
  val = harness.read_c000();
  CHECK_EQ(val, 0x61);

  // 6. Check Any-Key-Down flag at $C010 (Bit 7) while key is still down
  val = harness.read_c010();
  CHECK_EQ(val & 0x80, 0x80);

  // 7. Release 'a'
  ev.is_down = 0;
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);

  // 8. Bit 7 of $C010 is now clear
  val = harness.read_c010();
  CHECK_EQ(val & 0x80, 0);
}

TEST_CASE("KBD-05: Write Access to $C010-$C01F Clears Strobe") {
  KeyboardTestHarness_t harness;

  // Set strobe with key press
  KeyboardEvent_t ev = {'X', 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  CHECK_EQ(harness.read_c000() & 0x80, 0x80);

  // Write to $C010: clears strobe
  harness.write_c010(0x00);
  CHECK_EQ(harness.read_c000() & 0x80, 0);

  // Set strobe again
  ev.is_down = 0;
  harness.send_event(ev);
  ev.is_down = 1;
  harness.send_event(ev);
  CHECK_EQ(harness.read_c000() & 0x80, 0x80);

  // Write to $C015: clears strobe
  harness.write_io(0xC015, 0x00);
  CHECK_EQ(harness.read_c000() & 0x80, 0);
}

TEST_CASE("KBD-06: Modifier and Pushbutton Sensing ($C061-$C063)") {
  KeyboardTestHarness_t harness;

  // Initially, pushbuttons report floating bus with bit 7 clear
  CHECK_EQ(harness.read_io(ADDR_OPEN_APPLE) & 0x80, 0);
  CHECK_EQ(harness.read_io(ADDR_CLOSED_APPLE) & 0x80, 0);
  CHECK_EQ(harness.read_io(ADDR_SHIFT_KEY) & 0x80, 0);

  // Send shift and GUI (Open Apple)
  KeyboardModifiers_t mods = {1, 0, 0, 1, 0, {0, 0, 0}};
  REQUIRE_EQ(harness.set_mods(mods), peripheral_ok);

  CHECK_EQ(harness.read_io(ADDR_OPEN_APPLE) & 0x80, 0x80);
  CHECK_EQ(harness.read_io(ADDR_CLOSED_APPLE) & 0x80, 0);
  CHECK_EQ(harness.read_io(ADDR_SHIFT_KEY) & 0x80, 0x80);

  // Send Alt: sets both Open Apple and Closed Apple
  mods = {0, 0, 1, 0, 0, {0, 0, 0}};
  REQUIRE_EQ(harness.set_mods(mods), peripheral_ok);

  CHECK_EQ(harness.read_io(ADDR_OPEN_APPLE) & 0x80, 0x80);
  CHECK_EQ(harness.read_io(ADDR_CLOSED_APPLE) & 0x80, 0x80);
  CHECK_EQ(harness.read_io(ADDR_SHIFT_KEY) & 0x80, 0);
}

TEST_CASE("KBD-07: Caps Lock Behavior") {
  KeyboardTestHarness_t harness;

  // 1. Caps lock ON (default): lowercase 'a' translates to uppercase 'A'
  KeyboardEvent_t ev = {'a', 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  CHECK_EQ(harness.read_c000(), 'A' | 0x80);

  // 2. Caps lock OFF: lowercase 'a' remains 'a'
  harness.write_c010(0);
  ev.is_down = 0;
  harness.send_event(ev);
  harness.set_caps(0);

  ev.is_down = 1;
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  CHECK_EQ(harness.read_c000(), 'a' | 0x80);
}

TEST_CASE("KBD-08: International Layout and Rocker Switch") {
  KeyboardTestHarness_t harness;

  // Select German layout (alternate_layout = 3)
  REQUIRE_EQ(harness.set_layout(keyboard_layout_de), peripheral_ok);

  // Rocker switch OFF (US mode): scancode 45 ('-') produces '-' (0x2D)
  KeyboardEvent_t ev = {0x500 + 45, 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  CHECK_EQ(harness.read_c000() & 0x7F, 0x2D);

  // Clear key
  ev.is_down = 0;
  harness.send_event(ev);
  harness.write_c010(0);

  // Rocker switch ON: scancode 45 in German layout produces 'ß' (0x7E)
  REQUIRE_EQ(harness.set_rocker(1), peripheral_ok);
  ev.is_down = 1;
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  CHECK_EQ(harness.read_c000() & 0x7F, 0x7E);
}

TEST_CASE("KBD-09: Custom Keymap Overrides") {
  KeyboardTestHarness_t harness;

  // Set custom key for scancode 4: normal='X', shift='Y', ctrl=0x18, flags=1
  KeyboardCustomKeyPayload_t payload = {4, 'X', 'Y', 0x18, 1};
  REQUIRE_EQ(harness.set_custom_key(payload), peripheral_ok);

  // Normal press
  KeyboardEvent_t ev = {0x500 + 4, 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  CHECK_EQ(harness.read_c000() & 0x7F, 'X');

  // Shift press
  ev.is_down = 0;
  harness.send_event(ev);
  ev.is_down = 1;
  ev.mod_shift = 1;
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  CHECK_EQ(harness.read_c000() & 0x7F, 'Y');

  // Clear custom keys: reverts to default 'A'
  harness.clear_custom_keys();
  ev.mod_shift = 0;
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  CHECK_EQ(harness.read_c000() & 0x7F, 'A');
}

TEST_CASE("KBD-10: Pushbutton Custom Keys (Open/Closed Apple)") {
  KeyboardTestHarness_t harness;

  // Map scancode 10 with flags=2 (Open Apple)
  KeyboardCustomKeyPayload_t p_oa = {10, 0, 0, 0, 2};
  REQUIRE_EQ(harness.set_custom_key(p_oa), peripheral_ok);

  // Key down sets Open Apple PB0
  KeyboardEvent_t ev_oa = {0x500 + 10, 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness.send_event(ev_oa), peripheral_ok);
  CHECK_EQ(harness.read_io(ADDR_OPEN_APPLE) & 0x80, 0x80);

  // Key up clears Open Apple
  ev_oa.is_down = 0;
  REQUIRE_EQ(harness.send_event(ev_oa), peripheral_ok);
  CHECK_EQ(harness.read_io(ADDR_OPEN_APPLE) & 0x80, 0);

  // Map scancode 11 with flags=4 (Closed Apple)
  KeyboardCustomKeyPayload_t p_ca = {11, 0, 0, 0, 4};
  REQUIRE_EQ(harness.set_custom_key(p_ca), peripheral_ok);

  KeyboardEvent_t ev_ca = {0x500 + 11, 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness.send_event(ev_ca), peripheral_ok);
  CHECK_EQ(harness.read_io(ADDR_CLOSED_APPLE) & 0x80, 0x80);

  ev_ca.is_down = 0;
  REQUIRE_EQ(harness.send_event(ev_ca), peripheral_ok);
  CHECK_EQ(harness.read_io(ADDR_CLOSED_APPLE) & 0x80, 0);
}

TEST_CASE("KBD-11: Pure Cycle-Driven Auto-Repeat") {
  KeyboardTestHarness_t harness;

  KeyboardEvent_t ev = {'A', 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  CHECK_EQ(harness.read_c000() & 0x80, 0x80);

  // Clear strobe
  harness.read_c010();
  CHECK_EQ(harness.read_c000() & 0x80, 0);

  // Step 250,000 cycles (< 512,000 initial delay): strobe remains clear
  harness.think(250000);
  CHECK_EQ(harness.read_c000() & 0x80, 0);

  // Step 300,000 cycles (total 550k >= 512k initial delay): repeat triggers!
  harness.think(300000);
  CHECK_EQ(harness.read_c000() & 0x80, 0x80);

  // Clear strobe again
  harness.read_c010();
  CHECK_EQ(harness.read_c000() & 0x80, 0);

  // Step 70,000 cycles (>= 68,000 repeat rate): repeat triggers again!
  harness.think(70000);
  CHECK_EQ(harness.read_c000() & 0x80, 0x80);

  // Key up cancels repeat
  ev.is_down = 0;
  harness.send_event(ev);
  harness.read_c010();
  harness.think(500000);
  CHECK_EQ(harness.read_c000() & 0x80, 0);
}

TEST_CASE("KBD-12: Auto-Repeat Toggle (Apple II/II+ Mode)") {
  KeyboardTestHarness_t harness;

  // Disable auto-repeat (Apple II/II+ behavior)
  REQUIRE_EQ(harness.set_auto_repeat(0), peripheral_ok);

  KeyboardEvent_t ev = {'A', 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  harness.read_c010();
  CHECK_EQ(harness.read_c000() & 0x80, 0);

  // Step 1,000,000 cycles: repeat NEVER triggers
  harness.think(1000000);
  CHECK_EQ(harness.read_c000() & 0x80, 0);

  // Re-enable auto-repeat
  REQUIRE_EQ(harness.set_auto_repeat(1), peripheral_ok);
  harness.think(600000);
  CHECK_EQ(harness.read_c000() & 0x80, 0x80);
}

TEST_CASE("KBD-13: Multiple Key Tracking and ASCII 0") {
  KeyboardTestHarness_t harness;

  // 1. Press 'A'
  harness.set_caps(0);
  KeyboardEvent_t ev_a = {'a', 1, 0, 0, 0, 0, {0, 0}};
  harness.send_event(ev_a);
  CHECK_EQ(harness.read_c010() & 0x80, 0x80);

  // 2. Press 'B'
  KeyboardEvent_t ev_b = {'b', 1, 0, 0, 0, 0, {0, 0}};
  harness.send_event(ev_b);
  CHECK_EQ(harness.read_c010() & 0x80, 0x80);

  // 3. Release 'A': 'B' is still down, so bit 7 remains set
  ev_a.is_down = 0;
  harness.send_event(ev_a);
  CHECK_EQ(harness.read_c010() & 0x80, 0x80);

  // 4. Release 'B': bit 7 clears
  ev_b.is_down = 0;
  harness.send_event(ev_b);
  CHECK_EQ(harness.read_c010() & 0x80, 0);

  // 5. Test ASCII 0 (Ctrl-@)
  KeyboardEvent_t ev_ctrl_at = {0, 1, 0, 0, 0, 0, {0, 0}};
  harness.send_event(ev_ctrl_at);
  CHECK_EQ(harness.read_c010() & 0x80, 0x80);

  ev_ctrl_at.is_down = 0;
  harness.send_event(ev_ctrl_at);
  CHECK_EQ(harness.read_c010() & 0x80, 0);
}

TEST_CASE("KBD-14: Command ABI Parameter Validation") {
  KeyboardTestHarness_t harness;

  KeyboardEvent_t ev = {'A', 1, 0, 0, 0, 0, {0, 0}};
  CHECK_EQ(harness.command(keyboard_cmd_event, nullptr, sizeof(ev)),
           peripheral_error);
  CHECK_EQ(harness.command(keyboard_cmd_event, &ev, 0), peripheral_error);

  uint8_t caps = 1;
  CHECK_EQ(harness.command(keyboard_cmd_set_caps, nullptr, sizeof(caps)),
           peripheral_error);
  CHECK_EQ(harness.command(keyboard_cmd_set_caps, &caps, 0), peripheral_error);

  KeyboardCustomKeyPayload_t bad_scancode = {128, 'A', 'B', 0, 1};
  CHECK_EQ(harness.command(keyboard_cmd_set_custom_key, &bad_scancode,
                           sizeof(bad_scancode)),
           peripheral_error);

  CHECK_EQ(harness.command(0x9999, &caps, sizeof(caps)),
           peripheral_incompatible);
}

TEST_CASE("KBD-15: Query ABI Protocol and Sizing Probes") {
  KeyboardTestHarness_t harness;

  // Pass 1 sizing probe for keyboard_query_mods
  size_t mods_size = 0;
  REQUIRE_EQ(harness.query(keyboard_query_mods, nullptr, &mods_size),
             peripheral_ok);
  CHECK_EQ(mods_size, sizeof(KeyboardModifiers_t));

  // Undersized buffer for mods
  KeyboardModifiers_t mods{};
  mods_size = sizeof(mods) - 1;
  CHECK_EQ(harness.query(keyboard_query_mods, &mods, &mods_size),
           peripheral_error);

  // Pass 1 sizing probe for keyboard_query_rocker
  size_t rocker_size = 0;
  REQUIRE_EQ(harness.query(keyboard_query_rocker, nullptr, &rocker_size),
             peripheral_ok);
  CHECK_EQ(rocker_size, sizeof(uint8_t));

  // Undersized buffer for rocker
  uint8_t rocker = 0;
  rocker_size = 0;
  CHECK_EQ(harness.query(keyboard_query_rocker, &rocker, &rocker_size),
           peripheral_error);

  // Unknown query ID
  size_t unknown_size = sizeof(rocker);
  CHECK_EQ(harness.query(0x9999, &rocker, &unknown_size),
           peripheral_incompatible);
}

TEST_CASE("KBD-16: Deterministic 552-Byte Save State Round-Trip") {
  KeyboardTestHarness_t harness1;

  // Pass 1: Sizing probe
  size_t state_size = 0;
  REQUIRE_EQ(harness1.save_state(nullptr, &state_size), peripheral_ok);
  CHECK_EQ(state_size, 552U);

  // Configure harness 1
  harness1.set_caps(0);
  harness1.set_rocker(1);
  harness1.set_layout(keyboard_layout_fr);
  harness1.set_auto_repeat(0);

  // Configure custom keymappings across multiple scancodes
  KeyboardCustomKeyPayload_t k1 = {4, 'X', 'Y', 0x18, 1};
  KeyboardCustomKeyPayload_t k2 = {50, 0x30, 0x31, 0x32, 2};
  REQUIRE_EQ(harness1.set_custom_key(k1), peripheral_ok);
  REQUIRE_EQ(harness1.set_custom_key(k2), peripheral_ok);

  // Press key
  KeyboardEvent_t ev = {'Z', 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness1.send_event(ev), peripheral_ok);

  // Pass 2: Save state
  std::vector<uint8_t> buffer(state_size, 0);
  REQUIRE_EQ(harness1.save_state(buffer.data(), &state_size), peripheral_ok);
  CHECK_EQ(state_size, 552U);

  // Verify byte header
  const auto* ss = reinterpret_cast<const KeyboardSaveState_t*>(buffer.data());
  CHECK_EQ(ss->version, static_cast<uint32_t>(KEYBOARD_STATE_VERSION));
  CHECK_EQ(ss->struct_size, 552U);
  CHECK_EQ(ss->caps_lock, 0U);
  CHECK_EQ(ss->rocker_switch, 1U);
  CHECK_EQ(ss->alternate_layout, static_cast<uint8_t>(keyboard_layout_fr));
  CHECK_EQ(ss->auto_repeat_enabled, 0U);
  CHECK_EQ(ss->has_custom_keys, 1U);
  CHECK_EQ(ss->custom_map[4], 'X');
  CHECK_EQ(ss->custom_flags[50], 2U);

  // Restore into fresh harness 2
  KeyboardTestHarness_t harness2;
  REQUIRE_EQ(harness2.load_state(buffer.data(), buffer.size()), peripheral_ok);

  // Verify restored latch and strobe
  CHECK_EQ(harness2.read_c000(), 'Z' | 0x80);

  // Verify custom key is preserved in harness 2
  KeyboardEvent_t ev_custom = {0x500 + 4, 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness2.send_event(ev_custom), peripheral_ok);
  CHECK_EQ(harness2.read_c000() & 0x7F, 'X');

  // Verify auto-repeat disabled state is preserved
  harness2.read_c010();
  harness2.think(1000000);
  CHECK_EQ(harness2.read_c000() & 0x80, 0);
}

TEST_CASE("KBD-17: Corrupt State Rejection") {
  KeyboardTestHarness_t harness;

  KeyboardSaveState_t valid_state{};
  valid_state.version = KEYBOARD_STATE_VERSION;
  valid_state.struct_size = sizeof(KeyboardSaveState_t);

  // Null buffer
  CHECK_EQ(harness.load_state(nullptr, sizeof(valid_state)), peripheral_error);

  // Undersized buffer
  CHECK_EQ(harness.load_state(&valid_state, sizeof(valid_state) - 1),
           peripheral_error);

  // Oversized buffer
  CHECK_EQ(harness.load_state(&valid_state, sizeof(valid_state) + 1),
           peripheral_error);

  // Bad version
  KeyboardSaveState_t bad_version = valid_state;
  bad_version.version = 99;
  CHECK_EQ(harness.load_state(&bad_version, sizeof(bad_version)),
           peripheral_error);

  // Bad struct size
  KeyboardSaveState_t bad_size = valid_state;
  bad_size.struct_size = 500;
  CHECK_EQ(harness.load_state(&bad_size, sizeof(bad_size)), peripheral_error);
}

TEST_CASE("KBD-18: Auto-Repeat Suppression in Warp Speed (g_full_speed)") {
  KeyboardTestHarness_t harness;

  KeyboardEvent_t ev = {'B', 1, 0, 0, 0, 0, {0, 0}};
  REQUIRE_EQ(harness.send_event(ev), peripheral_ok);
  CHECK_EQ(harness.read_c000() & 0x80, 0x80);

  // Clear strobe
  harness.read_c010();
  CHECK_EQ(harness.read_c000() & 0x80, 0);

  // When g_full_speed is true, even 1,000,000 cycles must NOT trigger
  // auto-repeat
  extern bool g_full_speed;
  const bool prev_full_speed = g_full_speed;
  g_full_speed = true;
  harness.think(1000000);
  CHECK_EQ(harness.read_c000() & 0x80, 0);

  // Restore normal speed: auto-repeat can now advance and fire
  g_full_speed = false;
  harness.think(600000);
  CHECK_EQ(harness.read_c000() & 0x80, 0x80);
  g_full_speed = prev_full_speed;
}
