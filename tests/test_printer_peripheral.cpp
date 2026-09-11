// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "apple2/peripherals/printer/Printer.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

namespace {

constexpr int TEST_SLOT_1 = 1;
constexpr int TEST_SLOT_2 = 2;

constexpr uint8_t STATUS_READY = 0x7F;
constexpr uint8_t STATUS_BUSY = 0x80;
constexpr uint8_t STATUS_OFFLINE = 0xFF;
constexpr uint8_t STATUS_PAPER_OUT = 0x20;
constexpr uint8_t TRANSMIT_SUCCESS = 0;

constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;
constexpr int REGISTERS_PER_SLOT = 16;
constexpr size_t SLOT_ROM_PAGE_SIZE = 256;

constexpr uint8_t ROM_FIRST_BYTE = 0x18;
constexpr uint8_t ROM_LAST_BYTE = 0x84;

constexpr uint8_t TEST_CHAR_A = 'A';

struct MockHandler_t {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockHandler_t() = default;
  MockHandler_t(void* inst, PeripheralIOHandler r, PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

class PrinterHarness {
 public:
  PrinterHarness() {
    s_active_harness = this;
    host_.Log = Mock_Log;
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.RegisterCxROM = Mock_RegisterCxROM;
    host_.RegisterExpansionROM = Mock_RegisterExpansionROM;
    host_.RegisterDirectIO = Mock_RegisterDirectIO;
    host_.PrinterPutChar = Mock_PrinterPutChar;
    host_.PrinterGetStatus = Mock_PrinterGetStatus;
  }

  ~PrinterHarness() {
    for (const auto& entry : instances_) {
      if (entry.second != nullptr) {
        printer_get_descriptor()->shutdown(entry.second);
      }
    }
    instances_.clear();
    slot_by_instance_.clear();
    s_active_harness = nullptr;
  }

  PrinterHarness(const PrinterHarness&) = delete;
  auto operator=(const PrinterHarness&) -> PrinterHarness& = delete;
  PrinterHarness(PrinterHarness&&) = delete;
  auto operator=(PrinterHarness&&) -> PrinterHarness& = delete;

  auto host() -> HostInterface_t* { return &host_; }

  auto create_printer(int slot) -> void* {
    void* instance = printer_get_descriptor()->init(slot, &host_);
    if (instance != nullptr) {
      instances_[slot] = instance;
      slot_by_instance_[instance] = slot;
      const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
      for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
        auto it = handlers_.find(base + i);
        if (it != handlers_.end()) {
          it->second.instance = instance;
        }
      }
    }
    return instance;
  }

  auto shutdown_instance(int slot) -> void {
    auto it = instances_.find(slot);
    if (it != instances_.end()) {
      if (it->second != nullptr) {
        slot_by_instance_.erase(it->second);
        printer_get_descriptor()->shutdown(it->second);
      }
      instances_.erase(it);
      const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
      for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
        handlers_.erase(base + i);
      }
    }
  }

  auto get_instance(int slot) const -> void* {
    auto it = instances_.find(slot);
    return (it != instances_.end()) ? it->second : nullptr;
  }

  auto set_status(int slot, uint8_t status) -> void {
    printer_status_[slot] = status;
  }

  auto set_status_callback_enabled(bool enabled) -> void {
    host_.PrinterGetStatus = enabled ? Mock_PrinterGetStatus : nullptr;
  }

  auto set_putchar_callback_enabled(bool enabled) -> void {
    host_.PrinterPutChar = enabled ? Mock_PrinterPutChar : nullptr;
  }

  auto printed_chars(int slot) const -> const std::vector<uint8_t>& {
    static const std::vector<uint8_t> empty_vector{};
    auto it = printed_chars_.find(slot);
    return (it != printed_chars_.end()) ? it->second : empty_vector;
  }

  auto clear_printed_chars(int slot) -> void {
    auto it = printed_chars_.find(slot);
    if (it != printed_chars_.end()) {
      it->second.clear();
    }
  }

  auto read_io(uint16_t addr, uint8_t is_write = 0, uint8_t val = 0)
      -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      return it->second.read(it->second.instance, 0, addr, is_write, val, 0);
    }
    return STATUS_OFFLINE;
  }

  auto write_io(uint16_t addr, uint8_t val, uint8_t is_write = 1) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      return it->second.write(it->second.instance, 0, addr, is_write, val, 0);
    }
    return 0;
  }

  auto read_slot_reg(int slot, uint8_t offset, uint8_t is_write = 0,
                     uint8_t val = 0) -> uint8_t {
    const uint16_t addr = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET) + offset;
    return read_io(addr, is_write, val);
  }

  auto write_slot_reg(int slot, uint8_t offset, uint8_t val,
                      uint8_t is_write = 1) -> uint8_t {
    const uint16_t addr = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET) + offset;
    return write_io(addr, val, is_write);
  }

  auto has_handler(uint16_t addr) const -> bool {
    return handlers_.find(addr) != handlers_.end();
  }

  auto get_handler(uint16_t addr) const -> const MockHandler_t& {
    return handlers_.at(addr);
  }

  auto has_rom(int slot) const -> bool {
    return roms_.find(slot) != roms_.end();
  }

  auto get_rom(int slot) const -> const std::vector<uint8_t>& {
    return roms_.at(slot);
  }

 private:
  HostInterface_t host_{};
  std::map<uint16_t, MockHandler_t> handlers_;
  std::map<int, std::vector<uint8_t>> roms_;
  std::map<int, void*> instances_;
  std::map<void*, int> slot_by_instance_;
  std::map<int, std::vector<uint8_t>> printed_chars_;
  std::map<int, uint8_t> printer_status_;
  uint8_t default_status_ = STATUS_READY;

  static PrinterHarness* s_active_harness;

  static auto Mock_Log(void* instance, PeripheralLogLevel_t level,
                       const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    (void)fmt;
  }

  static auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
    (void)slot;
    (void)assert_irq;
  }

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  // Justification: Signature is required by HostInterface_t ABI.
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
    if (s_active_harness != nullptr && rom_ptr != nullptr) {
      std::vector<uint8_t> rom_data(SLOT_ROM_PAGE_SIZE);
      std::copy_n(rom_ptr, SLOT_ROM_PAGE_SIZE, rom_data.begin());
      s_active_harness->roms_[slot] = std::move(rom_data);
    }
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

  static auto Mock_PrinterPutChar(void* instance, uint8_t c) -> void {
    if (s_active_harness != nullptr) {
      auto it = s_active_harness->slot_by_instance_.find(instance);
      const int slot =
          (it != s_active_harness->slot_by_instance_.end()) ? it->second : 0;
      s_active_harness->printed_chars_[slot].push_back(c);
    }
  }

  static auto Mock_PrinterGetStatus(void* instance) -> uint8_t {
    if (s_active_harness != nullptr) {
      auto it = s_active_harness->slot_by_instance_.find(instance);
      if (it != s_active_harness->slot_by_instance_.end()) {
        auto st = s_active_harness->printer_status_.find(it->second);
        if (st != s_active_harness->printer_status_.end()) {
          return st->second;
        }
      }
      return s_active_harness->default_status_;
    }
    return STATUS_OFFLINE;
  }
  // NOLINTEND(bugprone-easily-swappable-parameters)
};

PrinterHarness* PrinterHarness::s_active_harness = nullptr;

TEST_CASE("Printer Peripheral: Descriptor Metadata and Compatibility") {
  const auto* descriptor = printer_get_descriptor();
  REQUIRE(descriptor != nullptr);

  CHECK(descriptor->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(descriptor->id) == "linapple.printer");
  CHECK(std::string(descriptor->name) == "Parallel Printer");
  CHECK(std::string(descriptor->description) ==
        "Standard parallel printer interface emulation");
  CHECK(std::string(descriptor->author) == "LinApple Contributors");
  REQUIRE(descriptor->version != nullptr);
  CHECK(std::string(descriptor->version).empty() == false);
  CHECK(descriptor->compatible_slots == PERIPHERAL_MASK_EXPANSION);
  CHECK(descriptor->default_slot == 1);

  CHECK(descriptor->init != nullptr);
  CHECK(descriptor->reset != nullptr);
  CHECK(descriptor->shutdown != nullptr);
  CHECK(descriptor->think != nullptr);
  CHECK(descriptor->on_vblank == nullptr);
  CHECK(descriptor->save_state == nullptr);
  CHECK(descriptor->load_state == nullptr);
  CHECK(descriptor->command == nullptr);
  CHECK(descriptor->query == nullptr);
}

TEST_CASE("Printer Peripheral: Registration and Firmware") {
  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  const uint16_t base = IO_BASE_ADDRESS + (TEST_SLOT_1 << IO_SLOT_OFFSET);
  for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
    const uint16_t addr = base + i;
    CHECK(harness.has_handler(addr));
    CHECK(harness.get_handler(addr).read != nullptr);
    CHECK(harness.get_handler(addr).write != nullptr);
  }

#if ENABLE_ROM_PRINTER
  REQUIRE(harness.has_rom(TEST_SLOT_1));
  const auto& rom = harness.get_rom(TEST_SLOT_1);
  CHECK(rom.size() == SLOT_ROM_PAGE_SIZE);
  CHECK(rom.at(0) == ROM_FIRST_BYTE);
  CHECK(rom.at(SLOT_ROM_PAGE_SIZE - 1) == ROM_LAST_BYTE);
#endif
}

TEST_CASE("Printer Peripheral: I/O Mirroring Across Slot Registers") {
  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  for (uint8_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
    const uint8_t char_to_write = static_cast<uint8_t>(TEST_CHAR_A + i);
    const uint8_t write_ret =
        harness.write_slot_reg(TEST_SLOT_1, i, char_to_write);
    CHECK(write_ret == TRANSMIT_SUCCESS);

    const auto& captured = harness.printed_chars(TEST_SLOT_1);
    REQUIRE(captured.size() == static_cast<size_t>(i + 1));
    CHECK(captured.back() == char_to_write);

    const uint8_t mock_status = static_cast<uint8_t>(i | 0x40);
    harness.set_status(TEST_SLOT_1, mock_status);
    const uint8_t read_val = harness.read_slot_reg(TEST_SLOT_1, i);
    CHECK(read_val == mock_status);
  }
}

TEST_CASE("Printer Peripheral: Character Transmission") {
  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  const std::vector<uint8_t> payload = {
      'H', 'e', 'l', 'l', 'o',  ',',  ' ',  'A',  'p',  'p',  'l', 'e',
      ' ', 'I', 'I', '!', '\r', '\n', 0x00, 0x1B, 0x7F, 0x80, 0xFF};

  for (size_t i = 0; i < payload.size(); ++i) {
    const uint8_t reg_offset = static_cast<uint8_t>(i % REGISTERS_PER_SLOT);
    const uint8_t write_ret =
        harness.write_slot_reg(TEST_SLOT_1, reg_offset, payload[i]);
    CHECK(write_ret == TRANSMIT_SUCCESS);
  }

  const auto& captured = harness.printed_chars(TEST_SLOT_1);
  REQUIRE(captured.size() == payload.size());
  for (size_t i = 0; i < payload.size(); ++i) {
    CHECK(captured[i] == payload[i]);
  }

  harness.clear_printed_chars(TEST_SLOT_1);
  CHECK(harness.printed_chars(TEST_SLOT_1).empty());
}

TEST_CASE(
    "Printer Peripheral: Status Reporting (Ready, Busy, Offline, Paper Out)") {
  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  harness.set_status(TEST_SLOT_1, STATUS_READY);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_READY);

  harness.set_status(TEST_SLOT_1, STATUS_BUSY);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_BUSY);

  harness.set_status(TEST_SLOT_1, STATUS_OFFLINE);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_OFFLINE);

  harness.set_status(TEST_SLOT_1, STATUS_PAPER_OUT);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_PAPER_OUT);

  const std::vector<uint8_t> test_statuses = {0x00, 0x01, 0x55, 0xAA, 0xFE};
  for (uint8_t st : test_statuses) {
    harness.set_status(TEST_SLOT_1, st);
    CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == st);
  }
}

TEST_CASE("Printer Peripheral: Multi-Slot Independence") {
  PrinterHarness harness;
  void* instance1 = harness.create_printer(TEST_SLOT_1);
  void* instance2 = harness.create_printer(TEST_SLOT_2);

  REQUIRE(instance1 != nullptr);
  REQUIRE(instance2 != nullptr);
  CHECK(instance1 != instance2);

  const uint16_t base1 = IO_BASE_ADDRESS + (TEST_SLOT_1 << IO_SLOT_OFFSET);
  const uint16_t base2 = IO_BASE_ADDRESS + (TEST_SLOT_2 << IO_SLOT_OFFSET);

  CHECK(harness.get_handler(base1).instance == instance1);
  CHECK(harness.get_handler(base2).instance == instance2);

  harness.set_status(TEST_SLOT_1, STATUS_READY);
  harness.set_status(TEST_SLOT_2, STATUS_BUSY);

  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_READY);
  CHECK(harness.read_slot_reg(TEST_SLOT_2, 0) == STATUS_BUSY);

  const std::vector<uint8_t> msg1 = {'S', 'L', 'O', 'T', '1'};
  const std::vector<uint8_t> msg2 = {'S', 'L', 'O', 'T', '2'};

  for (uint8_t c : msg1) {
    harness.write_slot_reg(TEST_SLOT_1, 0, c);
  }
  for (uint8_t c : msg2) {
    harness.write_slot_reg(TEST_SLOT_2, 0, c);
  }

  CHECK(harness.printed_chars(TEST_SLOT_1) == msg1);
  CHECK(harness.printed_chars(TEST_SLOT_2) == msg2);

  // Shut down slot 1; verify slot 2 remains functional
  harness.shutdown_instance(TEST_SLOT_1);
  CHECK(harness.get_instance(TEST_SLOT_1) == nullptr);

  harness.write_slot_reg(TEST_SLOT_2, 0, '!');
  const std::vector<uint8_t> expected_slot2 = {'S', 'L', 'O', 'T', '2', '!'};
  CHECK(harness.printed_chars(TEST_SLOT_2) == expected_slot2);
  CHECK(harness.read_slot_reg(TEST_SLOT_2, 0) == STATUS_BUSY);
}

TEST_CASE("Printer Peripheral: Robustness and Seam Error Handling") {
  auto* descriptor = printer_get_descriptor();
  REQUIRE(descriptor != nullptr);

  // Init with nullptr host must return nullptr
  CHECK(descriptor->init(TEST_SLOT_1, nullptr) == nullptr);

  // Reset, think, and shutdown with nullptr instance must not crash
  descriptor->reset(nullptr);
  descriptor->think(nullptr, 1024);
  descriptor->shutdown(nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  // Reset and think with valid instance must not crash
  descriptor->reset(instance);
  descriptor->think(instance, 1024);

  const uint16_t base = IO_BASE_ADDRESS + (TEST_SLOT_1 << IO_SLOT_OFFSET);
  const auto& handler = harness.get_handler(base);

  // Read handler: when is_write != 0, must return offline status (0xFF)
  CHECK(handler.read(instance, 0, base, 1, 0, 0) == STATUS_OFFLINE);

  // Read handler: when instance == nullptr, must return offline status (0xFF)
  CHECK(handler.read(nullptr, 0, base, 0, 0, 0) == STATUS_OFFLINE);

  // Write handler: when is_write == 0, must return success (0) and not transmit
  harness.clear_printed_chars(TEST_SLOT_1);
  CHECK(handler.write(instance, 0, base, 0, TEST_CHAR_A, 0) ==
        TRANSMIT_SUCCESS);
  CHECK(harness.printed_chars(TEST_SLOT_1).empty());

  // Write handler: when instance == nullptr, must return success (0) and not
  // crash
  CHECK(handler.write(nullptr, 0, base, 1, TEST_CHAR_A, 0) == TRANSMIT_SUCCESS);

  // Host interface with null PrinterGetStatus callback: MMIO read must return
  // offline status
  harness.set_status_callback_enabled(false);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_OFFLINE);
  harness.set_status_callback_enabled(true);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_READY);

  // Host interface with null PrinterPutChar callback: MMIO write must return
  // success and not crash
  harness.set_putchar_callback_enabled(false);
  harness.clear_printed_chars(TEST_SLOT_1);
  CHECK(harness.write_slot_reg(TEST_SLOT_1, 0, TEST_CHAR_A) ==
        TRANSMIT_SUCCESS);
  CHECK(harness.printed_chars(TEST_SLOT_1).empty());
  harness.set_putchar_callback_enabled(true);
  CHECK(harness.write_slot_reg(TEST_SLOT_1, 0, TEST_CHAR_A) ==
        TRANSMIT_SUCCESS);
  const auto& captured = harness.printed_chars(TEST_SLOT_1);
  REQUIRE(captured.size() == 1);
  CHECK(captured.front() == TEST_CHAR_A);
}

}  // namespace
