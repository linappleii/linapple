// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/peripherals/mouse/Mouse.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "doctest.h"

namespace {

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
    host_.RegisterCxROM = Mock_RegisterCxROM;
    init(slot);
  }

  ~MouseHarness() {
    shutdown();
    for (void* inst : extra_instances_) {
      if (inst != nullptr) {
        descriptor()->shutdown(inst);
      }
    }
    extra_instances_.clear();
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

  auto create_extra_card(int slot) -> void* {
    void* inst = descriptor()->init(slot, &host_);
    if (inst != nullptr) {
      extra_instances_.push_back(inst);
      descriptor()->reset(inst);
    }
    return inst;
  }

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

  auto read_io(uint16_t addr, void* inst = nullptr) -> uint8_t {
    void* target = (inst != nullptr) ? inst : instance_;
    if (io_handler_ == nullptr || target == nullptr) {
      return 0;
    }
    return io_handler_(target, 0, addr, 0, 0, 0);
  }

  auto write_io(uint16_t addr, uint8_t val, void* inst = nullptr) -> void {
    void* target = (inst != nullptr) ? inst : instance_;
    if (io_handler_ == nullptr || target == nullptr) {
      return;
    }
    io_handler_(target, 0, addr, 1, val, 0);
  }

  auto read_port_a(void* inst = nullptr) -> uint8_t {
    return read_io(port_a_addr(), inst);
  }
  auto write_port_a(uint8_t val, void* inst = nullptr) -> void {
    write_io(port_a_addr(), val, inst);
  }

  auto read_control_a(void* inst = nullptr) -> uint8_t {
    return read_io(control_a_addr(), inst);
  }
  auto write_control_a(uint8_t val, void* inst = nullptr) -> void {
    write_io(control_a_addr(), val, inst);
  }

  auto read_port_b(void* inst = nullptr) -> uint8_t {
    return read_io(port_b_addr(), inst);
  }
  auto write_port_b(uint8_t val, void* inst = nullptr) -> void {
    write_io(port_b_addr(), val, inst);
  }

  auto read_control_b(void* inst = nullptr) -> uint8_t {
    return read_io(control_b_addr(), inst);
  }
  auto write_control_b(uint8_t val, void* inst = nullptr) -> void {
    write_io(control_b_addr(), val, inst);
  }

  auto init_mouse_card(void* inst = nullptr) -> void {
    write_control_a(PIA_DDR_ACCESS, inst);
    write_control_b(PIA_DDR_ACCESS, inst);
    write_port_a(PIA_ALL_OUTPUTS, inst);
    write_port_b(PIA_ALL_OUTPUTS, inst);
    write_control_a(PIA_DATA_ACCESS, inst);
    write_control_b(PIA_DATA_ACCESS, inst);
  }

  auto send_byte(uint8_t val, void* inst = nullptr) -> void {
    write_port_a(val, inst);
    write_port_b(STROBE_SEND_HIGH, inst);
    write_port_b(STROBE_SEND_LOW, inst);
  }

  auto recv_byte(void* inst = nullptr) -> uint8_t {
    write_port_b(STROBE_RECV_HIGH, inst);
    write_port_b(STROBE_RECV_LOW, inst);
    return read_port_a(inst);
  }

  auto drain_read(void* inst = nullptr) -> MouseReadData_t {
    MouseReadData_t data{};
    data.x_low = read_port_a(inst);
    data.x_high = recv_byte(inst);
    data.y_low = recv_byte(inst);
    data.y_high = recv_byte(inst);
    data.status = recv_byte(inst);
    (void)recv_byte(inst);  // 6th byte resets response buffer pointer
    return data;
  }

  auto irq_asserted() const -> bool { return irq_asserted_; }
  auto last_irq_slot() const -> int { return last_irq_slot_; }
  auto clear_irq_flag() -> void { irq_asserted_ = false; }

  auto last_rom_bank_page() const -> const uint8_t* {
    return last_rom_bank_page_;
  }

  auto set_pos(int32_t x, int32_t x_range, int32_t y, int32_t y_range,
               void* inst = nullptr) -> PeripheralStatus_t {
    void* target = (inst != nullptr) ? inst : instance_;
    MousePosPayload_t payload{x, x_range, y, y_range};
    return descriptor()->command(target, mouse_cmd_set_pos, &payload,
                                 sizeof(payload));
  }

  auto set_button(uint8_t button, bool down, void* inst = nullptr)
      -> PeripheralStatus_t {
    void* target = (inst != nullptr) ? inst : instance_;
    MouseButtonPayload_t payload{button, down, {0, 0}};
    return descriptor()->command(target, mouse_cmd_set_button, &payload,
                                 sizeof(payload));
  }

  auto is_active(void* inst = nullptr) -> bool {
    void* target = (inst != nullptr) ? inst : instance_;
    uint8_t active = 0;
    size_t out_size = sizeof(active);
    PeripheralStatus_t status =
        descriptor()->query(target, mouse_query_is_active, &active, &out_size);
    return (status == peripheral_ok && active == 1);
  }

  auto on_vblank(bool vblank, void* inst = nullptr) -> void {
    void* target = (inst != nullptr) ? inst : instance_;
    if (descriptor()->on_vblank != nullptr && target != nullptr) {
      descriptor()->on_vblank(target, vblank);
    }
  }

 private:
  HostInterface_t host_{};
  void* instance_ = nullptr;
  std::vector<void*> extra_instances_{};
  PeripheralIOHandler io_handler_ = nullptr;
  int slot_ = DEFAULT_SLOT;
  bool irq_asserted_ = false;
  int last_irq_slot_ = -1;
  const uint8_t* last_rom_bank_page_ = nullptr;

  static MouseHarness* s_active_harness;

  static auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
    if (s_active_harness != nullptr && slot == s_active_harness->slot_) {
      s_active_harness->irq_asserted_ = assert_irq;
      s_active_harness->last_irq_slot_ = slot;
    }
  }

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

  static auto Mock_RegisterCxROM(int slot, uint8_t* rom_page) -> void {
    (void)slot;
    if (s_active_harness != nullptr) {
      s_active_harness->last_rom_bank_page_ = rom_page;
    }
  }
};

MouseHarness* MouseHarness::s_active_harness = nullptr;

}  // namespace

TEST_CASE("Mouse Peripheral: MS-01 Descriptor Identity & Registration") {
  const Peripheral_t* desc = mouse_get_descriptor();
  REQUIRE(desc != nullptr);
  CHECK(desc->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(desc->id) == "linapple.mouse");
  CHECK(std::string(desc->name) == "Mouse Interface");
  CHECK(desc->compatible_slots == PERIPHERAL_MASK_EXPANSION);
  CHECK(desc->default_slot == DEFAULT_SLOT);
  CHECK(desc->init != nullptr);
  CHECK(desc->reset != nullptr);
  CHECK(desc->shutdown != nullptr);
  CHECK(desc->on_vblank != nullptr);
  CHECK(desc->save_state != nullptr);
  CHECK(desc->load_state != nullptr);
  CHECK(desc->command != nullptr);
  CHECK(desc->query != nullptr);
}

TEST_CASE("Mouse Peripheral: MS-02 Lifecycle & Defensive Null Guards") {
  const Peripheral_t* desc = mouse_get_descriptor();
  REQUIRE(desc != nullptr);

  // Null host rejected cleanly
  CHECK(desc->init(DEFAULT_SLOT, nullptr) == nullptr);

  // Missing RegisterIO host rejected cleanly
  HostInterface_t bad_host{};
  CHECK(desc->init(DEFAULT_SLOT, &bad_host) == nullptr);

  // Null instance safe across callbacks
  desc->reset(nullptr);
  desc->shutdown(nullptr);
  desc->on_vblank(nullptr, true);

  size_t size = 0;
  uint8_t dummy = 0;
  CHECK(desc->save_state(nullptr, &dummy, &size) == peripheral_error);
  CHECK(desc->load_state(nullptr, &dummy, 10) == peripheral_error);
  CHECK(desc->command(nullptr, mouse_cmd_set_pos, &dummy, 1) ==
        peripheral_error);
  CHECK(desc->query(nullptr, mouse_query_is_active, &dummy, &size) ==
        peripheral_error);
}

TEST_CASE("Mouse Peripheral: MS-03 Bus MMIO Registration & ROM Banking") {
  MouseHarness harness(DEFAULT_SLOT);
  REQUIRE(harness.instance() != nullptr);

  CHECK(harness.base_addr() == 0xC0C0);
  CHECK(harness.port_a_addr() == 0xC0C0);
  CHECK(harness.control_a_addr() == 0xC0C1);
  CHECK(harness.port_b_addr() == 0xC0C2);
  CHECK(harness.control_b_addr() == 0xC0C3);

  // Bank switching via Port B bits 1-3 triggers RegisterCxROM
  harness.init_mouse_card();
  harness.write_io(harness.port_b_addr(), 0x02);  // Bank 1
  CHECK(harness.last_rom_bank_page() != nullptr);

  // Unmapped address fallthrough returns floating bus
  uint8_t unmapped = harness.read_io(0xC0C4);
  CHECK(unmapped == 0xFF);
}

TEST_CASE("Mouse Peripheral: MS-04 6821 PIA Register Read/Write") {
  MouseHarness harness(DEFAULT_SLOT);
  REQUIRE(harness.instance() != nullptr);
  harness.init_mouse_card();

  harness.write_io(harness.port_b_addr(), 0x55);
  CHECK(harness.read_io(harness.port_b_addr()) == 0x55);

  harness.write_io(harness.port_b_addr(), 0xAA);
  CHECK(harness.read_io(harness.port_b_addr()) == 0xAA);
}

TEST_CASE("Mouse Peripheral: MS-05 Protocol Handshake & Strobe Sequence") {
  MouseHarness harness(DEFAULT_SLOT);
  REQUIRE(harness.instance() != nullptr);
  harness.init_mouse_card();

  // Send MOUSE_INIT command
  harness.send_byte(regs::MOUSE_INIT);
  uint8_t byte1 = harness.read_port_a();
  CHECK(byte1 == 0xFF);

  (void)harness.recv_byte();  // drain byte 2
  (void)harness.recv_byte();  // drain byte 3
}

TEST_CASE("Mouse Peripheral: MS-06 Mouse Command Protocol") {
  MouseHarness harness(DEFAULT_SLOT);
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

  // Consecutive read without movement clears movement flag
  harness.send_byte(regs::MOUSE_READ);
  const auto data2 = harness.drain_read();
  CHECK(data2.x() == 123);
  CHECK(data2.y() == 456);
  CHECK((data2.status & regs::STAT_MOVEMENT) == 0);
}

TEST_CASE("Mouse Peripheral: MS-07 External Position Injection") {
  MouseHarness harness(DEFAULT_SLOT);
  REQUIRE(harness.instance() != nullptr);
  harness.init_mouse_card();

  // Inject position (350, 720) within range 1023
  CHECK(harness.set_pos(350, 1023, 720, 1023) == peripheral_ok);
  harness.send_byte(regs::MOUSE_READ);

  const auto data = harness.drain_read();
  CHECK(data.x() == 350);
  CHECK(data.y() == 720);
}

TEST_CASE("Mouse Peripheral: MS-08 Coordinate Clamping") {
  MouseHarness harness(DEFAULT_SLOT);
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

TEST_CASE("Mouse Peripheral: MS-09 Button State Handling") {
  MouseHarness harness(DEFAULT_SLOT);
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

  // Hold button 0 on next read
  harness.send_byte(regs::MOUSE_READ);
  auto d2 = harness.drain_read();
  CHECK((d2.status & regs::STAT_CURR_BTN0) != 0);
  CHECK((d2.status & regs::STAT_PREV_BTN0) != 0);

  // Release button 0
  CHECK(harness.set_button(0, false) == peripheral_ok);
  harness.send_byte(regs::MOUSE_READ);
  auto d3 = harness.drain_read();
  CHECK((d3.status & regs::STAT_CURR_BTN0) == 0);
  CHECK((d3.status & regs::STAT_PREV_BTN0) != 0);

  // Read again: up
  harness.send_byte(regs::MOUSE_READ);
  auto d4 = harness.drain_read();
  CHECK((d4.status & regs::STAT_CURR_BTN0) == 0);
  CHECK((d4.status & regs::STAT_PREV_BTN0) == 0);
}

TEST_CASE("Mouse Peripheral: MS-10 VBL Interrupt Generation") {
  MouseHarness harness(DEFAULT_SLOT);
  REQUIRE(harness.instance() != nullptr);
  harness.init_mouse_card();

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

TEST_CASE("Mouse Peripheral: MS-11 Movement & Button Interrupts") {
  MouseHarness harness(DEFAULT_SLOT);
  REQUIRE(harness.instance() != nullptr);
  harness.init_mouse_card();

  // Enable Button Interrupts
  harness.send_byte(regs::MOUSE_SET | regs::MODE_ON | regs::MODE_BTN_INT);
  harness.clear_irq_flag();
  CHECK(harness.set_button(0, true) == peripheral_ok);
  CHECK(harness.irq_asserted());

  // Clear via MOUSE_SERV
  harness.send_byte(regs::MOUSE_SERV);
  (void)harness.read_port_a();
  (void)harness.recv_byte();
  CHECK_FALSE(harness.irq_asserted());

  // Enable Movement Interrupts
  harness.send_byte(regs::MOUSE_SET | regs::MODE_ON | regs::MODE_MOVE_INT);
  harness.clear_irq_flag();
  harness.set_pos(200, 1023, 200, 1023);
  CHECK(harness.irq_asserted());

  // Clear via MOUSE_SERV
  harness.send_byte(regs::MOUSE_SERV);
  (void)harness.read_port_a();
  (void)harness.recv_byte();
  CHECK_FALSE(harness.irq_asserted());
}

TEST_CASE("Mouse Peripheral: MS-12 Multi-Slot Concurrency") {
  MouseHarness harness(DEFAULT_SLOT);
  void* inst1 = harness.instance();
  void* inst2 = harness.create_extra_card(ALTERNATE_SLOT);
  REQUIRE(inst1 != nullptr);
  REQUIRE(inst2 != nullptr);
  CHECK(inst1 != inst2);

  harness.init_mouse_card(inst1);
  harness.init_mouse_card(inst2);

  // Set different coordinates on each card
  CHECK(harness.set_pos(100, 1023, 200, 1023, inst1) == peripheral_ok);
  CHECK(harness.set_pos(300, 1023, 400, 1023, inst2) == peripheral_ok);

  harness.send_byte(regs::MOUSE_READ, inst1);
  const auto data1 = harness.drain_read(inst1);
  CHECK(data1.x() == 100);
  CHECK(data1.y() == 200);

  harness.send_byte(regs::MOUSE_READ, inst2);
  const auto data2 = harness.drain_read(inst2);
  CHECK(data2.x() == 300);
  CHECK(data2.y() == 400);
}

TEST_CASE("Mouse Peripheral: MS-13 Command ABI Validation") {
  MouseHarness harness(DEFAULT_SLOT);
  REQUIRE(harness.instance() != nullptr);

  // Valid set_pos
  MousePosPayload_t pos_payload{100, 1023, 100, 1023};
  CHECK(harness.descriptor()->command(harness.instance(), mouse_cmd_set_pos,
                                      &pos_payload,
                                      sizeof(pos_payload)) == peripheral_ok);

  // Undersized set_pos rejected
  CHECK(harness.descriptor()->command(harness.instance(), mouse_cmd_set_pos,
                                      &pos_payload, sizeof(pos_payload) - 1) ==
        peripheral_error);

  // Null payload rejected
  CHECK(harness.descriptor()->command(harness.instance(), mouse_cmd_set_pos,
                                      nullptr,
                                      sizeof(pos_payload)) == peripheral_error);

  // Valid set_button
  MouseButtonPayload_t btn_payload{0, true, {0, 0}};
  CHECK(harness.descriptor()->command(harness.instance(), mouse_cmd_set_button,
                                      &btn_payload,
                                      sizeof(btn_payload)) == peripheral_ok);

  // Unknown command rejected with peripheral_incompatible
  CHECK(harness.descriptor()->command(harness.instance(), 0x9999, &btn_payload,
                                      sizeof(btn_payload)) ==
        peripheral_incompatible);
}

TEST_CASE("Mouse Peripheral: MS-14 Query ABI Protocol & Sizing Probes") {
  MouseHarness harness(DEFAULT_SLOT);
  REQUIRE(harness.instance() != nullptr);

  // Pass 1 sizing probe: null buffer returns peripheral_ok and required size
  size_t size = 0;
  CHECK(harness.descriptor()->query(harness.instance(), mouse_query_is_active,
                                    nullptr, &size) == peripheral_ok);
  CHECK(size == sizeof(uint8_t));

  // Undersized buffer returns peripheral_error and sets required size
  uint8_t active = 0;
  size = 0;
  CHECK(harness.descriptor()->query(harness.instance(), mouse_query_is_active,
                                    &active, &size) == peripheral_error);
  CHECK(size == sizeof(uint8_t));

  // Pass 2 execution
  size = sizeof(uint8_t);
  CHECK(harness.descriptor()->query(harness.instance(), mouse_query_is_active,
                                    &active, &size) == peripheral_ok);
  CHECK(active == 1);

  // Unknown query returns peripheral_incompatible
  CHECK(harness.descriptor()->query(harness.instance(), 0x9999, &active,
                                    &size) == peripheral_incompatible);
}

TEST_CASE(
    "Mouse Peripheral: MS-15 Deterministic 92-byte Save State Round Trip") {
  MouseHarness harness(DEFAULT_SLOT);
  void* inst1 = harness.instance();
  void* inst2 = harness.create_extra_card(ALTERNATE_SLOT);
  REQUIRE(inst1 != nullptr);
  REQUIRE(inst2 != nullptr);

  harness.init_mouse_card(inst1);
  harness.set_pos(789, 1023, 321, 1023, inst1);
  harness.set_button(0, true, inst1);
  harness.send_byte(regs::MOUSE_SET | 0x0F, inst1);

  // Pass 1 sizing probe
  size_t state_size = 0;
  CHECK(harness.descriptor()->save_state(inst1, nullptr, &state_size) ==
        peripheral_ok);
  CHECK(state_size == sizeof(MouseSaveState_t));
  CHECK(state_size == 92);

  // Pass 2 save
  std::vector<uint8_t> buffer(state_size);
  REQUIRE(harness.descriptor()->save_state(inst1, buffer.data(), &state_size) ==
          peripheral_ok);

  const auto* ss = reinterpret_cast<const MouseSaveState_t*>(buffer.data());
  CHECK(ss->version == MOUSE_STATE_VERSION);
  CHECK(ss->struct_size == sizeof(MouseSaveState_t));
  CHECK(ss->internal_x == 789);
  CHECK(ss->internal_y == 321);

  // Restore into Card 2
  REQUIRE(harness.descriptor()->load_state(inst2, buffer.data(), state_size) ==
          peripheral_ok);

  harness.send_byte(regs::MOUSE_READ, inst2);
  const auto data = harness.drain_read(inst2);
  CHECK(data.x() == 789);
  CHECK(data.y() == 321);
  CHECK((data.status & regs::STAT_CURR_BTN0) != 0);
}

TEST_CASE("Mouse Peripheral: MS-16 Corrupt Save State Rejection") {
  MouseHarness harness(DEFAULT_SLOT);
  REQUIRE(harness.instance() != nullptr);

  size_t state_size = 0;
  REQUIRE(harness.descriptor()->save_state(harness.instance(), nullptr,
                                           &state_size) == peripheral_ok);
  std::vector<uint8_t> buffer(state_size);
  REQUIRE(harness.descriptor()->save_state(harness.instance(), buffer.data(),
                                           &state_size) == peripheral_ok);

  // Null buffer rejected
  CHECK(harness.descriptor()->load_state(harness.instance(), nullptr,
                                         state_size) == peripheral_error);

  // Undersized buffer rejected
  CHECK(harness.descriptor()->load_state(harness.instance(), buffer.data(),
                                         state_size - 1) == peripheral_error);

  // Oversized buffer rejected
  std::vector<uint8_t> oversized(state_size + 1);
  std::memcpy(oversized.data(), buffer.data(), state_size);
  CHECK(harness.descriptor()->load_state(harness.instance(), oversized.data(),
                                         oversized.size()) == peripheral_error);

  // Corrupt version rejected
  auto* ss = reinterpret_cast<MouseSaveState_t*>(buffer.data());
  uint32_t orig_ver = ss->version;
  ss->version = 999;
  CHECK(harness.descriptor()->load_state(harness.instance(), buffer.data(),
                                         state_size) == peripheral_error);
  ss->version = orig_ver;

  // Corrupt struct_size rejected
  uint32_t orig_sz = ss->struct_size;
  ss->struct_size = 50;
  CHECK(harness.descriptor()->load_state(harness.instance(), buffer.data(),
                                         state_size) == peripheral_error);
  ss->struct_size = orig_sz;

  // Clean buffer loaded successfully
  CHECK(harness.descriptor()->load_state(harness.instance(), buffer.data(),
                                         state_size) == peripheral_ok);
}
