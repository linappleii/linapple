// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstddef>
#include <cstring>
#include <map>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/Peripheral.h"
#include "doctest.h"

// Scoped dummy memory pointer definition for linkage compatibility
uint8_t* mem = nullptr;

// Stub for auto-registration since core/Peripheral.cpp is not linked
extern "C" auto peripheral_register_builtin(Peripheral_t* p) -> void {
  (void)p;
}

namespace {

constexpr int TEST_SLOT = 2;
constexpr int REGISTERS_PER_SLOT = 16;
constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;

constexpr uint16_t ADDR_DATA = 0xC0A8;
constexpr uint16_t ADDR_STATUS = 0xC0A9;
constexpr uint16_t ADDR_COMMAND = 0xC0AA;

constexpr uint8_t STATUS_TDRE_MASK = 0x10;
constexpr uint8_t STATUS_RX_FULL_MASK = 0x08;

constexpr uint8_t CMD_ENABLE_RX_IRQ = 0x01;
constexpr uint8_t CMD_ENABLE_TX_IRQ = 0x04;

constexpr uint8_t TEST_BYTE_VAL = 0x55;
constexpr uint8_t TEST_BYTE_A = 0x41;

constexpr size_t MEMORY_SIZE_64K = 65536;

struct MockHandler_t {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockHandler_t() = default;
  MockHandler_t(void* inst, PeripheralIOHandler r, PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

class SuperSerialHarness {
 public:
  explicit SuperSerialHarness(int slot = TEST_SLOT, bool auto_init = true)
      : slot_(slot) {
    s_active_harness = this;
    scoped_mem_.fill(0);
    prev_mem_ = mem;
    mem = scoped_mem_.data();

    setup_host();

    if (auto_init) {
      init(slot_);
    }
  }

  ~SuperSerialHarness() {
    shutdown();
    handlers_.clear();
    mem = prev_mem_;
    s_active_harness = nullptr;
  }

  SuperSerialHarness(const SuperSerialHarness&) = delete;
  auto operator=(const SuperSerialHarness&) -> SuperSerialHarness& = delete;
  SuperSerialHarness(SuperSerialHarness&&) = delete;
  auto operator=(SuperSerialHarness&&) -> SuperSerialHarness& = delete;

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
      const uint16_t base = IO_BASE_ADDRESS + (slot_ << IO_SLOT_OFFSET);
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

  auto read_io(uint16_t addr, uint8_t is_write = 0, uint8_t val = 0)
      -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      void* target = (instance_ != nullptr) ? instance_ : it->second.instance;
      return it->second.read(target, 0, addr, is_write, val, 0);
    }
    return 0;
  }

  auto write_io(uint16_t addr, uint8_t val, uint8_t is_write = 1) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      void* target = (instance_ != nullptr) ? instance_ : it->second.instance;
      return it->second.write(target, 0, addr, is_write, val, 0);
    }
    return 0;
  }

  auto read_status() -> uint8_t { return read_io(status_addr()); }
  auto read_data() -> uint8_t { return read_io(data_addr()); }
  auto write_command(uint8_t val) -> uint8_t {
    return write_io(command_addr(), val);
  }
  auto write_data(uint8_t val) -> uint8_t { return write_io(data_addr(), val); }

  auto push_rx_byte(uint8_t byte) -> PeripheralStatus_t {
    return command(SUPER_SERIAL_CMD_PUSH_RX_BYTE, &byte, sizeof(byte));
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
  auto set_cycles(uint64_t cycles) -> void { cycles_ = cycles; }

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
    host_.Log = Mock_Log;
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.RegisterCxROM = Mock_RegisterCxROM;
    host_.RegisterExpansionROM = Mock_RegisterExpansionROM;
    host_.RegisterDirectIO = Mock_RegisterDirectIO;
    host_.get_mem_ptr = Mock_GetMemPtr;
    host_.GetCycles = Mock_GetCycles;
    host_.SerialTransmitByte = Mock_SerialTransmitByte;
  }

  static auto Mock_Log(void* instance, PeripheralLogLevel_t level,
                       const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    (void)fmt;
  }

  static auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->irq_slot_ = slot;
      s_active_harness->irq_asserted_ = assert_irq;
    }
  }

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  // Justification: Signature required by HostInterface_t C ABI.
  static auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                              PeripheralIOHandler write_c0,
                              PeripheralIOHandler read_cx,
                              PeripheralIOHandler write_cx) -> void {
    (void)read_cx;
    (void)write_cx;
    if (s_active_harness != nullptr &&
        (read_c0 != nullptr || write_c0 != nullptr)) {
      const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
      for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
        s_active_harness->handlers_[base + i] = {nullptr, read_c0, write_c0};
      }
    }
  }

  static auto Mock_RegisterCxROM(int slot, uint8_t* rom_ptr) -> void {
    (void)slot;
    (void)rom_ptr;
  }

  static auto Mock_RegisterExpansionROM(int slot, uint8_t* rom_ptr) -> void {
    (void)slot;
    (void)rom_ptr;
  }

  static auto Mock_RegisterDirectIO(void* instance, uint16_t addr,
                                    PeripheralIOHandler read,
                                    PeripheralIOHandler write) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->handlers_[addr] = {instance, read, write};
    }
  }

  static auto Mock_SerialTransmitByte(void* instance, uint8_t byte) -> void {
    (void)instance;
    if (s_active_harness != nullptr) {
      s_active_harness->sent_bytes_.push_back(byte);
    }
  }
  // NOLINTEND(bugprone-easily-swappable-parameters)

  static auto Mock_GetMemPtr(uint16_t addr) -> uint8_t* {
    if (s_active_harness != nullptr && addr < MEMORY_SIZE_64K) {
      return &s_active_harness->scoped_mem_[addr];
    }
    return nullptr;
  }

  static auto Mock_GetCycles() -> uint64_t {
    if (s_active_harness != nullptr) {
      return s_active_harness->cycles_;
    }
    return 0;
  }

  HostInterface_t host_{};
  std::map<uint16_t, MockHandler_t> handlers_{};
  std::vector<uint8_t> sent_bytes_{};
  bool irq_asserted_ = false;
  int irq_slot_ = -1;
  uint64_t cycles_ = 0;
  std::array<uint8_t, MEMORY_SIZE_64K> scoped_mem_{};
  uint8_t* prev_mem_ = nullptr;
  void* instance_ = nullptr;
  int slot_ = TEST_SLOT;

  static SuperSerialHarness* s_active_harness;
};

SuperSerialHarness* SuperSerialHarness::s_active_harness = nullptr;

}  // namespace

TEST_CASE("SuperSerial: Status Register Bit 4 (TDRE) Set On Reset") {
  SuperSerialHarness harness;
  REQUIRE(harness.instance() != nullptr);

  harness.reset();

  const uint8_t status = harness.read_status();
  CHECK((status & STATUS_TDRE_MASK) != 0);
}

TEST_CASE("SuperSerial: Transmit and Interrupt Behavior") {
  SuperSerialHarness harness;
  REQUIRE(harness.instance() != nullptr);

  harness.write_command(CMD_ENABLE_TX_IRQ);
  harness.write_data(TEST_BYTE_A);

  REQUIRE(harness.sent_bytes().size() == 1);
  CHECK(harness.sent_bytes().at(0) == TEST_BYTE_A);
}

TEST_CASE("SuperSerial: Receive Buffer and RX IRQ") {
  SuperSerialHarness harness;
  REQUIRE(harness.instance() != nullptr);

  harness.write_command(CMD_ENABLE_RX_IRQ);

  const uint8_t rx_byte = TEST_BYTE_VAL;
  CHECK(harness.push_rx_byte(rx_byte) == peripheral_ok);

  CHECK(harness.irq_asserted() == true);

  uint8_t status = harness.read_status();
  CHECK((status & STATUS_RX_FULL_MASK) != 0);

  const uint8_t read_byte = harness.read_data();
  CHECK(read_byte == TEST_BYTE_VAL);

  status = harness.read_status();
  CHECK((status & STATUS_RX_FULL_MASK) == 0);
}

TEST_CASE("SuperSerial: Robustness and ABI") {
  CHECK(super_serial_get_descriptor()->init(TEST_SLOT, nullptr) == nullptr);

  SuperSerialHarness harness;
  REQUIRE(harness.instance() != nullptr);

  SuperSerialDipSwConfig_t cfg = {.baud_rate = SUPER_SERIAL_BAUD_9600,
                                  .firmware_mode = SUPER_SERIAL_FIRMWARE_CIC,
                                  .stop_bits = SUPER_SERIAL_STOP_BITS_1,
                                  .byte_size = SUPER_SERIAL_BITS_8,
                                  .parity = SUPER_SERIAL_PARITY_NONE,
                                  .linefeed = false,
                                  .interrupts = false};
  CHECK(harness.command(SUPER_SERIAL_CMD_SET_CONFIG, &cfg, sizeof(cfg)) ==
        peripheral_ok);

  SuperSerialDipSwConfig_t queried = {
      .baud_rate = SUPER_SERIAL_BAUD_110,
      .firmware_mode = SUPER_SERIAL_FIRMWARE_CIC,
      .stop_bits = SUPER_SERIAL_STOP_BITS_1,
      .byte_size = SUPER_SERIAL_BITS_8,
      .parity = SUPER_SERIAL_PARITY_NONE,
      .linefeed = false,
      .interrupts = false};
  size_t size = sizeof(queried);
  CHECK(harness.query(SUPER_SERIAL_QUERY_CONFIG, &queried, &size) ==
        peripheral_ok);
  CHECK(queried.baud_rate == SUPER_SERIAL_BAUD_9600);
}

TEST_CASE("SuperSerial: [SSC-13] Snapshot rx_count bounds check") {
  SuperSerialHarness harness;
  REQUIRE(harness.instance() != nullptr);

  SS_IO_Comms state{};
  state.control_byte = 0x12;
  state.command_byte = 0x34;
  state.recv_bytes = 999999;  // Exceeds SUPER_SERIAL_FIFO_SIZE (256)
  std::memset(state.recv_buffer, 0x77, sizeof(state.recv_buffer));

  const PeripheralStatus_t load_status =
      harness.load_state(&state, sizeof(state));
  CHECK(load_status == peripheral_ok);

  bool rx_ready = false;
  size_t query_size = sizeof(rx_ready);
  CHECK(harness.query(SUPER_SERIAL_QUERY_RX_READY, &rx_ready, &query_size) ==
        peripheral_ok);
  CHECK(rx_ready == true);

  // Read back all bytes; exactly SUPER_SERIAL_FIFO_SIZE (256) should be
  // available
  for (size_t i = 0; i < SUPER_SERIAL_FIFO_SIZE; ++i) {
    const uint8_t byte = harness.read_data();
    CHECK(byte == 0x77);
  }

  // After draining 256 bytes, rx should be empty
  const uint8_t status = harness.read_status();
  CHECK((status & STATUS_RX_FULL_MASK) == 0);
}
