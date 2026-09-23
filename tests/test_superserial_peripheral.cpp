// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "doctest.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

constexpr int TEST_SLOT = 2;
constexpr int REGISTERS_PER_SLOT = 16;
constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;

constexpr uint8_t STATUS_TDRE_MASK = 0x10;
constexpr uint8_t STATUS_RDRF_MASK = 0x08;
constexpr uint8_t STATUS_IRQ_MASK = 0x80;

constexpr uint8_t TEST_BYTE_A = 0x41;
constexpr uint8_t TEST_BYTE_B = 0x42;
constexpr uint8_t TEST_BYTE_C = 0x43;

struct MockHandler_t {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockHandler_t() = default;
  MockHandler_t(void* inst, PeripheralIOHandler r, PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

class SuperSerialHarness_t {
 public:
  explicit SuperSerialHarness_t(int slot = TEST_SLOT, bool auto_init = true)
      : slot_(slot) {
    s_active_instances[slot_] = this;
    setup_host();
    if (auto_init) {
      init(slot_);
    }
  }

  ~SuperSerialHarness_t() {
    shutdown();
    handlers_.clear();
    s_active_instances.erase(slot_);
  }

  SuperSerialHarness_t(const SuperSerialHarness_t&) = delete;
  auto operator=(const SuperSerialHarness_t&) -> SuperSerialHarness_t& = delete;
  SuperSerialHarness_t(SuperSerialHarness_t&&) = delete;
  auto operator=(SuperSerialHarness_t&&) -> SuperSerialHarness_t& = delete;

  auto host() -> HostInterface_t* { return &host_; }
  auto instance() const -> void* { return instance_; }
  auto slot() const -> int { return slot_; }

  auto init(int slot = -1) -> void* {
    if (slot >= 0) {
      slot_ = slot;
    }
    if (instance_ != nullptr) {
      shutdown();
    }
    instance_ = super_serial_get_descriptor()->init(slot_, &host_);
    if (instance_ != nullptr) {
      const uint16_t base =
          static_cast<uint16_t>(IO_BASE_ADDRESS + (slot_ << IO_SLOT_OFFSET));
      for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
        auto it = handlers_.find(base + i);
        if (it != handlers_.end()) {
          it->second.instance = instance_;
        }
      }
    }
    return instance_;
  }

  auto shutdown() -> void {
    if (instance_ != nullptr) {
      super_serial_get_descriptor()->shutdown(instance_);
      instance_ = nullptr;
    }
  }

  auto reset() -> void {
    if (instance_ != nullptr) {
      super_serial_get_descriptor()->reset(instance_);
    }
  }

  auto read_io(uint16_t addr, uint8_t is_write = 0, uint8_t val = 0,
               uint32_t executed_cycles = 0) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      void* target = (instance_ != nullptr) ? instance_ : it->second.instance;
      return it->second.read(target, 0, addr, is_write, val, executed_cycles);
    }
    return 0;
  }

  auto write_io(uint16_t addr, uint8_t val, uint8_t is_write = 1,
                uint32_t executed_cycles = 0) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      void* target = (instance_ != nullptr) ? instance_ : it->second.instance;
      return it->second.write(target, 0, addr, is_write, val, executed_cycles);
    }
    return 0;
  }

  auto read_data() -> uint8_t { return read_io(data_addr()); }
  auto read_status() -> uint8_t { return read_io(status_addr()); }
  auto read_command() -> uint8_t { return read_io(command_addr()); }
  auto read_control() -> uint8_t { return read_io(control_addr()); }

  auto write_data(uint8_t val) -> uint8_t { return write_io(data_addr(), val); }
  auto write_status(uint8_t val) -> uint8_t {
    return write_io(status_addr(), val);
  }
  auto write_command(uint8_t val) -> uint8_t {
    return write_io(command_addr(), val);
  }
  auto write_control(uint8_t val) -> uint8_t {
    return write_io(control_addr(), val);
  }

  auto push_rx_byte(uint8_t byte) -> PeripheralStatus_t {
    return command(SUPER_SERIAL_CMD_PUSH_RX_BYTE, &byte, sizeof(byte));
  }

  auto set_config(const SuperSerialDipSwConfig_t& config)
      -> PeripheralStatus_t {
    return command(SUPER_SERIAL_CMD_SET_CONFIG, &config, sizeof(config));
  }

  auto query_config(SuperSerialDipSwConfig_t* config, size_t* size)
      -> PeripheralStatus_t {
    return query(SUPER_SERIAL_QUERY_CONFIG, config, size);
  }

  auto query_rx_ready(uint8_t* ready, size_t* size) -> PeripheralStatus_t {
    return query(SUPER_SERIAL_QUERY_RX_READY, ready, size);
  }

  auto command(uint32_t cmd, const void* data, size_t size)
      -> PeripheralStatus_t {
    return super_serial_get_descriptor()->command(instance_, cmd, data, size);
  }

  auto query(uint32_t cmd, void* out, size_t* size) -> PeripheralStatus_t {
    return super_serial_get_descriptor()->query(instance_, cmd, out, size);
  }

  auto load_state(const void* state, size_t size) -> PeripheralStatus_t {
    return super_serial_get_descriptor()->load_state(instance_, state, size);
  }

  auto save_state(void* state, size_t* size) -> PeripheralStatus_t {
    return super_serial_get_descriptor()->save_state(instance_, state, size);
  }

  auto sent_bytes() const -> const std::vector<uint8_t>& { return sent_bytes_; }
  auto clear_sent_bytes() -> void { sent_bytes_.clear(); }

  auto irq_asserted() const -> bool { return irq_asserted_; }
  auto irq_slot() const -> int { return irq_slot_; }
  auto clear_irq() -> void {
    irq_asserted_ = false;
    irq_slot_ = -1;
  }

  auto last_baud() const -> uint32_t { return last_baud_; }
  auto last_bits() const -> uint32_t { return last_bits_; }
  auto last_parity() const -> int { return last_parity_; }
  auto last_stop() const -> int { return last_stop_; }
  auto update_state_calls() const -> uint32_t { return update_state_calls_; }

  auto data_addr() const -> uint16_t {
    return static_cast<uint16_t>(IO_BASE_ADDRESS + (slot_ << IO_SLOT_OFFSET) +
                                 8);
  }
  auto status_addr() const -> uint16_t {
    return static_cast<uint16_t>(IO_BASE_ADDRESS + (slot_ << IO_SLOT_OFFSET) +
                                 9);
  }
  auto command_addr() const -> uint16_t {
    return static_cast<uint16_t>(IO_BASE_ADDRESS + (slot_ << IO_SLOT_OFFSET) +
                                 10);
  }
  auto control_addr() const -> uint16_t {
    return static_cast<uint16_t>(IO_BASE_ADDRESS + (slot_ << IO_SLOT_OFFSET) +
                                 11);
  }

 private:
  auto setup_host() -> void {
    host_ = {};
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.RegisterCxROM = Mock_RegisterCxROM;
    host_.SerialTransmitByte = Mock_SerialTransmitByte;
    host_.SerialUpdateState = Mock_SerialUpdateState;
  }

  static auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
    auto it = s_active_instances.find(slot);
    if (it != s_active_instances.end()) {
      it->second->irq_slot_ = slot;
      it->second->irq_asserted_ = assert_irq;
    }
  }

  static auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                              PeripheralIOHandler write_c0,
                              PeripheralIOHandler read_cx,
                              PeripheralIOHandler write_cx) -> void {
    (void)read_cx;
    (void)write_cx;
    auto it = s_active_instances.find(slot);
    if (it != s_active_instances.end() &&
        (read_c0 != nullptr || write_c0 != nullptr)) {
      const uint16_t base =
          static_cast<uint16_t>(IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET));
      for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
        it->second->handlers_[base + i] =
            MockHandler_t(nullptr, read_c0, write_c0);
      }
    }
  }

  static auto Mock_RegisterCxROM(int slot, const uint8_t* rom_ptr) -> void {
    (void)slot;
    (void)rom_ptr;
  }

  static auto Mock_SerialTransmitByte(void* instance, uint8_t byte) -> void {
    for (auto& pair : s_active_instances) {
      if (pair.second->instance_ == instance) {
        pair.second->sent_bytes_.push_back(byte);
        break;
      }
    }
  }

  static auto Mock_SerialUpdateState(void* instance, uint32_t baud,
                                     uint32_t bits, int parity, int stop)
      -> void {
    for (auto& pair : s_active_instances) {
      if (pair.second->instance_ == instance) {
        pair.second->last_baud_ = baud;
        pair.second->last_bits_ = bits;
        pair.second->last_parity_ = parity;
        pair.second->last_stop_ = stop;
        pair.second->update_state_calls_++;
        break;
      }
    }
  }

  int slot_ = TEST_SLOT;
  void* instance_ = nullptr;
  HostInterface_t host_{};
  std::map<uint16_t, MockHandler_t> handlers_{};
  std::vector<uint8_t> sent_bytes_{};
  bool irq_asserted_ = false;
  int irq_slot_ = -1;
  uint32_t last_baud_ = 0;
  uint32_t last_bits_ = 0;
  int last_parity_ = 0;
  int last_stop_ = 0;
  uint32_t update_state_calls_ = 0;

  static std::map<int, SuperSerialHarness_t*> s_active_instances;
};

std::map<int, SuperSerialHarness_t*> SuperSerialHarness_t::s_active_instances;

}  // namespace

TEST_CASE("SSC-01: Descriptor and Registration") {
  Peripheral_t* desc = super_serial_get_descriptor();
  REQUIRE_NE(desc, nullptr);
  CHECK_EQ(desc->abi_version, LINAPPLE_ABI_VERSION);
  CHECK_EQ(std::string(desc->id), "linapple.ssc");
  CHECK_EQ(std::string(desc->name), "Super Serial Card");
  CHECK_EQ(desc->compatible_slots, PERIPHERAL_MASK_EXPANSION);
  CHECK_EQ(desc->default_slot, 2);

  CHECK_NE(desc->init, nullptr);
  CHECK_NE(desc->reset, nullptr);
  CHECK_NE(desc->shutdown, nullptr);
  CHECK_NE(desc->save_state, nullptr);
  CHECK_NE(desc->load_state, nullptr);
  CHECK_NE(desc->command, nullptr);
  CHECK_NE(desc->query, nullptr);
}

TEST_CASE("SSC-02: Lifecycle and Defensive Null Guards") {
  Peripheral_t* desc = super_serial_get_descriptor();
  REQUIRE_NE(desc, nullptr);

  // Null host check
  void* inst = desc->init(2, nullptr);
  CHECK_EQ(inst, nullptr);

  // Safe null operations
  desc->reset(nullptr);
  desc->shutdown(nullptr);

  // Proper initialization via harness
  SuperSerialHarness_t harness(2, true);
  CHECK_NE(harness.instance(), nullptr);
  CHECK_EQ(harness.slot(), 2);
}

TEST_CASE("SSC-03: Bus Seam & Address Decoding") {
  SuperSerialHarness_t harness(2);
  const uint8_t floating = mem_read_floating_bus(0);

  // Slot 2 addresses: $C0A0..$C0AF
  // Offsets 0..7 and 12..15 should return floating bus (read with is_write=0)
  for (uint16_t offset = 0; offset <= 7; ++offset) {
    const uint16_t addr = static_cast<uint16_t>(0xC0A0 + offset);
    CHECK_EQ(harness.read_io(addr, 0, 0, 0), floating);
  }
  for (uint16_t offset = 12; offset <= 15; ++offset) {
    const uint16_t addr = static_cast<uint16_t>(0xC0A0 + offset);
    CHECK_EQ(harness.read_io(addr, 0, 0, 0), floating);
  }

  // Write on read path should return floating bus
  CHECK_EQ(harness.read_io(0xC0A8, 1, 0, 0), floating);
  // Read on write path should return 0
  CHECK_EQ(harness.write_io(0xC0A8, 0x55, 0, 0), 0);
}

TEST_CASE("SSC-04: Status Register Bitfield Fidelity") {
  SuperSerialHarness_t harness(2);

  // Initial status: TDRE bit 4 must be set (Transmitter Data Register Empty)
  uint8_t status = harness.read_status();
  CHECK_EQ(status & STATUS_TDRE_MASK, STATUS_TDRE_MASK);
  CHECK_EQ(status & STATUS_RDRF_MASK, 0);
  CHECK_EQ(status & STATUS_IRQ_MASK, 0);

  // Push byte: RDRF bit 3 must now be set
  REQUIRE_EQ(harness.push_rx_byte(TEST_BYTE_A), peripheral_ok);
  status = harness.read_status();
  CHECK_EQ(status & STATUS_RDRF_MASK, STATUS_RDRF_MASK);

  // Pop byte: RDRF bit 3 must be cleared
  uint8_t byte = harness.read_data();
  CHECK_EQ(byte, TEST_BYTE_A);
  status = harness.read_status();
  CHECK_EQ(status & STATUS_RDRF_MASK, 0);
}

TEST_CASE("SSC-05: Transmitter Data Write and Host Forwarding") {
  SuperSerialHarness_t harness(2);

  harness.write_data(TEST_BYTE_A);
  harness.write_data(TEST_BYTE_B);
  harness.write_data(TEST_BYTE_C);

  REQUIRE_EQ(harness.sent_bytes().size(), 3U);
  CHECK_EQ(harness.sent_bytes().at(0), TEST_BYTE_A);
  CHECK_EQ(harness.sent_bytes().at(1), TEST_BYTE_B);
  CHECK_EQ(harness.sent_bytes().at(2), TEST_BYTE_C);
}

TEST_CASE("SSC-06: Control Register Decoding & Host Baud Sync") {
  SuperSerialHarness_t harness(2);

  // 0x1E: baud index 0x0E (9600 baud), bits 8-0 = 8 data bits, stop bit 0 = 1
  // stop
  harness.write_control(0x1E);
  CHECK_EQ(harness.update_state_calls(), 1U);
  CHECK_EQ(harness.last_baud(), 9600U);
  CHECK_EQ(harness.last_bits(), 8U);
  CHECK_EQ(harness.last_stop(), SUPER_SERIAL_STOP_BITS_1);

  // 0x90: baud index 0x00 (0 baud/ext), bits 8-0 = 8 data bits, stop bit 1 = 2
  // stop
  harness.write_control(0x90);
  CHECK_EQ(harness.update_state_calls(), 2U);
  CHECK_EQ(harness.last_baud(), 0U);
  CHECK_EQ(harness.last_bits(), 8U);
  CHECK_EQ(harness.last_stop(), SUPER_SERIAL_STOP_BITS_2);

  // Read back control register
  CHECK_EQ(harness.read_control(), 0x90);
}

TEST_CASE("SSC-07: Command Register Decoding & Parity Sync") {
  SuperSerialHarness_t harness(2);

  // Write odd parity (bit 5 = 1, bits 6..7 = 00)
  harness.write_command(0x20);
  CHECK_EQ(harness.last_parity(), SUPER_SERIAL_PARITY_ODD);

  // Write even parity (bit 5 = 1, bits 6..7 = 01)
  harness.write_command(0x60);
  CHECK_EQ(harness.last_parity(), SUPER_SERIAL_PARITY_EVEN);

  // Write no parity (bit 5 = 0)
  harness.write_command(0x00);
  CHECK_EQ(harness.last_parity(), SUPER_SERIAL_PARITY_NONE);

  // Read back command register
  CHECK_EQ(harness.read_command(), 0x00);
}

TEST_CASE("SSC-08: Interrupt Assertion on Incoming Byte") {
  SuperSerialHarness_t harness(2);

  // Bit 1 is 0 -> receiver IRQ enabled
  harness.write_command(0x00);
  harness.clear_irq();

  REQUIRE_EQ(harness.push_rx_byte(TEST_BYTE_A), peripheral_ok);
  CHECK_EQ(harness.irq_asserted(), true);
  CHECK_EQ(harness.irq_slot(), 2);
  CHECK_EQ(harness.read_status() & STATUS_IRQ_MASK, STATUS_IRQ_MASK);

  // Reset and test with receiver IRQ disabled (bit 1 is 1 -> 0x02)
  harness.reset();
  harness.clear_irq();
  harness.write_command(0x02);

  REQUIRE_EQ(harness.push_rx_byte(TEST_BYTE_B), peripheral_ok);
  CHECK_EQ(harness.irq_asserted(), false);
  CHECK_EQ(harness.read_status() & STATUS_IRQ_MASK, 0);
}

TEST_CASE("SSC-09: Interrupt Deassertion on Data Read (Offset 8)") {
  SuperSerialHarness_t harness(2);

  // Enable receiver IRQ and push byte
  harness.write_command(0x00);
  REQUIRE_EQ(harness.push_rx_byte(TEST_BYTE_A), peripheral_ok);
  CHECK_EQ(harness.irq_asserted(), true);

  // Reading data register must deassert IRQ immediately
  uint8_t byte = harness.read_data();
  CHECK_EQ(byte, TEST_BYTE_A);
  CHECK_EQ(harness.irq_asserted(), false);
  CHECK_EQ(harness.read_status() & STATUS_IRQ_MASK, 0);
}

TEST_CASE("SSC-10: Interrupt Deassertion on Programmed Reset (Offset 9)") {
  SuperSerialHarness_t harness(2);

  harness.write_command(0x00);
  REQUIRE_EQ(harness.push_rx_byte(TEST_BYTE_A), peripheral_ok);
  CHECK_EQ(harness.irq_asserted(), true);

  // Writing to offset 9 (Programmed Reset) must deassert IRQ
  harness.write_status(0x00);
  CHECK_EQ(harness.irq_asserted(), false);
  CHECK_EQ(harness.read_status() & STATUS_IRQ_MASK, 0);
}

TEST_CASE("SSC-11: Interrupt Deassertion on Command IRQ Disable (Offset 10)") {
  SuperSerialHarness_t harness(2);

  harness.write_command(0x00);
  REQUIRE_EQ(harness.push_rx_byte(TEST_BYTE_A), peripheral_ok);
  CHECK_EQ(harness.irq_asserted(), true);

  // Disabling receiver IRQ (bit 1 is 1) must deassert pending IRQ
  harness.write_command(0x02);
  CHECK_EQ(harness.irq_asserted(), false);
  CHECK_EQ(harness.read_status() & STATUS_IRQ_MASK, 0);
}

TEST_CASE("SSC-12: Interrupt Deassertion on Hardware Reset and Shutdown") {
  SuperSerialHarness_t harness(2);

  // Test reset path
  harness.write_command(0x00);
  REQUIRE_EQ(harness.push_rx_byte(TEST_BYTE_A), peripheral_ok);
  CHECK_EQ(harness.irq_asserted(), true);
  harness.reset();
  CHECK_EQ(harness.irq_asserted(), false);

  // Test shutdown path
  harness.write_command(0x00);
  REQUIRE_EQ(harness.push_rx_byte(TEST_BYTE_B), peripheral_ok);
  CHECK_EQ(harness.irq_asserted(), true);
  harness.shutdown();
  CHECK_EQ(harness.irq_asserted(), false);
}

TEST_CASE("SSC-13: FIFO Queuing, Capacity and Saturation") {
  SuperSerialHarness_t harness(2);

  // Push exactly 9 bytes into the FIFO
  for (uint8_t i = 1; i <= 9; ++i) {
    REQUIRE_EQ(harness.push_rx_byte(i), peripheral_ok);
  }

  // RX Ready should be true
  uint8_t ready = 0;
  size_t size = sizeof(ready);
  REQUIRE_EQ(harness.query_rx_ready(&ready, &size), peripheral_ok);
  CHECK_EQ(ready, 1);

  // 10th push should not overflow the 9-byte FIFO
  REQUIRE_EQ(harness.push_rx_byte(10), peripheral_ok);

  // Read all 9 bytes back; order must be FIFO (1..9)
  for (uint8_t i = 1; i <= 9; ++i) {
    CHECK_EQ(harness.read_data(), i);
  }

  // 10th read should return 0 (FIFO empty)
  CHECK_EQ(harness.read_data(), 0);

  // RX Ready should now report false (0)
  size = sizeof(ready);
  REQUIRE_EQ(harness.query_rx_ready(&ready, &size), peripheral_ok);
  CHECK_EQ(ready, 0);
}

TEST_CASE("SSC-14: Command ABI Parameter Validation") {
  SuperSerialHarness_t harness(2);

  uint8_t byte = 0x55;
  CHECK_EQ(
      harness.command(SUPER_SERIAL_CMD_PUSH_RX_BYTE, nullptr, sizeof(byte)),
      peripheral_error);
  CHECK_EQ(harness.command(SUPER_SERIAL_CMD_PUSH_RX_BYTE, &byte, 0),
           peripheral_error);

  SuperSerialDipSwConfig_t config{};
  CHECK_EQ(
      harness.command(SUPER_SERIAL_CMD_SET_CONFIG, nullptr, sizeof(config)),
      peripheral_error);
  CHECK_EQ(
      harness.command(SUPER_SERIAL_CMD_SET_CONFIG, &config, sizeof(config) - 1),
      peripheral_error);

  CHECK_EQ(harness.command(0x9999, &byte, sizeof(byte)),
           peripheral_incompatible);
}

TEST_CASE("SSC-15: Query ABI Protocol and Sizing Probes") {
  SuperSerialHarness_t harness(2);

  // Null buffer_size pointer
  CHECK_EQ(harness.query(SUPER_SERIAL_QUERY_CONFIG, nullptr, nullptr),
           peripheral_error);

  // Pass 1 sizing probe for SUPER_SERIAL_QUERY_CONFIG
  size_t config_size = 0;
  REQUIRE_EQ(harness.query_config(nullptr, &config_size), peripheral_ok);
  CHECK_EQ(config_size, sizeof(SuperSerialDipSwConfig_t));

  // Undersized buffer for CONFIG
  SuperSerialDipSwConfig_t config{};
  config_size = sizeof(config) - 1;
  CHECK_EQ(harness.query_config(&config, &config_size), peripheral_error);

  // Pass 1 sizing probe for SUPER_SERIAL_QUERY_RX_READY
  size_t ready_size = 0;
  REQUIRE_EQ(harness.query_rx_ready(nullptr, &ready_size), peripheral_ok);
  CHECK_EQ(ready_size, sizeof(uint8_t));

  // Undersized buffer for RX_READY
  uint8_t ready = 0;
  ready_size = 0;
  CHECK_EQ(harness.query_rx_ready(&ready, &ready_size), peripheral_error);

  // Unknown query ID
  size_t query_size = sizeof(ready);
  CHECK_EQ(harness.query(0x9999, &ready, &query_size), peripheral_incompatible);
}

TEST_CASE("SSC-16: Deterministic Save State Sizing and Round-Trip") {
  SuperSerialHarness_t harness1(2);

  // Pass 1: Sizing probe
  size_t state_size = 0;
  REQUIRE_EQ(harness1.save_state(nullptr, &state_size), peripheral_ok);
  CHECK_EQ(state_size, 56U);

  // Configure harness 1
  SuperSerialDipSwConfig_t orig_config{};
  orig_config.baud_rate = SUPER_SERIAL_BAUD_9600;
  orig_config.firmware_mode = SUPER_SERIAL_FIRMWARE_SIC_P8;
  orig_config.stop_bits = SUPER_SERIAL_STOP_BITS_2;
  orig_config.byte_size = SUPER_SERIAL_BITS_7;
  orig_config.parity = SUPER_SERIAL_PARITY_EVEN;
  orig_config.linefeed = true;
  orig_config.interrupts = true;
  REQUIRE_EQ(harness1.set_config(orig_config), peripheral_ok);

  harness1.write_control(0x1E);  // 9600 baud, 8 bits
  harness1.write_command(0x00);  // RX IRQ enabled
  REQUIRE_EQ(harness1.push_rx_byte(TEST_BYTE_A), peripheral_ok);
  REQUIRE_EQ(harness1.push_rx_byte(TEST_BYTE_B), peripheral_ok);
  CHECK_EQ(harness1.irq_asserted(), true);

  // Pass 2: Save state
  std::vector<uint8_t> buffer(state_size, 0);
  REQUIRE_EQ(harness1.save_state(buffer.data(), &state_size), peripheral_ok);
  CHECK_EQ(state_size, 56U);

  // Verify byte layout
  const auto* state_header =
      reinterpret_cast<const SuperSerialSaveState_t*>(buffer.data());
  CHECK_EQ(state_header->version,
           static_cast<uint32_t>(SUPER_SERIAL_STATE_VERSION));
  CHECK_EQ(state_header->struct_size, 56U);
  CHECK_EQ(state_header->rx_count, 2U);
  CHECK_EQ(state_header->control_byte, 0x1E);
  CHECK_EQ(state_header->command_byte, 0x00);
  CHECK_EQ(state_header->is_irq_pending, 1U);

  // Restore into fresh harness 2
  SuperSerialHarness_t harness2(2);
  REQUIRE_EQ(harness2.load_state(buffer.data(), buffer.size()), peripheral_ok);

  // Hardware state restoration check
  CHECK_EQ(harness2.irq_asserted(), true);
  CHECK_EQ(harness2.last_baud(), 9600U);
  CHECK_EQ(harness2.last_bits(), 8U);
  CHECK_EQ(harness2.read_control(), 0x1E);
  CHECK_EQ(harness2.read_command(), 0x00);

  // Popping first byte clears IRQ
  CHECK_EQ(harness2.read_data(), TEST_BYTE_A);
  CHECK_EQ(harness2.irq_asserted(), false);
  CHECK_EQ(harness2.read_data(), TEST_BYTE_B);
  CHECK_EQ(harness2.read_data(), 0);

  // Verify config restoration
  SuperSerialDipSwConfig_t restored_config{};
  size_t conf_len = sizeof(restored_config);
  REQUIRE_EQ(harness2.query_config(&restored_config, &conf_len), peripheral_ok);
  CHECK_EQ(restored_config.baud_rate, SUPER_SERIAL_BAUD_9600);
  CHECK_EQ(restored_config.firmware_mode, SUPER_SERIAL_FIRMWARE_SIC_P8);
  CHECK_EQ(restored_config.parity, SUPER_SERIAL_PARITY_EVEN);
  CHECK_EQ(restored_config.linefeed, true);
}

TEST_CASE("SSC-17: Corrupt State Rejection") {
  SuperSerialHarness_t harness(2);

  SuperSerialSaveState_t valid_state{};
  valid_state.version = SUPER_SERIAL_STATE_VERSION;
  valid_state.struct_size = sizeof(SuperSerialSaveState_t);

  // Null buffer
  CHECK_EQ(harness.load_state(nullptr, sizeof(valid_state)), peripheral_error);

  // Undersized buffer
  CHECK_EQ(harness.load_state(&valid_state, sizeof(valid_state) - 1),
           peripheral_error);

  // Oversized buffer
  CHECK_EQ(harness.load_state(&valid_state, sizeof(valid_state) + 1),
           peripheral_error);

  // Bad version
  SuperSerialSaveState_t bad_version = valid_state;
  bad_version.version = 99;
  CHECK_EQ(harness.load_state(&bad_version, sizeof(bad_version)),
           peripheral_error);

  // Bad struct_size
  SuperSerialSaveState_t bad_size = valid_state;
  bad_size.struct_size = 32;
  CHECK_EQ(harness.load_state(&bad_size, sizeof(bad_size)), peripheral_error);
}

TEST_CASE("SSC-18: Multi-Slot Concurrency") {
  SuperSerialHarness_t card_slot1(1);
  SuperSerialHarness_t card_slot2(2);

  // Slot 1: $C098..$C09B; Slot 2: $C0A8..$C0AB
  CHECK_EQ(card_slot1.data_addr(), 0xC098);
  CHECK_EQ(card_slot2.data_addr(), 0xC0A8);

  // Enable IRQs on both
  card_slot1.write_command(0x00);
  card_slot2.write_command(0x00);

  // Push byte to Slot 1 only
  REQUIRE_EQ(card_slot1.push_rx_byte(TEST_BYTE_A), peripheral_ok);
  CHECK_EQ(card_slot1.irq_asserted(), true);
  CHECK_EQ(card_slot1.irq_slot(), 1);
  CHECK_EQ(card_slot2.irq_asserted(), false);

  // Push byte to Slot 2
  REQUIRE_EQ(card_slot2.push_rx_byte(TEST_BYTE_B), peripheral_ok);
  CHECK_EQ(card_slot2.irq_asserted(), true);
  CHECK_EQ(card_slot2.irq_slot(), 2);

  // Read Slot 1 data: clears Slot 1 IRQ, Slot 2 retains IRQ
  CHECK_EQ(card_slot1.read_data(), TEST_BYTE_A);
  CHECK_EQ(card_slot1.irq_asserted(), false);
  CHECK_EQ(card_slot2.read_data(), TEST_BYTE_B);
  CHECK_EQ(card_slot2.irq_asserted(), false);
}
