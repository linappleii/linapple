// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstring>
#include <vector>

#include "apple2/peripherals/mouse/Mouse.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

namespace {

// --- Slot and I/O Constants ---
constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;

constexpr uint8_t PIA_DDR_ACCESS = 0x00;
constexpr uint8_t PIA_DATA_ACCESS = 0x04;
constexpr uint8_t PIA_ALL_OUTPUTS = 0xFF;

constexpr uint8_t STROBE_SEND_HIGH = 0x20;  // PB5 High
constexpr uint8_t STROBE_SEND_LOW = 0x00;   // PB5 Low
constexpr uint8_t STROBE_RECV_HIGH = 0x10;  // PB4 High
constexpr uint8_t STROBE_RECV_LOW = 0x00;   // PB4 Low

constexpr int DEFAULT_SLOT = 4;
constexpr int ALTERNATE_SLOT = 5;

// --- Protocol Constants (Matching Mouse.cpp) ---
namespace regs {
constexpr uint8_t MOUSE_SET = 0x00;
constexpr uint8_t MOUSE_READ = 0x10;
constexpr uint8_t MOUSE_SERV = 0x20;
constexpr uint8_t MOUSE_CLEAR = 0x30;
constexpr uint8_t MOUSE_POS = 0x40;
constexpr uint8_t MOUSE_INIT = 0x50;
constexpr uint8_t MOUSE_CLAMP = 0x60;
constexpr uint8_t MOUSE_HOME = 0x70;

constexpr uint8_t MODE_ON = 0x01;
constexpr uint8_t MODE_MOVE_INT = 0x02;
constexpr uint8_t MODE_BTN_INT = 0x04;
constexpr uint8_t MODE_VBL_INT = 0x08;

constexpr uint8_t STAT_PREV_BTN1 = 0x01;
constexpr uint8_t STAT_MOVE_INT = 0x02;
constexpr uint8_t STAT_BTN_INT = 0x04;
constexpr uint8_t STAT_VBL_INT = 0x08;
constexpr uint8_t STAT_CURR_BTN1 = 0x10;
constexpr uint8_t STAT_MOVEMENT = 0x20;
constexpr uint8_t STAT_PREV_BTN0 = 0x40;
constexpr uint8_t STAT_CURR_BTN0 = 0x80;
}  // namespace regs

struct MouseReadData_t {
  uint8_t x_low = 0;
  uint8_t x_high = 0;
  uint8_t y_low = 0;
  uint8_t y_high = 0;
  uint8_t status = 0;

  auto x() const -> uint16_t {
    return static_cast<uint16_t>(x_low | (static_cast<uint16_t>(x_high) << 8));
  }
  auto y() const -> uint16_t {
    return static_cast<uint16_t>(y_low | (static_cast<uint16_t>(y_high) << 8));
  }
};

class MouseHarness {
 public:
  explicit MouseHarness(int slot = DEFAULT_SLOT) {
    s_active_harness = this;
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    init(slot);
  }

  ~MouseHarness() {
    shutdown();
    s_active_harness = nullptr;
  }

  MouseHarness(const MouseHarness&) = delete;
  auto operator=(const MouseHarness&) -> MouseHarness& = delete;
  MouseHarness(MouseHarness&&) = delete;
  auto operator=(MouseHarness&&) -> MouseHarness& = delete;

  auto host() -> HostInterface_t* { return &host_; }
  auto descriptor() const -> Peripheral_t* { return mouse_get_descriptor(); }
  auto instance() const -> void* { return instance_; }
  auto slot() const -> int { return slot_; }

  auto shutdown() -> void {
    if (instance_ != nullptr) {
      descriptor()->shutdown(instance_);
      instance_ = nullptr;
    }
    io_handler_ = nullptr;
    irq_asserted_ = false;
    last_irq_slot_ = -1;
  }

  auto init(int slot) -> void* {
    shutdown();
    slot_ = slot;
    instance_ = descriptor()->init(slot_, &host_);
    return instance_;
  }

  auto base_addr() const -> uint16_t {
    return static_cast<uint16_t>(IO_BASE_ADDRESS + (slot_ << IO_SLOT_OFFSET));
  }
  auto port_a_addr() const -> uint16_t { return base_addr(); }
  auto control_a_addr() const -> uint16_t {
    return static_cast<uint16_t>(base_addr() + 1);
  }
  auto port_b_addr() const -> uint16_t {
    return static_cast<uint16_t>(base_addr() + 2);
  }
  auto control_b_addr() const -> uint16_t {
    return static_cast<uint16_t>(base_addr() + 3);
  }

  auto read_io(uint16_t addr) -> uint8_t {
    if (io_handler_ == nullptr || instance_ == nullptr) {
      return 0;
    }
    return io_handler_(instance_, 0, addr, 0, 0, 0);
  }

  auto write_io(uint16_t addr, uint8_t val) -> void {
    if (io_handler_ == nullptr || instance_ == nullptr) {
      return;
    }
    io_handler_(instance_, 0, addr, 1, val, 0);
  }

  auto read_port_a() -> uint8_t { return read_io(port_a_addr()); }
  auto write_port_a(uint8_t val) -> void { write_io(port_a_addr(), val); }

  auto read_control_a() -> uint8_t { return read_io(control_a_addr()); }
  auto write_control_a(uint8_t val) -> void { write_io(control_a_addr(), val); }

  auto read_port_b() -> uint8_t { return read_io(port_b_addr()); }
  auto write_port_b(uint8_t val) -> void { write_io(port_b_addr(), val); }

  auto read_control_b() -> uint8_t { return read_io(control_b_addr()); }
  auto write_control_b(uint8_t val) -> void { write_io(control_b_addr(), val); }

  auto init_mouse_card() -> void {
    write_control_a(PIA_DDR_ACCESS);
    write_control_b(PIA_DDR_ACCESS);
    write_port_a(PIA_ALL_OUTPUTS);
    write_port_b(PIA_ALL_OUTPUTS);
    write_control_a(PIA_DATA_ACCESS);
    write_control_b(PIA_DATA_ACCESS);
  }

  auto send_byte(uint8_t val) -> void {
    write_port_a(val);
    write_port_b(STROBE_SEND_HIGH);
    write_port_b(STROBE_SEND_LOW);
  }

  auto recv_byte() -> uint8_t {
    write_port_b(STROBE_RECV_HIGH);
    write_port_b(STROBE_RECV_LOW);
    return read_port_a();
  }

  auto drain_read() -> MouseReadData_t {
    MouseReadData_t data{};
    data.x_low = read_port_a();
    data.x_high = recv_byte();
    data.y_low = recv_byte();
    data.y_high = recv_byte();
    data.status = recv_byte();
    (void)recv_byte();  // 6th byte resets response buffer pointer
    return data;
  }

  auto irq_asserted() const -> bool { return irq_asserted_; }
  auto last_irq_slot() const -> int { return last_irq_slot_; }
  auto clear_irq_flag() -> void { irq_asserted_ = false; }

  auto set_pos(int x, int x_range, int y, int y_range) -> PeripheralStatus_t {
    MousePosPayload_t payload{x, x_range, y, y_range};
    return descriptor()->command(instance_, mouse_cmd_set_pos, &payload,
                                 sizeof(payload));
  }

  auto set_button(uint8_t button, bool down) -> PeripheralStatus_t {
    MouseButtonPayload_t payload{button, down};
    return descriptor()->command(instance_, mouse_cmd_set_button, &payload,
                                 sizeof(payload));
  }

  auto is_active() -> bool {
    uint8_t active = 0;
    size_t out_size = sizeof(active);
    PeripheralStatus_t status = descriptor()->query(
        instance_, mouse_query_is_active, &active, &out_size);
    return (status == peripheral_ok && active == 1);
  }

  auto on_vblank(bool vblank) -> void {
    if (descriptor()->on_vblank != nullptr && instance_ != nullptr) {
      descriptor()->on_vblank(instance_, vblank);
    }
  }

 private:
  HostInterface_t host_{};
  void* instance_ = nullptr;
  PeripheralIOHandler io_handler_ = nullptr;
  int slot_ = DEFAULT_SLOT;
  bool irq_asserted_ = false;
  int last_irq_slot_ = -1;

  static MouseHarness* s_active_harness;

  static auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
    if (s_active_harness != nullptr && slot == s_active_harness->slot_) {
      s_active_harness->irq_asserted_ = assert_irq;
      s_active_harness->last_irq_slot_ = slot;
    }
  }

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  static auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                              PeripheralIOHandler write_c0,
                              PeripheralIOHandler read_cx,
                              PeripheralIOHandler write_cx) -> void {
    (void)slot;
    (void)write_c0;
    (void)read_cx;
    (void)write_cx;
    if (s_active_harness != nullptr) {
      s_active_harness->io_handler_ = read_c0;
    }
  }
  // NOLINTEND(bugprone-easily-swappable-parameters)
};

MouseHarness* MouseHarness::s_active_harness = nullptr;

}  // namespace

TEST_CASE("Mouse Exhaustive Functional Tests") {
  SUBCASE("ABI Verification") {
    MouseHarness harness(DEFAULT_SLOT);
    REQUIRE(harness.instance() != nullptr);
    CHECK(std::strcmp(harness.descriptor()->id, "linapple.mouse") == 0);
    CHECK(std::strcmp(harness.descriptor()->name, "Mouse Interface") == 0);
    CHECK(harness.is_active());

    // Slot compatibility check (expansion slots mask)
    CHECK((harness.descriptor()->compatible_slots & (1 << DEFAULT_SLOT)) != 0);
  }

  SUBCASE("I/O Register Access and Slot Address Derivation") {
    // Test slot 4 dynamic addressing
    MouseHarness harness(DEFAULT_SLOT);
    REQUIRE(harness.instance() != nullptr);
    CHECK(harness.base_addr() == 0xC0C0);
    CHECK(harness.port_a_addr() == 0xC0C0);
    CHECK(harness.control_a_addr() == 0xC0C1);
    CHECK(harness.port_b_addr() == 0xC0C2);
    CHECK(harness.control_b_addr() == 0xC0C3);

    harness.init_mouse_card();

    harness.write_io(harness.port_b_addr(), 0x55);
    CHECK(harness.read_io(harness.port_b_addr()) == 0x55);

    harness.write_io(harness.port_b_addr(), 0xAA);
    CHECK(harness.read_io(harness.port_b_addr()) == 0xAA);

    // Test alternate slot (slot 5) dynamic addressing
    harness.init(ALTERNATE_SLOT);
    REQUIRE(harness.instance() != nullptr);
    CHECK(harness.base_addr() == 0xC0D0);
    CHECK(harness.port_a_addr() == 0xC0D0);
    CHECK(harness.control_a_addr() == 0xC0D1);
    CHECK(harness.port_b_addr() == 0xC0D2);
    CHECK(harness.control_b_addr() == 0xC0D3);

    harness.init_mouse_card();
    harness.write_io(harness.port_b_addr(), 0x33);
    CHECK(harness.read_io(harness.port_b_addr()) == 0x33);
  }

  SUBCASE("Firmware Commands - MOUSE_INIT") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();

    harness.send_byte(regs::MOUSE_INIT);
    uint8_t byte1 = harness.read_port_a();
    CHECK(byte1 == 0xFF);

    (void)harness.recv_byte();  // drain byte 2
    (void)harness.recv_byte();  // drain byte 3
  }

  SUBCASE("Firmware Commands - MOUSE_SET and IRQ Logic") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();

    // Default coordinate range
    harness.set_pos(0, 1023, 0, 1023);

    // Mode 1: Mouse On, interrupts disabled
    harness.send_byte(regs::MOUSE_SET | regs::MODE_ON);
    harness.clear_irq_flag();

    harness.set_pos(100, 1023, 200, 1023);
    CHECK_FALSE(harness.irq_asserted());

    // Mode 0x0F: Mouse On, all interrupts enabled
    harness.send_byte(regs::MOUSE_SET | regs::MODE_ON | regs::MODE_MOVE_INT |
                      regs::MODE_BTN_INT | regs::MODE_VBL_INT);

    harness.set_pos(110, 1023, 200, 1023);
    CHECK(harness.irq_asserted());
    CHECK(harness.last_irq_slot() == DEFAULT_SLOT);

    // MOUSE_SERV clears the asserted interrupt
    harness.send_byte(regs::MOUSE_SERV);
    (void)harness.read_port_a();  // Status byte
    (void)harness.recv_byte();    // Dummy byte
    CHECK_FALSE(harness.irq_asserted());
  }

  SUBCASE("Firmware Commands - MOUSE_READ") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();

    harness.set_pos(123, 1023, 456, 1023);
    harness.send_byte(regs::MOUSE_READ);

    const auto data1 = harness.drain_read();
    CHECK(data1.x_low == 123);
    CHECK(data1.x_high == 0);
    CHECK(data1.y_low == (456 & 0xFF));
    CHECK(data1.y_high == (456 >> 8));
    CHECK(data1.x() == 123);
    CHECK(data1.y() == 456);
    CHECK((data1.status & regs::STAT_MOVEMENT) != 0);

    // Consecutive read without movement: STAT_MOVEMENT flag is cleared
    harness.send_byte(regs::MOUSE_READ);
    const auto data2 = harness.drain_read();
    CHECK(data2.x() == 123);
    CHECK(data2.y() == 456);
    CHECK((data2.status & regs::STAT_MOVEMENT) == 0);
  }

  SUBCASE("Firmware Commands - MOUSE_POS") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();
    harness.set_pos(0, 1023, 0, 1023);

    // Set position via protocol: X = 0x01AA, Y = 0x02BB
    harness.send_byte(regs::MOUSE_POS);
    harness.send_byte(0xAA);
    harness.send_byte(0x01);
    harness.send_byte(0xBB);
    harness.send_byte(0x02);

    harness.send_byte(regs::MOUSE_READ);
    const auto data = harness.drain_read();
    CHECK(data.x_low == 0xAA);
    CHECK(data.x_high == 0x01);
    CHECK(data.y_low == 0xBB);
    CHECK(data.y_high == 0x02);
    CHECK(data.x() == 0x01AA);
    CHECK(data.y() == 0x02BB);
  }

  SUBCASE("Firmware Commands - MOUSE_CLAMP") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();
    harness.set_pos(0, 1023, 0, 1023);

    // Clamp X to [100, 200]
    harness.send_byte(regs::MOUSE_CLAMP | 0);
    harness.send_byte(100 & 0xFF);
    harness.send_byte(0);
    harness.send_byte(200 & 0xFF);
    harness.send_byte(0);

    // Clamp Y to [300, 400]
    harness.send_byte(regs::MOUSE_CLAMP | 1);
    harness.send_byte(300 & 0xFF);
    harness.send_byte(300 >> 8);
    harness.send_byte(400 & 0xFF);
    harness.send_byte(400 >> 8);

    // Move below minimums: X = 50, Y = 250 -> Clamped to (100, 300)
    harness.set_pos(50, 1023, 250, 1023);
    harness.send_byte(regs::MOUSE_READ);
    const auto data_low = harness.drain_read();
    CHECK(data_low.x() == 100);
    CHECK(data_low.y() == 300);

    // Move above maximums: X = 250, Y = 450 -> Clamped to (200, 400)
    harness.set_pos(250, 1023, 450, 1023);
    harness.send_byte(regs::MOUSE_READ);
    const auto data_high = harness.drain_read();
    CHECK(data_high.x() == 200);
    CHECK(data_high.y() == 400);
  }

  SUBCASE("Firmware Commands - MOUSE_HOME") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();
    harness.set_pos(0, 1023, 0, 1023);

    harness.set_pos(500, 1023, 500, 1023);
    harness.send_byte(regs::MOUSE_HOME);

    harness.send_byte(regs::MOUSE_READ);
    const auto data = harness.drain_read();
    CHECK(data.x() == 0);
    CHECK(data.y() == 0);
  }

  SUBCASE("Firmware Commands - MOUSE_CLEAR") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();

    // Set position and enable mouse
    harness.set_pos(300, 1023, 400, 1023);
    harness.send_byte(regs::MOUSE_SET | regs::MODE_ON);

    // MOUSE_CLEAR resets internal state and coordinates
    harness.send_byte(regs::MOUSE_CLEAR);

    // Coordinates reset to (0, 0)
    harness.send_byte(regs::MOUSE_READ);
    const auto data = harness.drain_read();
    CHECK(data.x() == 0);
    CHECK(data.y() == 0);

    // Mode is reset to 0, so movement does not trigger IRQ
    harness.set_pos(100, 1023, 100, 1023);
    CHECK_FALSE(harness.irq_asserted());
  }

  SUBCASE("Button Handling and Interrupts") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();
    harness.send_byte(regs::MOUSE_SET | regs::MODE_ON);

    // Initial read: both buttons up
    harness.send_byte(regs::MOUSE_READ);
    auto d0 = harness.drain_read();
    CHECK((d0.status & regs::STAT_CURR_BTN0) == 0);
    CHECK((d0.status & regs::STAT_PREV_BTN0) == 0);

    // Press button 0
    CHECK(harness.set_button(0, true) == peripheral_ok);
    harness.send_byte(regs::MOUSE_READ);
    auto d1 = harness.drain_read();
    CHECK((d1.status & regs::STAT_CURR_BTN0) != 0);
    CHECK((d1.status & regs::STAT_PREV_BTN0) == 0);

    // Hold button 0 on next read: both current and previous down
    harness.send_byte(regs::MOUSE_READ);
    auto d2 = harness.drain_read();
    CHECK((d2.status & regs::STAT_CURR_BTN0) != 0);
    CHECK((d2.status & regs::STAT_PREV_BTN0) != 0);

    // Release button 0: current up, previous down
    CHECK(harness.set_button(0, false) == peripheral_ok);
    harness.send_byte(regs::MOUSE_READ);
    auto d3 = harness.drain_read();
    CHECK((d3.status & regs::STAT_CURR_BTN0) == 0);
    CHECK((d3.status & regs::STAT_PREV_BTN0) != 0);

    // Read again: both current and previous up
    harness.send_byte(regs::MOUSE_READ);
    auto d4 = harness.drain_read();
    CHECK((d4.status & regs::STAT_CURR_BTN0) == 0);
    CHECK((d4.status & regs::STAT_PREV_BTN0) == 0);

    // Button 1 transition verification
    CHECK(harness.set_button(1, true) == peripheral_ok);
    harness.send_byte(regs::MOUSE_READ);
    auto d5 = harness.drain_read();
    CHECK((d5.status & regs::STAT_CURR_BTN1) != 0);
    CHECK((d5.status & regs::STAT_PREV_BTN1) == 0);

    CHECK(harness.set_button(1, false) == peripheral_ok);
    harness.send_byte(regs::MOUSE_READ);
    auto d6 = harness.drain_read();
    CHECK((d6.status & regs::STAT_CURR_BTN1) == 0);
    CHECK((d6.status & regs::STAT_PREV_BTN1) != 0);

    // Button Interrupt Generation
    harness.send_byte(regs::MOUSE_SET | regs::MODE_ON | regs::MODE_BTN_INT);
    harness.clear_irq_flag();
    CHECK(harness.set_button(0, true) == peripheral_ok);
    CHECK(harness.irq_asserted());

    // Clear interrupt via MOUSE_SERV
    harness.send_byte(regs::MOUSE_SERV);
    (void)harness.read_port_a();
    (void)harness.recv_byte();
    CHECK_FALSE(harness.irq_asserted());
  }

  SUBCASE("VBlank Interrupt Logic") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();

    // Enable VBL interrupt mode
    harness.send_byte(regs::MOUSE_SET | regs::MODE_ON | regs::MODE_VBL_INT);
    harness.clear_irq_flag();

    // Falling edge does not trigger interrupt
    harness.on_vblank(false);
    CHECK_FALSE(harness.irq_asserted());

    // Rising edge triggers interrupt
    harness.on_vblank(true);
    CHECK(harness.irq_asserted());

    // Servicing interrupt clears it
    harness.send_byte(regs::MOUSE_SERV);
    (void)harness.read_port_a();
    (void)harness.recv_byte();
    CHECK_FALSE(harness.irq_asserted());
  }

  SUBCASE("State Persistence") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();

    harness.set_pos(789, 1023, 321, 1023);
    harness.set_button(0, true);
    harness.send_byte(regs::MOUSE_SET | 0x0F);

    // Query state buffer size
    size_t state_size = 0;
    PeripheralStatus_t query_stat = harness.descriptor()->save_state(
        harness.instance(), nullptr, &state_size);
    CHECK(query_stat == peripheral_ok);
    CHECK(state_size > 0);

    // Negative test: undersized buffer rejection
    size_t undersized = 1;
    uint8_t dummy = 0;
    PeripheralStatus_t under_stat = harness.descriptor()->save_state(
        harness.instance(), &dummy, &undersized);
    CHECK(under_stat == peripheral_error);
    CHECK(undersized == state_size);

    std::vector<uint8_t> buffer(state_size);
    PeripheralStatus_t save_stat = harness.descriptor()->save_state(
        harness.instance(), buffer.data(), &state_size);
    CHECK(save_stat == peripheral_ok);

    // Re-initialize harness and card
    harness.init(DEFAULT_SLOT);
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();

    // Negative test: load undersized buffer
    PeripheralStatus_t load_err = harness.descriptor()->load_state(
        harness.instance(), buffer.data(), sizeof(uint16_t));
    CHECK(load_err == peripheral_error);

    // Successful state load
    PeripheralStatus_t load_stat = harness.descriptor()->load_state(
        harness.instance(), buffer.data(), buffer.size());
    CHECK(load_stat == peripheral_ok);

    harness.send_byte(regs::MOUSE_READ);
    const auto data = harness.drain_read();
    CHECK(data.x_low == (789 & 0xFF));
    CHECK(data.x_high == (789 >> 8));
    CHECK(data.y_low == (321 & 0xFF));
    CHECK(data.y_high == (321 >> 8));
    CHECK(data.x() == 789);
    CHECK(data.y() == 321);
    CHECK((data.status & regs::STAT_CURR_BTN0) != 0);
  }

  SUBCASE("Clamping and Coordinate Mapping") {
    MouseHarness harness;
    REQUIRE(harness.instance() != nullptr);
    harness.init_mouse_card();

    // Map input range (0..100) to internal coordinates (0..1023)
    harness.set_pos(50, 100, 25, 100);

    harness.send_byte(regs::MOUSE_READ);
    const auto data = harness.drain_read();
    CHECK(data.x() == 511);
    CHECK(data.y() == 255);
  }
}
