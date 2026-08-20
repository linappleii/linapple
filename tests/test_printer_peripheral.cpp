// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <map>
#include <vector>

#include "apple2/peripherals/printer/Printer.h"
#include "core/Peripheral.h"
#include "doctest.h"

namespace {

constexpr int TEST_SLOT_1 = 1;
constexpr int TEST_SLOT_2 = 2;

constexpr uint8_t STATUS_READY = 0x7F;
constexpr uint8_t TEST_CHAR_A = 'A';

constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;
constexpr int REGISTERS_PER_SLOT = 16;
constexpr size_t SLOT_ROM_PAGE_SIZE = 256;

constexpr uint8_t ROM_FIRST_BYTE = 0x18;
constexpr uint8_t ROM_LAST_BYTE = 0x84;

struct MockHandler {
  void* instance;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
};

static std::map<uint16_t, MockHandler> g_mock_handlers;
static std::vector<uint8_t> g_printed_chars_1;
static std::map<int, std::vector<uint8_t>> g_mock_roms;
static uint8_t g_mock_status = STATUS_READY;

auto Mock_Log(void* instance, PeripheralLogLevel_t level, const char* fmt, ...)
    -> void {
  (void)instance;
  (void)level;
  (void)fmt;
}

auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
  (void)slot;
  (void)assert_irq;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
// Justification: Signature is required by HostInterface_t ABI.
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
  if (rom_ptr != nullptr) {
    std::vector<uint8_t> rom_data(SLOT_ROM_PAGE_SIZE);
    std::copy_n(rom_ptr, SLOT_ROM_PAGE_SIZE, rom_data.begin());
    g_mock_roms[slot] = rom_data;
  }
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

auto Mock_PrinterPutChar(void* instance, uint8_t c) -> void {
  (void)instance;
  g_printed_chars_1.push_back(c);
}

auto Mock_PrinterGetStatus(void* instance) -> uint8_t {
  (void)instance;
  return g_mock_status;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static HostInterface_t mock_host = [] {
  HostInterface_t h{};
  h.Log = Mock_Log;
  h.AssertIrq = Mock_AssertIrq;
  h.RegisterIO = Mock_RegisterIO;
  h.RegisterCxROM = Mock_RegisterCxROM;
  h.RegisterExpansionROM = Mock_RegisterExpansionROM;
  h.RegisterDirectIO = Mock_RegisterDirectIO;
  h.get_mem_ptr = nullptr;
  h.GetCycles = nullptr;
  h.GetConfig = nullptr;
  h.SetConfig = nullptr;
  h.NotifyStatusChanged = nullptr;
  h.NotifyActivityChanged = nullptr;
  h.RequestPreciseTiming = nullptr;
  h.AudioPushSamples = nullptr;
  h.ResetSystem = nullptr;
  h.PrinterPutChar = Mock_PrinterPutChar;
  h.PrinterGetStatus = Mock_PrinterGetStatus;
  h.SerialTransmitByte = nullptr;
  h.SerialUpdateState = nullptr;
  return h;
}();

auto Printer_Init_With_Mock(int slot) -> void* {
  void* instance = printer_get_descriptor()->init(slot, &mock_host);
  const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
  for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
    if (g_mock_handlers.count(base + i) > 0) {
      g_mock_handlers.at(base + i).instance = instance;
    }
  }
  return instance;
}

TEST_CASE("Printer Peripheral: Registration and Firmware") {
  g_mock_handlers.clear();
  g_mock_roms.clear();
  void* instance = Printer_Init_With_Mock(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  const uint16_t base = IO_BASE_ADDRESS + (TEST_SLOT_1 << IO_SLOT_OFFSET);
  CHECK(g_mock_handlers.count(base) > 0);
  CHECK(g_mock_handlers.at(base).read != nullptr);
  CHECK(g_mock_handlers.at(base).write != nullptr);

  REQUIRE(g_mock_roms.count(TEST_SLOT_1) > 0);
  const auto& rom = g_mock_roms.at(TEST_SLOT_1);
  CHECK(rom.size() == SLOT_ROM_PAGE_SIZE);
  CHECK(rom.at(0) == ROM_FIRST_BYTE);
  CHECK(rom.at(SLOT_ROM_PAGE_SIZE - 1) == ROM_LAST_BYTE);

  printer_get_descriptor()->shutdown(instance);
}

TEST_CASE("Printer Peripheral: I/O Mirroring") {
  g_mock_handlers.clear();
  g_printed_chars_1.clear();
  void* instance = Printer_Init_With_Mock(TEST_SLOT_1);
  const uint16_t base = IO_BASE_ADDRESS + (TEST_SLOT_1 << IO_SLOT_OFFSET);

  for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
    const uint16_t addr = base + i;
    g_mock_handlers.at(addr).write(instance, 0, addr, 1, TEST_CHAR_A, 0);
    CHECK(g_printed_chars_1.back() == TEST_CHAR_A);

    g_mock_status = static_cast<uint8_t>(i);
    CHECK(g_mock_handlers.at(addr).read(instance, 0, addr, 0, 0, 0) == i);
  }

  printer_get_descriptor()->shutdown(instance);
}

TEST_CASE("Printer Peripheral: Multi-Slot Independence") {
  g_mock_handlers.clear();
  void* instance1 = Printer_Init_With_Mock(TEST_SLOT_1);
  void* instance2 = Printer_Init_With_Mock(TEST_SLOT_2);

  const uint16_t base1 = IO_BASE_ADDRESS + (TEST_SLOT_1 << IO_SLOT_OFFSET);
  const uint16_t base2 = IO_BASE_ADDRESS + (TEST_SLOT_2 << IO_SLOT_OFFSET);

  CHECK(g_mock_handlers.at(base1).instance == instance1);
  CHECK(g_mock_handlers.at(base2).instance == instance2);
  CHECK(instance1 != instance2);

  printer_get_descriptor()->shutdown(instance1);
  printer_get_descriptor()->shutdown(instance2);
}

TEST_CASE("Printer Peripheral: Robustness") {
  g_mock_handlers.clear();

  CHECK(printer_get_descriptor()->init(TEST_SLOT_1, nullptr) == nullptr);

  void* instance = Printer_Init_With_Mock(TEST_SLOT_1);
  const uint16_t base = IO_BASE_ADDRESS + (TEST_SLOT_1 << IO_SLOT_OFFSET);

  CHECK(g_mock_handlers.at(base).write(instance, 0, base, 0, TEST_CHAR_A, 0) ==
        0);

  CHECK(g_mock_handlers.at(base).read(instance, 0, base, 1, 0, 0) == 0xFF);

  printer_get_descriptor()->shutdown(instance);
}

}  // namespace
