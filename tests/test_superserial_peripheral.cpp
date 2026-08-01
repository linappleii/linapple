// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/Peripheral.h"
#include "doctest.h"

extern "C" uint64_t g_cumulative_cycles;
extern "C" double g_current_clk_6502;

extern uint8_t* mem;
constexpr size_t MEMORY_SIZE_64K = 65536;
static std::array<uint8_t, MEMORY_SIZE_64K> dummy_mem{};
uint8_t* mem = dummy_mem.data();

extern "C" auto video_get_scanner_address(uint32_t*, uint32_t) -> uint16_t {
  return 0;
}

auto mem_read_floating_bus(uint32_t) -> uint8_t { return 0; }

// Stub for auto-registration
extern "C" void peripheral_register_builtin(Peripheral_t* p) { (void)p; }

namespace {

constexpr uint16_t ADDR_DATA = 0xC0A8;
constexpr uint16_t ADDR_STATUS = 0xC0A9;
constexpr uint16_t ADDR_COMMAND = 0xC0AA;

constexpr int TEST_SLOT = 2;
constexpr int REGISTERS_PER_SLOT = 16;
constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;

constexpr uint8_t STATUS_TDRE_MASK = 0x10;
constexpr uint8_t STATUS_RX_FULL_MASK = 0x08;

constexpr uint8_t CMD_ENABLE_RX_IRQ = 0x01;
constexpr uint8_t CMD_ENABLE_TX_IRQ = 0x04;

constexpr uint8_t TEST_BYTE_VAL = 0x55;
constexpr uint8_t TEST_BYTE_A = 0x41;

struct MockHandler {
  void* instance;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
};

static std::map<uint16_t, MockHandler> g_mock_handlers;
static std::vector<uint8_t> g_sent_bytes;
static bool g_irq_asserted = false;

auto Mock_Log(void* instance, PeripheralLogLevel_t level, const char* fmt, ...)
    -> void {
  (void)instance;
  (void)level;
  (void)fmt;
}

auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
  (void)slot;
  g_irq_asserted = assert_irq;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
// Justification: ABI signature required by HostInterface_t.
auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                     PeripheralIOHandler write_c0, PeripheralIOHandler read_cx,
                     PeripheralIOHandler write_cx) -> void {
  (void)read_cx;
  (void)write_cx;
  if (read_c0 != nullptr || write_c0 != nullptr) {
    const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
    for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
      g_mock_handlers[base + i] = {nullptr, read_c0, write_c0};
    }
  }
}

auto Mock_RegisterCxROM(int slot, uint8_t* rom_ptr) -> void {
  (void)slot;
  (void)rom_ptr;
}

auto Mock_RegisterExpansionROM(int slot, uint8_t* rom_ptr) -> void {
  (void)slot;
  (void)rom_ptr;
}

auto Mock_RegisterDirectIO(void* instance, uint16_t addr,
                           PeripheralIOHandler read, PeripheralIOHandler write)
    -> void {
  g_mock_handlers[addr] = {instance, read, write};
}

auto Mock_SerialTransmitByte(void* instance, uint8_t byte) -> void {
  (void)instance;
  g_sent_bytes.push_back(byte);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static HostInterface_t mock_host = {
    .Log = Mock_Log,
    .AssertIrq = Mock_AssertIrq,
    .RegisterIO = Mock_RegisterIO,
    .RegisterCxROM = Mock_RegisterCxROM,
    .RegisterExpansionROM = Mock_RegisterExpansionROM,
    .RegisterDirectIO = Mock_RegisterDirectIO,
    .get_mem_ptr = nullptr,
    .GetCycles = nullptr,
    .GetConfig = nullptr,
    .SetConfig = nullptr,
    .NotifyStatusChanged = nullptr,
    .NotifyActivityChanged = nullptr,
    .RequestPreciseTiming = nullptr,
    .AudioPushSamples = nullptr,
    .ResetSystem = nullptr,
    .PrinterPutChar = nullptr,
    .PrinterGetStatus = nullptr,
    .SerialTransmitByte = Mock_SerialTransmitByte,
    .SerialUpdateState = nullptr};

static auto SuperSerial_Init_With_Mock(int slot) -> void* {
  void* instance = super_serial_get_descriptor()->init(slot, &mock_host);
  const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
  for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
    if (g_mock_handlers.count(base + i) > 0) {
      g_mock_handlers.at(base + i).instance = instance;
    }
  }
  return instance;
}

TEST_CASE("SuperSerial: Status Register Bit 4 (TDRE) Set On Reset") {
  g_mock_handlers.clear();
  void* instance = SuperSerial_Init_With_Mock(TEST_SLOT);
  REQUIRE(instance != nullptr);

  super_serial_get_descriptor()->reset(instance);

  uint8_t status =
      g_mock_handlers.at(ADDR_STATUS).read(instance, 0, ADDR_STATUS, 0, 0, 0);
  CHECK((status & STATUS_TDRE_MASK) != 0);

  super_serial_get_descriptor()->shutdown(instance);
}

TEST_CASE("SuperSerial: Transmit and Interrupt Behavior") {
  g_mock_handlers.clear();
  g_sent_bytes.clear();
  g_irq_asserted = false;
  void* instance = SuperSerial_Init_With_Mock(TEST_SLOT);

  g_mock_handlers.at(ADDR_COMMAND)
      .write(instance, 0, ADDR_COMMAND, 1, CMD_ENABLE_TX_IRQ, 0);

  g_mock_handlers.at(ADDR_DATA).write(instance, 0, ADDR_DATA, 1, TEST_BYTE_A,
                                      0);

  REQUIRE(g_sent_bytes.size() == 1);
  CHECK(g_sent_bytes.at(0) == TEST_BYTE_A);

  super_serial_get_descriptor()->shutdown(instance);
}

TEST_CASE("SuperSerial: Receive Buffer and RX IRQ") {
  g_mock_handlers.clear();
  g_irq_asserted = false;
  void* instance = SuperSerial_Init_With_Mock(TEST_SLOT);

  g_mock_handlers.at(ADDR_COMMAND)
      .write(instance, 0, ADDR_COMMAND, 1, CMD_ENABLE_RX_IRQ, 0);

  uint8_t rx_byte = TEST_BYTE_VAL;
  super_serial_get_descriptor()->command(instance, SUPER_SERIAL_CMD_PUSH_RX_BYTE,
                                       &rx_byte, sizeof(uint8_t));

  CHECK(g_irq_asserted == true);

  uint8_t status =
      g_mock_handlers.at(ADDR_STATUS).read(instance, 0, ADDR_STATUS, 0, 0, 0);
  CHECK((status & STATUS_RX_FULL_MASK) != 0);

  uint8_t read_byte =
      g_mock_handlers.at(ADDR_DATA).read(instance, 0, ADDR_DATA, 0, 0, 0);
  CHECK(read_byte == TEST_BYTE_VAL);

  status =
      g_mock_handlers.at(ADDR_STATUS).read(instance, 0, ADDR_STATUS, 0, 0, 0);
  CHECK((status & STATUS_RX_FULL_MASK) == 0);

  super_serial_get_descriptor()->shutdown(instance);
}

TEST_CASE("SuperSerial: Robustness and ABI") {
  g_mock_handlers.clear();

  CHECK(super_serial_get_descriptor()->init(TEST_SLOT, nullptr) == nullptr);

  void* instance = SuperSerial_Init_With_Mock(TEST_SLOT);

  SuperSerialDipSwConfig_t cfg = {.baud_rate = SUPER_SERIAL_BAUD_9600,
                                  .firmware_mode = SUPER_SERIAL_FIRMWARE_CIC,
                                  .stop_bits = SUPER_SERIAL_STOP_BITS_1,
                                  .byte_size = SUPER_SERIAL_BITS_8,
                                  .parity = SUPER_SERIAL_PARITY_NONE,
                                  .linefeed = false,
                                  .interrupts = false};
  CHECK(super_serial_get_descriptor()->command(instance,
                                             SUPER_SERIAL_CMD_SET_CONFIG, &cfg,
                                             sizeof(cfg)) == peripheral_ok);

  SuperSerialDipSwConfig_t queried = {
      .baud_rate = SUPER_SERIAL_BAUD_110,
      .firmware_mode = SUPER_SERIAL_FIRMWARE_CIC,
      .stop_bits = SUPER_SERIAL_STOP_BITS_1,
      .byte_size = SUPER_SERIAL_BITS_8,
      .parity = SUPER_SERIAL_PARITY_NONE,
      .linefeed = false,
      .interrupts = false};
  size_t size = sizeof(queried);
  CHECK(super_serial_get_descriptor()->query(instance, SUPER_SERIAL_QUERY_CONFIG,
                                           &queried, &size) == peripheral_ok);
  CHECK(queried.baud_rate == SUPER_SERIAL_BAUD_9600);

  super_serial_get_descriptor()->shutdown(instance);
}

}  // namespace
