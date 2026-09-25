// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "doctest.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

// The card is reached the way the emulator reaches it, through the registry,
// so one test binary covers the built-in card and the loaded plugin alike.
auto printer_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.printer");
}

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
    REQUIRE(printer_descriptor() != nullptr);
    host_.Log = Mock_Log;
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.RegisterCxROM = Mock_RegisterCxROM;
    host_.RegisterExpansionROM = Mock_RegisterExpansionROM;
    host_.RegisterDirectIO = Mock_RegisterDirectIO;
    host_.PrinterPutChar = Mock_PrinterPutChar;
    host_.PrinterGetStatus = Mock_PrinterGetStatus;
    host_.NotifyActivityChanged = Mock_NotifyActivityChanged;
  }

  ~PrinterHarness() {
    for (const auto& entry : instances_) {
      if (entry.second != nullptr) {
        printer_descriptor()->shutdown(entry.second);
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
    void* instance = printer_descriptor()->init(slot, &host_);
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
        printer_descriptor()->shutdown(it->second);
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

  auto set_notify_activity_callback_enabled(bool enabled) -> void {
    host_.NotifyActivityChanged =
        enabled ? Mock_NotifyActivityChanged : nullptr;
  }

  auto activity_state(int slot) const -> bool {
    auto it = activity_states_.find(slot);
    return (it != activity_states_.end()) ? it->second : false;
  }

  auto activity_changes_count(int slot) const -> size_t {
    auto it = activity_counts_.find(slot);
    return (it != activity_counts_.end()) ? it->second : 0;
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
  std::map<int, bool> activity_states_;
  std::map<int, size_t> activity_counts_;
  uint8_t default_status_ = STATUS_READY;

  static PrinterHarness* s_active_harness;

  static auto Mock_NotifyActivityChanged(int slot, bool active) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->activity_states_[slot] = active;
      s_active_harness->activity_counts_[slot]++;
    }
  }

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

  static auto Mock_RegisterCxROM(int slot, const uint8_t* rom_ptr) -> void {
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
  const auto* descriptor = printer_descriptor();
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
  CHECK(descriptor->save_state != nullptr);
  CHECK(descriptor->load_state != nullptr);
  CHECK(descriptor->command != nullptr);
  CHECK(descriptor->query != nullptr);
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
  auto* descriptor = printer_descriptor();
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

  // Read handler: when is_write != 0, must return floating bus noise
  CHECK(handler.read(instance, 0, base, 1, 0, 0) == mem_read_floating_bus(0));

  // Read handler: when instance == nullptr, must return floating bus noise
  CHECK(handler.read(nullptr, 0, base, 0, 0, 0) == mem_read_floating_bus(0));

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

TEST_CASE("Printer Peripheral: Save and Load State Lifecycle") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);
  REQUIRE(descriptor->save_state != nullptr);
  REQUIRE(descriptor->load_state != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  // Sizing probe with null state buffer
  size_t required_size = 0;
  CHECK(descriptor->save_state(instance, nullptr, &required_size) ==
        peripheral_ok);
  CHECK(required_size == sizeof(SsCardPrinter_t));

  // Null buffer_size pointer must fail
  CHECK(descriptor->save_state(instance, nullptr, nullptr) == peripheral_error);

  // Undersized buffer must fail
  std::vector<uint8_t> undersized(required_size - 1, 0);
  size_t too_small = undersized.size();
  CHECK(descriptor->save_state(instance, undersized.data(), &too_small) ==
        peripheral_error);

  // Save state with valid buffer
  std::vector<uint8_t> save_buf(required_size, 0);
  size_t actual_size = save_buf.size();
  CHECK(descriptor->save_state(instance, save_buf.data(), &actual_size) ==
        peripheral_ok);
  CHECK(actual_size == sizeof(SsCardPrinter_t));

  const auto* state_header =
      reinterpret_cast<const SsCardPrinter_t*>(save_buf.data());
  CHECK(state_header->version == PRINTER_STATE_VERSION);
  CHECK(state_header->struct_size == sizeof(SsCardPrinter_t));
  CHECK(state_header->is_online == 1);
  CHECK(state_header->is_busy == 0);

  // Corrupted version or size in load_state must fail
  std::vector<uint8_t> corrupt_buf = save_buf;
  auto* corrupt_header = reinterpret_cast<SsCardPrinter_t*>(corrupt_buf.data());
  corrupt_header->version = 999;
  CHECK(descriptor->load_state(instance, corrupt_buf.data(),
                               corrupt_buf.size()) == peripheral_error);

  corrupt_buf = save_buf;
  corrupt_header = reinterpret_cast<SsCardPrinter_t*>(corrupt_buf.data());
  corrupt_header->struct_size = 12;
  CHECK(descriptor->load_state(instance, corrupt_buf.data(),
                               corrupt_buf.size()) == peripheral_error);

  // Wrong buffer size on load must fail
  CHECK(descriptor->load_state(instance, save_buf.data(), required_size - 1) ==
        peripheral_error);

  // Null instance on save/load must fail
  CHECK(descriptor->save_state(nullptr, save_buf.data(), &actual_size) ==
        peripheral_error);
  CHECK(descriptor->load_state(nullptr, save_buf.data(), required_size) ==
        peripheral_error);
  CHECK(descriptor->load_state(instance, nullptr, required_size) ==
        peripheral_error);

  // Successful round-trip restoration
  CHECK(descriptor->load_state(instance, save_buf.data(), required_size) ==
        peripheral_ok);
}

TEST_CASE("Printer Peripheral: Command and Query ABI Protocol") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);
  REQUIRE(descriptor->command != nullptr);
  REQUIRE(descriptor->query != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  // Null instance on command must fail
  PrinterOnlineCmd_t online_cmd{0, {0, 0, 0}};
  CHECK(descriptor->command(nullptr, PRINTER_CMD_SET_ONLINE, &online_cmd,
                            sizeof(online_cmd)) == peripheral_error);

  // Undersized payload on command must fail
  CHECK(descriptor->command(instance, PRINTER_CMD_SET_ONLINE, &online_cmd,
                            sizeof(uint8_t) - 1) == peripheral_error);
  CHECK(descriptor->command(instance, PRINTER_CMD_SET_ONLINE, nullptr, 0) ==
        peripheral_error);

  // Unknown command must return peripheral_incompatible
  CHECK(descriptor->command(instance, 0x9999, nullptr, 0) ==
        peripheral_incompatible);

  // Toggle online/offline via command
  online_cmd.online = 0;
  CHECK(descriptor->command(instance, PRINTER_CMD_SET_ONLINE, &online_cmd,
                            sizeof(online_cmd)) == peripheral_ok);

  // Query: null output_size pointer must fail
  CHECK(descriptor->query(instance, PRINTER_QUERY_STATUS, nullptr, nullptr) ==
        peripheral_error);

  // Query: sizing probe with null output
  size_t query_size = 0;
  CHECK(descriptor->query(instance, PRINTER_QUERY_STATUS, nullptr,
                          &query_size) == peripheral_ok);
  CHECK(query_size == sizeof(PrinterStatusQuery_t));

  // Query: undersized buffer must fail
  std::vector<uint8_t> undersized(query_size - 1, 0);
  size_t small_size = undersized.size();
  CHECK(descriptor->query(instance, PRINTER_QUERY_STATUS, undersized.data(),
                          &small_size) == peripheral_error);
  CHECK(small_size == sizeof(PrinterStatusQuery_t));

  // Query: null instance with valid buffer must fail
  PrinterStatusQuery_t status_out{};
  size_t valid_size = sizeof(status_out);
  CHECK(descriptor->query(nullptr, PRINTER_QUERY_STATUS, &status_out,
                          &valid_size) == peripheral_error);

  // Query: successful query verifying offline status
  CHECK(descriptor->query(instance, PRINTER_QUERY_STATUS, &status_out,
                          &valid_size) == peripheral_ok);
  CHECK(valid_size == sizeof(PrinterStatusQuery_t));
  CHECK(status_out.is_online == 0);
  CHECK(status_out.is_busy == 0);

  // Toggle back online
  online_cmd.online = 1;
  CHECK(descriptor->command(instance, PRINTER_CMD_SET_ONLINE, &online_cmd,
                            sizeof(online_cmd)) == peripheral_ok);
  CHECK(descriptor->query(instance, PRINTER_QUERY_STATUS, &status_out,
                          &valid_size) == peripheral_ok);
  CHECK(status_out.is_online == 1);

  // Command: reset stats
  CHECK(descriptor->command(instance, PRINTER_CMD_RESET_STATS, nullptr, 0) ==
        peripheral_ok);
  CHECK(descriptor->query(instance, PRINTER_QUERY_STATUS, &status_out,
                          &valid_size) == peripheral_ok);
  CHECK(status_out.total_chars_printed == 0);

  // Unknown query ID must return peripheral_incompatible
  CHECK(descriptor->query(instance, 0x9999, &status_out, &valid_size) ==
        peripheral_incompatible);
}

TEST_CASE("Printer Peripheral: Activity Notification and Pulse Decay") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  CHECK(harness.activity_state(TEST_SLOT_1) == false);

  // Transmit character: triggers active notification
  harness.write_slot_reg(TEST_SLOT_1, 0, 'A');
  CHECK(harness.activity_state(TEST_SLOT_1) == true);
  CHECK(harness.activity_changes_count(TEST_SLOT_1) == 1);

  // Step 9 cycles: activity remains asserted
  for (int i = 0; i < 9; ++i) {
    descriptor->think(instance, 1000);
    CHECK(harness.activity_state(TEST_SLOT_1) == true);
  }

  // 10th step: activity pulse decays and notifies inactive
  descriptor->think(instance, 1000);
  CHECK(harness.activity_state(TEST_SLOT_1) == false);
  CHECK(harness.activity_changes_count(TEST_SLOT_1) == 2);
}

TEST_CASE("Printer Peripheral: Hardware Busy Delay and Cycle Stepping") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  // Inject initial busy_cycles cleanly via load_state
  SsCardPrinter_t state{};
  state.version = PRINTER_STATE_VERSION;
  state.struct_size = sizeof(SsCardPrinter_t);
  state.is_online = 1;
  state.busy_cycles = 5000;
  CHECK(descriptor->load_state(instance, &state, sizeof(state)) ==
        peripheral_ok);

  // Transmit character: triggers busy flag
  harness.write_slot_reg(TEST_SLOT_1, 0, 'B');
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_BUSY);

  // Step 2500 cycles: still busy (2500 remaining)
  descriptor->think(instance, 2500);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_BUSY);

  // Step another 2500 cycles: busy expires, returns to ready
  descriptor->think(instance, 2500);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_READY);
}

TEST_CASE("Printer Peripheral: Reset Lifecycle and Latch Clearing") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  harness.write_slot_reg(TEST_SLOT_1, 0, 'Z');
  CHECK(harness.activity_state(TEST_SLOT_1) == true);

  PrinterOnlineCmd_t online_cmd{0, {0, 0, 0}};
  CHECK(descriptor->command(instance, PRINTER_CMD_SET_ONLINE, &online_cmd,
                            sizeof(online_cmd)) == peripheral_ok);

  PrinterStatusQuery_t query{};
  size_t query_size = sizeof(query);
  CHECK(descriptor->query(instance, PRINTER_QUERY_STATUS, &query,
                          &query_size) == peripheral_ok);
  CHECK(query.is_online == 0);
  CHECK(query.last_char == 'Z');

  // Reset instance
  descriptor->reset(instance);

  // Query after reset: must return defaults
  CHECK(descriptor->query(instance, PRINTER_QUERY_STATUS, &query,
                          &query_size) == peripheral_ok);
  CHECK(query.is_online == 1);
  CHECK(query.is_busy == 0);
  CHECK(query.last_char == 0);
  CHECK(harness.activity_state(TEST_SLOT_1) == false);
}

TEST_CASE("Printer Peripheral: Offline State Hardware Suppression") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  harness.set_status(TEST_SLOT_1, STATUS_READY);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_READY);

  // Mark offline via command
  PrinterOnlineCmd_t online_cmd{0, {0, 0, 0}};
  CHECK(descriptor->command(instance, PRINTER_CMD_SET_ONLINE, &online_cmd,
                            sizeof(online_cmd)) == peripheral_ok);

  // Status read must return offline immediately
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_OFFLINE);

  // Mark back online
  online_cmd.online = 1;
  CHECK(descriptor->command(instance, PRINTER_CMD_SET_ONLINE, &online_cmd,
                            sizeof(online_cmd)) == peripheral_ok);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_READY);
}

TEST_CASE("Printer Peripheral: Full Binary Transparency Sweep") {
  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  std::vector<uint8_t> all_bytes(256);
  for (size_t i = 0; i < 256; ++i) {
    all_bytes[i] = static_cast<uint8_t>(i);
    CHECK(harness.write_slot_reg(TEST_SLOT_1, static_cast<uint8_t>(i % 16),
                                 all_bytes[i]) == TRANSMIT_SUCCESS);
  }

  const auto& printed = harness.printed_chars(TEST_SLOT_1);
  REQUIRE(printed.size() == 256);
  for (size_t i = 0; i < 256; ++i) {
    CHECK(printed[i] == all_bytes[i]);
  }
}

TEST_CASE("Printer Peripheral: State Round-Trip with Non-Default Values") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  harness.write_slot_reg(TEST_SLOT_1, 0, '1');
  harness.write_slot_reg(TEST_SLOT_1, 0, '2');
  harness.write_slot_reg(TEST_SLOT_1, 0, '3');

  PrinterOnlineCmd_t online_cmd{0, {0, 0, 0}};
  CHECK(descriptor->command(instance, PRINTER_CMD_SET_ONLINE, &online_cmd,
                            sizeof(online_cmd)) == peripheral_ok);

  std::vector<uint8_t> save_buf(sizeof(SsCardPrinter_t));
  size_t buf_size = save_buf.size();
  CHECK(descriptor->save_state(instance, save_buf.data(), &buf_size) ==
        peripheral_ok);

  // Reset instance to clean state
  descriptor->reset(instance);

  PrinterStatusQuery_t query{};
  size_t query_size = sizeof(query);
  CHECK(descriptor->query(instance, PRINTER_QUERY_STATUS, &query,
                          &query_size) == peripheral_ok);
  CHECK(query.is_online == 1);
  CHECK(query.last_char == 0);

  // Restore saved state
  CHECK(descriptor->load_state(instance, save_buf.data(), buf_size) ==
        peripheral_ok);

  CHECK(descriptor->query(instance, PRINTER_QUERY_STATUS, &query,
                          &query_size) == peripheral_ok);
  CHECK(query.total_chars_printed == 3);
  CHECK(query.last_char == '3');
  CHECK(query.is_online == 0);
}

TEST_CASE("Printer Peripheral: Robustness with Null Host Callbacks") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  harness.set_notify_activity_callback_enabled(false);
  harness.set_putchar_callback_enabled(false);
  harness.set_status_callback_enabled(false);

  // Write and read with null host callbacks must not crash
  CHECK(harness.write_slot_reg(TEST_SLOT_1, 0, 'X') == TRANSMIT_SUCCESS);
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 0) == STATUS_OFFLINE);

  // Think and reset with null host callbacks must not crash
  descriptor->think(instance, 1000);
  descriptor->reset(instance);
}

}  // namespace
