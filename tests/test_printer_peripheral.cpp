// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/printer/PrinterCommands.h"
#include "doctest.h"

namespace {

// The card is reached the way the emulator reaches it, through the registry,
// so one test binary covers the built-in card and the loaded plugin alike.
auto printer_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.printer");
}

constexpr int TEST_SLOT_1 = 1;
constexpr int TEST_SLOT_2 = 2;

constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;
constexpr int REGISTERS_PER_SLOT = 16;
constexpr size_t SLOT_ROM_PAGE_SIZE = 256;
constexpr size_t WAIT_IMAGE_SOURCE = 0x80;
constexpr size_t WAIT_IMAGE_TARGET = 0xC0;
constexpr size_t WAIT_IMAGE_LENGTH = 0x40;

constexpr uint8_t ROM_FIRST_BYTE = 0x18;
constexpr uint8_t ROM_LAST_BYTE = 0x84;

constexpr uint8_t TEST_CHAR_A = 'A';

constexpr size_t FRAME_SIZE = sizeof(PrinterSaveState_t);
using Frame_t = std::array<uint8_t, FRAME_SIZE>;

// Version 1, struct_size 24, the twelve bytes of total_chars_printed and
// busy_cycles zero, data_latch $33 after "123", then status_latch, is_online
// and is_busy zero.
constexpr Frame_t FRAME_AFTER_123 = {
    {0x01, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00, 0x00}};

// A frame as the card wrote it before it lost its busy model: three
// characters counted, 5000 busy cycles pending, data_latch $33, a status
// read of $7F recorded, offline, busy.
constexpr Frame_t FRAME_FROM_OLD_CARD = {
    {0x01, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x88, 0x13, 0x00, 0x00, 0x33, 0x7F, 0x00, 0x01}};

// Apple's PROM 341-0005 as res/roms/Parallel.rom holds it, transcribed here
// so the bytes the card hands the host are checked against a second copy
// rather than against themselves.
constexpr std::array<uint8_t, SLOT_ROM_PAGE_SIZE> PRINTER_ROM_LISTING = {
    {0x18, 0xb0, 0x38, 0x48, 0x8a, 0x48, 0x98, 0x48, 0x08, 0x78, 0x20, 0x58,
     0xff, 0xba, 0x68, 0x68, 0x68, 0x68, 0xa8, 0xca, 0x9a, 0x68, 0x28, 0xaa,
     0x90, 0x38, 0xbd, 0xb8, 0x05, 0x10, 0x19, 0x98, 0x29, 0x7f, 0x49, 0x30,
     0xc9, 0x0a, 0x90, 0x3b, 0xc9, 0x78, 0xb0, 0x29, 0x49, 0x3d, 0xf0, 0x21,
     0x98, 0x29, 0x9f, 0x9d, 0x38, 0x06, 0x90, 0x7e, 0xbd, 0xb8, 0x06, 0x30,
     0x14, 0xa5, 0x24, 0xdd, 0x38, 0x07, 0xb0, 0x0d, 0xc9, 0x11, 0xb0, 0x09,
     0x09, 0xf0, 0x3d, 0x38, 0x07, 0x65, 0x24, 0x85, 0x24, 0x4a, 0x38, 0xb0,
     0x6d, 0x18, 0x6a, 0x3d, 0xb8, 0x06, 0x90, 0x02, 0x49, 0x81, 0x9d, 0xb8,
     0x06, 0xd0, 0x53, 0xa0, 0x0a, 0x7d, 0x38, 0x05, 0x88, 0xd0, 0xfa, 0x9d,
     0xb8, 0x04, 0x9d, 0x38, 0x05, 0x38, 0xb0, 0x43, 0xc5, 0x24, 0x90, 0x3a,
     0x68, 0xa8, 0x68, 0xaa, 0x68, 0x4c, 0xf0, 0xfd, 0x90, 0xfe, 0xb0, 0xfe,
     0x99, 0x80, 0xc0, 0x90, 0x37, 0x49, 0x07, 0xa8, 0x49, 0x0a, 0x0a, 0xd0,
     0x06, 0xb8, 0x85, 0x24, 0x9d, 0x38, 0x07, 0xbd, 0xb8, 0x06, 0x4a, 0x70,
     0x02, 0xb0, 0x23, 0x0a, 0x0a, 0xa9, 0x27, 0xb0, 0xcf, 0xbd, 0x38, 0x07,
     0xfd, 0xb8, 0x04, 0xc9, 0xf8, 0x90, 0x03, 0x69, 0x27, 0xac, 0xa9, 0x00,
     0x85, 0x24, 0x18, 0x7e, 0xb8, 0x05, 0x68, 0xa8, 0x68, 0xaa, 0x68, 0x60,
     0x90, 0x27, 0xb0, 0x00, 0x10, 0x11, 0xa9, 0x89, 0x9d, 0x38, 0x06, 0x9d,
     0xb8, 0x06, 0xa9, 0x28, 0x9d, 0xb8, 0x04, 0xa9, 0x02, 0x85, 0x36, 0x98,
     0x5d, 0x38, 0x06, 0x0a, 0xf0, 0x90, 0x5e, 0xb8, 0x05, 0x98, 0x48, 0x8a,
     0x0a, 0x0a, 0x0a, 0x0a, 0xa8, 0xbd, 0x38, 0x07, 0xc5, 0x24, 0x68, 0xb0,
     0x05, 0x48, 0x29, 0x80, 0x09, 0x20, 0x2c, 0x58, 0xff, 0xf0, 0x03, 0xfe,
     0x38, 0x07, 0x70, 0x84}};

// Distinct cycles must give distinct bytes, so a read that asks the host for
// the bus can be told apart from any constant.
auto floating_bus_marker(uint32_t executed_cycles) -> uint8_t {
  return static_cast<uint8_t>((executed_cycles * 0x1D) ^ 0xA5);
}

struct MockHandler_t {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockHandler_t() = default;
  MockHandler_t(void* inst, PeripheralIOHandler r, PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

// One sink per slot, as the bridge keeps them; the token the card holds is
// the record's address. A byte written while the sink is not ready is dropped
// and counted, which is what the frontend does when its file is gone.
struct MockSink_t {
  std::vector<uint8_t> bytes;
  size_t dropped = 0;
  size_t ready_polls = 0;
  size_t closes = 0;
  bool ready = true;
  PeripheralSinkKind_t kind = peripheral_sink_printer;
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
    host_.NotifyActivityChanged = Mock_NotifyActivityChanged;
    host_.ReadFloatingBus = Mock_ReadFloatingBus;
    host_.SinkOpen = Mock_SinkOpen;
    host_.SinkWrite = Mock_SinkWrite;
    host_.SinkReady = Mock_SinkReady;
    host_.SinkClose = Mock_SinkClose;
  }

  ~PrinterHarness() {
    for (const auto& entry : instances_) {
      if (entry.second != nullptr) {
        printer_descriptor()->shutdown(entry.second);
      }
    }
    instances_.clear();
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

  auto set_sink_ready(int slot, bool ready) -> void {
    sinks_.at(static_cast<size_t>(slot)).ready = ready;
  }

  auto sink(int slot) const -> const MockSink_t& {
    return sinks_.at(static_cast<size_t>(slot));
  }

  auto printed_chars(int slot) const -> const std::vector<uint8_t>& {
    return sink(slot).bytes;
  }

  auto clear_printed_chars(int slot) -> void {
    sinks_.at(static_cast<size_t>(slot)).bytes.clear();
  }

  auto set_notify_activity_callback_enabled(bool enabled) -> void {
    host_.NotifyActivityChanged =
        enabled ? Mock_NotifyActivityChanged : nullptr;
  }

  auto set_log_enabled(bool enabled) -> void {
    host_.Log = enabled ? Mock_Log : nullptr;
  }

  auto activity_changes_count(int slot) const -> size_t {
    auto it = activity_counts_.find(slot);
    return (it != activity_counts_.end()) ? it->second : 0;
  }

  auto read_io(uint16_t addr, uint8_t is_write = 0, uint8_t val = 0,
               uint32_t cycles = 0) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      return it->second.read(it->second.instance, 0, addr, is_write, val,
                             cycles);
    }
    return 0;
  }

  auto write_io(uint16_t addr, uint8_t val, uint8_t is_write = 1) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      return it->second.write(it->second.instance, 0, addr, is_write, val, 0);
    }
    return 0;
  }

  auto read_slot_reg(int slot, uint8_t offset, uint8_t is_write = 0,
                     uint8_t val = 0, uint32_t cycles = 0) -> uint8_t {
    const uint16_t addr = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET) + offset;
    return read_io(addr, is_write, val, cycles);
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

  auto rom_pointer(int slot) const -> const uint8_t* {
    auto it = rom_history_.find(slot);
    return (it != rom_history_.end() && !it->second.empty()) ? it->second.back()
                                                             : nullptr;
  }

  auto rom_registrations(int slot) const -> size_t {
    auto it = rom_history_.find(slot);
    return (it != rom_history_.end()) ? it->second.size() : 0;
  }

  auto log_messages() const -> const std::vector<std::string>& {
    return log_messages_;
  }

 private:
  HostInterface_t host_{};
  std::map<uint16_t, MockHandler_t> handlers_;
  std::map<int, std::vector<uint8_t>> roms_;
  std::map<int, std::vector<const uint8_t*>> rom_history_;
  std::vector<std::string> log_messages_;
  std::map<int, void*> instances_;
  std::map<int, size_t> activity_counts_;
  std::array<MockSink_t, 8> sinks_{};

  static PrinterHarness* s_active_harness;

  static auto sink_from_token(void* token) -> MockSink_t* {
    if (s_active_harness == nullptr || token == nullptr) {
      return nullptr;
    }
    for (MockSink_t& record : s_active_harness->sinks_) {
      if (&record == token) {
        return &record;
      }
    }
    return nullptr;
  }

  static auto Mock_NotifyActivityChanged(int slot, bool active) -> void {
    (void)active;
    if (s_active_harness != nullptr) {
      s_active_harness->activity_counts_[slot]++;
    }
  }

  static auto Mock_Log(void* instance, PeripheralLogLevel_t level,
                       const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    if (s_active_harness == nullptr) {
      return;
    }
    std::array<char, 256> text{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(text.data(), text.size(), fmt, args);
    va_end(args);
    s_active_harness->log_messages_.emplace_back(text.data());
  }

  static auto Mock_ReadFloatingBus(uint32_t executed_cycles) -> uint8_t {
    return floating_bus_marker(executed_cycles);
  }

  static auto Mock_SinkOpen(void* instance, int slot, PeripheralSinkKind_t kind)
      -> void* {
    (void)instance;
    if (s_active_harness == nullptr || slot < 1 || slot > 7) {
      return nullptr;
    }
    MockSink_t& record = s_active_harness->sinks_.at(static_cast<size_t>(slot));
    record.kind = kind;
    return &record;
  }

  static auto Mock_SinkWrite(void* token, uint8_t byte) -> void {
    MockSink_t* record = sink_from_token(token);
    if (record == nullptr) {
      return;
    }
    if (!record->ready) {
      record->dropped++;
      return;
    }
    record->bytes.push_back(byte);
  }

  static auto Mock_SinkReady(void* token) -> bool {
    MockSink_t* record = sink_from_token(token);
    if (record == nullptr) {
      return false;
    }
    record->ready_polls++;
    return record->ready;
  }

  static auto Mock_SinkClose(void* token) -> void {
    MockSink_t* record = sink_from_token(token);
    if (record != nullptr) {
      record->closes++;
    }
  }

  static auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
    (void)slot;
    (void)assert_irq;
  }

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
      s_active_harness->rom_history_[slot].push_back(rom_ptr);
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
};

PrinterHarness* PrinterHarness::s_active_harness = nullptr;

auto save_frame(void* instance) -> Frame_t {
  Frame_t frame{};
  size_t size = frame.size();
  REQUIRE(printer_descriptor()->save_state(instance, frame.data(), &size) ==
          peripheral_ok);
  REQUIRE(size == FRAME_SIZE);
  return frame;
}

TEST_CASE("Printer Peripheral: Descriptor Metadata and Compatibility") {
  const auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  CHECK(descriptor->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(descriptor->id) == "linapple.printer");
  CHECK(std::string(descriptor->name) == "Parallel Printer");
  CHECK(std::string(descriptor->description) ==
        "Apple Parallel Printer Interface Card (A2B0002)");
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

TEST_CASE("Printer Peripheral: The registry resolves the card by its id") {
  Peripheral_t* by_id = peripheral_find_internal("linapple.printer");
  REQUIRE(by_id != nullptr);
  CHECK(by_id == printer_descriptor());
  CHECK(std::string(by_id->id) == "linapple.printer");
  CHECK(std::string(by_id->name) == "Parallel Printer");
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

  REQUIRE(harness.has_rom(TEST_SLOT_1));
  const auto& rom = harness.get_rom(TEST_SLOT_1);
  CHECK(rom.size() == SLOT_ROM_PAGE_SIZE);
  CHECK(rom.at(0) == ROM_FIRST_BYTE);
  CHECK(rom.at(SLOT_ROM_PAGE_SIZE - 1) == ROM_LAST_BYTE);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 1);
}

TEST_CASE("Printer Peripheral: I/O Mirroring Across Slot Registers") {
  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  std::vector<uint8_t> expected;
  for (uint8_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
    const uint8_t char_to_write = static_cast<uint8_t>(TEST_CHAR_A + i);
    CHECK(harness.write_slot_reg(TEST_SLOT_1, i, char_to_write) ==
          char_to_write);
    expected.push_back(char_to_write);
  }
  CHECK(harness.printed_chars(TEST_SLOT_1) == expected);

  // A read at every offset is one more strobe: the undriven bus byte goes to
  // the printer and comes back to the 6502.
  for (uint8_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
    const uint32_t cycles = 100 + (i * 3U);
    CHECK(harness.read_slot_reg(TEST_SLOT_1, i, 0, 0, cycles) ==
          floating_bus_marker(cycles));
    expected.push_back(floating_bus_marker(cycles));
  }
  CHECK(harness.printed_chars(TEST_SLOT_1) == expected);
  CHECK(harness.printed_chars(TEST_SLOT_1).size() == 32);
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
    harness.write_slot_reg(TEST_SLOT_1, reg_offset, payload[i]);
  }

  CHECK(harness.printed_chars(TEST_SLOT_1) == payload);
  CHECK(harness.sink(TEST_SLOT_1).dropped == 0);

  harness.clear_printed_chars(TEST_SLOT_1);
  CHECK(harness.printed_chars(TEST_SLOT_1).empty());
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

  // Shut down slot 1: its sink is closed once and slot 2 keeps printing.
  harness.shutdown_instance(TEST_SLOT_1);
  CHECK(harness.get_instance(TEST_SLOT_1) == nullptr);
  CHECK(harness.sink(TEST_SLOT_1).closes == 1);
  CHECK(harness.sink(TEST_SLOT_2).closes == 0);

  harness.write_slot_reg(TEST_SLOT_2, 0, '!');
  const std::vector<uint8_t> expected_slot2 = {'S', 'L', 'O', 'T', '2', '!'};
  CHECK(harness.printed_chars(TEST_SLOT_2) == expected_slot2);
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

  // An expansion card sits in slots 1 to 7; anything else is refused before
  // an address is formed from it, and the log names the slot.
  for (int slot : {0, 8, -1, 255}) {
    CAPTURE(slot);
    const size_t logged_before = harness.log_messages().size();
    CHECK(descriptor->init(slot, harness.host()) == nullptr);
    REQUIRE(harness.log_messages().size() == logged_before + 1);
    CHECK(harness.log_messages().back().find("slot " + std::to_string(slot)) !=
          std::string::npos);
  }

  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  // Reset and think with valid instance must not crash
  descriptor->reset(instance);
  descriptor->think(instance, 1024);

  const uint16_t base = IO_BASE_ADDRESS + (TEST_SLOT_1 << IO_SLOT_OFFSET);
  const auto& handler = harness.get_handler(base);

  // A handler with no card behind it answers nothing and strobes nothing.
  CHECK(handler.read(nullptr, 0, base, 0, 0, 7) == 0);
  CHECK(handler.write(nullptr, 0, base, 1, TEST_CHAR_A, 0) == 0);
  CHECK(harness.printed_chars(TEST_SLOT_1).empty());
  CHECK(harness.sink(TEST_SLOT_1).dropped == 0);
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
  CHECK(required_size == FRAME_SIZE);

  // Null buffer_size pointer must fail
  CHECK(descriptor->save_state(instance, nullptr, nullptr) == peripheral_error);

  // Undersized buffer must fail
  std::vector<uint8_t> undersized(required_size - 1, 0);
  size_t too_small = undersized.size();
  CHECK(descriptor->save_state(instance, undersized.data(), &too_small) ==
        peripheral_error);

  // Null instance on save/load must fail
  Frame_t frame = FRAME_AFTER_123;
  size_t frame_size = frame.size();
  CHECK(descriptor->save_state(nullptr, frame.data(), &frame_size) ==
        peripheral_error);
  CHECK(descriptor->load_state(nullptr, frame.data(), frame.size()) ==
        peripheral_error);
  CHECK(descriptor->load_state(instance, nullptr, frame.size()) ==
        peripheral_error);

  // Successful round-trip restoration
  CHECK(descriptor->load_state(instance, frame.data(), frame.size()) ==
        peripheral_ok);
  CHECK(save_frame(instance) == FRAME_AFTER_123);
}

TEST_CASE("Printer Peripheral: Command and Query ABI Protocol") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);
  REQUIRE(descriptor->command != nullptr);
  REQUIRE(descriptor->query != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  // The ids the card once answered (set online, reset statistics, status)
  // and a foreign subsystem's id are all somebody else's now.
  const std::array<uint32_t, 3> retired_commands = {
      {0x00080101U, 0x00080102U, 0x00010101U}};
  const std::array<uint8_t, 4> payload = {{1, 0, 0, 0}};
  for (uint32_t id : retired_commands) {
    CAPTURE(id);
    CHECK(descriptor->command(instance, id, payload.data(), payload.size()) ==
          peripheral_incompatible);
    CHECK(descriptor->command(instance, id, nullptr, 0) ==
          peripheral_incompatible);
    CHECK(descriptor->command(nullptr, id, payload.data(), payload.size()) ==
          peripheral_error);
  }

  const std::array<uint32_t, 2> retired_queries = {{0x00080100U, 0x00010100U}};
  std::array<uint8_t, 16> out{};
  for (uint32_t id : retired_queries) {
    CAPTURE(id);
    size_t out_size = 48879;
    CHECK(descriptor->query(instance, id, out.data(), &out_size) ==
          peripheral_incompatible);
    CHECK(out_size == 48879);
    CHECK(descriptor->query(instance, id, nullptr, &out_size) ==
          peripheral_incompatible);
    CHECK(out_size == 48879);
    CHECK(descriptor->query(instance, id, out.data(), nullptr) ==
          peripheral_error);
  }
}

TEST_CASE("Printer Peripheral: Activity Notification and Pulse Decay") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  // Printing is not disk activity: nothing here may switch the machine to
  // full speed.
  harness.write_slot_reg(TEST_SLOT_1, 0, 'A');
  harness.write_slot_reg(TEST_SLOT_1, 0, 'B');
  for (int i = 0; i < 12; ++i) {
    descriptor->think(instance, 1000);
  }
  descriptor->reset(instance);
  CHECK(harness.activity_changes_count(TEST_SLOT_1) == 0);
}

TEST_CASE("Printer Peripheral: The wait image is the PROM with A6 forced") {
  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);
  const uint8_t* normal = harness.rom_pointer(TEST_SLOT_1);
  REQUIRE(normal != nullptr);

  harness.set_sink_ready(TEST_SLOT_1, false);
  harness.write_slot_reg(TEST_SLOT_1, 0, 'A');
  REQUIRE(harness.rom_registrations(TEST_SLOT_1) == 2);
  const uint8_t* waiting = harness.rom_pointer(TEST_SLOT_1);
  REQUIRE(waiting != nullptr);
  CHECK(waiting != normal);

  const auto& image = harness.get_rom(TEST_SLOT_1);
  for (size_t i = 0; i < WAIT_IMAGE_TARGET; ++i) {
    CAPTURE(i);
    CHECK(image.at(i) == PRINTER_ROM_LISTING.at(i));
  }
  for (size_t i = 0; i < WAIT_IMAGE_LENGTH; ++i) {
    CAPTURE(i);
    CHECK(image.at(WAIT_IMAGE_TARGET + i) ==
          PRINTER_ROM_LISTING.at(WAIT_IMAGE_SOURCE + i));
  }
  // The branches the firmware parks on: BCC * at $C0, BCS * at $C2.
  CHECK(image.at(0xC0) == 0x90);
  CHECK(image.at(0xC1) == 0xFE);
  CHECK(image.at(0xC2) == 0xB0);
  CHECK(image.at(0xC3) == 0xFE);
}

TEST_CASE("Printer Peripheral: Hardware Busy Delay and Cycle Stepping") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);
  const uint8_t* normal = harness.rom_pointer(TEST_SLOT_1);
  REQUIRE(normal != nullptr);
  REQUIRE(harness.log_messages().empty());

  // The byte goes out first, as DEVICE SELECT latches and strobes before the
  // firmware can learn anything; then the page swaps and the log says why.
  harness.set_sink_ready(TEST_SLOT_1, false);
  harness.write_slot_reg(TEST_SLOT_1, 0, 'A');
  CHECK(harness.sink(TEST_SLOT_1).dropped == 1);
  CHECK(harness.sink(TEST_SLOT_1).ready_polls == 1);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 2);
  CHECK(harness.rom_pointer(TEST_SLOT_1) != normal);
  REQUIRE(harness.log_messages().size() == 1);
  CHECK(harness.log_messages().back().find("slot 1") != std::string::npos);
  CHECK(harness.log_messages().back().find("not ready") != std::string::npos);

  // Still not ready: think polls once per call and registers nothing more.
  descriptor->think(instance, 17030);
  descriptor->think(instance, 17030);
  CHECK(harness.sink(TEST_SLOT_1).ready_polls == 3);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 2);
  CHECK(harness.log_messages().size() == 1);

  // Two more strobes into the parked printer: dropped, no new page, no new
  // line in the log.
  harness.write_slot_reg(TEST_SLOT_1, 0, 'B');
  harness.write_slot_reg(TEST_SLOT_1, 0, 'C');
  CHECK(harness.sink(TEST_SLOT_1).dropped == 3);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 2);
  CHECK(harness.log_messages().size() == 1);

  // Ready again: the next think puts the PROM back so BCS * falls through.
  harness.set_sink_ready(TEST_SLOT_1, true);
  descriptor->think(instance, 17030);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 3);
  CHECK(harness.rom_pointer(TEST_SLOT_1) == normal);
  CHECK(harness.log_messages().size() == 1);

  // Not waiting: think reads nothing and registers nothing.
  const size_t polls_before = harness.sink(TEST_SLOT_1).ready_polls;
  descriptor->think(instance, 17030);
  CHECK(harness.sink(TEST_SLOT_1).ready_polls == polls_before);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 3);

  // Printing resumes through the same sink.
  harness.write_slot_reg(TEST_SLOT_1, 0, 'D');
  CHECK(harness.printed_chars(TEST_SLOT_1) == std::vector<uint8_t>{'D'});
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 3);
}

TEST_CASE("Printer Peripheral: Reset Lifecycle and Latch Clearing") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);
  const uint8_t* normal = harness.rom_pointer(TEST_SLOT_1);

  // RESET reaches neither the data register nor, in this model, a busy
  // flip-flop: the last byte stays, and a wait in progress stays pending.
  harness.write_slot_reg(TEST_SLOT_1, 0, 'Z');
  harness.set_sink_ready(TEST_SLOT_1, false);
  harness.write_slot_reg(TEST_SLOT_1, 0, 'Y');
  REQUIRE(harness.rom_registrations(TEST_SLOT_1) == 2);
  const uint8_t* waiting = harness.rom_pointer(TEST_SLOT_1);

  descriptor->reset(instance);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 2);
  CHECK(harness.rom_pointer(TEST_SLOT_1) == waiting);
  CHECK(harness.log_messages().size() == 1);
  Frame_t frame = save_frame(instance);
  CHECK(frame.at(20) == 'Y');

  descriptor->think(instance, 17030);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 2);

  harness.set_sink_ready(TEST_SLOT_1, true);
  descriptor->think(instance, 17030);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 3);
  CHECK(harness.rom_pointer(TEST_SLOT_1) == normal);
}

TEST_CASE("Printer Peripheral: Full Binary Transparency Sweep") {
  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  std::vector<uint8_t> all_bytes(256);
  for (size_t i = 0; i < 256; ++i) {
    all_bytes[i] = static_cast<uint8_t>(i);
    CHECK(harness.write_slot_reg(TEST_SLOT_1, static_cast<uint8_t>(i % 16),
                                 all_bytes[i]) == all_bytes[i]);
  }

  CHECK(harness.printed_chars(TEST_SLOT_1) == all_bytes);
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
  CHECK(save_frame(instance) == FRAME_AFTER_123);

  // A frame from the card as it was loads, and comes back with the dead
  // fields zeroed.
  CHECK(descriptor->load_state(instance, FRAME_FROM_OLD_CARD.data(),
                               FRAME_FROM_OLD_CARD.size()) == peripheral_ok);
  CHECK(save_frame(instance) == FRAME_AFTER_123);

  // A slot buffer larger than the frame loads what the frame says it holds.
  std::array<uint8_t, 32> larger{};
  std::copy(FRAME_AFTER_123.begin(), FRAME_AFTER_123.end(), larger.begin());
  larger.at(20) = 0x44;
  CHECK(descriptor->load_state(instance, larger.data(), larger.size()) ==
        peripheral_ok);
  CHECK(save_frame(instance).at(20) == 0x44);
  CHECK(descriptor->load_state(instance, FRAME_AFTER_123.data(),
                               FRAME_AFTER_123.size()) == peripheral_ok);

  // Every rejected frame leaves the latch as it was: each carries $55 where
  // the card holds $33, and the re-saved frame is still the literal.
  struct Rejected_t {
    const char* name;
    size_t offset;
    uint8_t value;
  };
  const std::array<Rejected_t, 7> rejected = {{
      {"version 0", 0, 0x00},
      {"version 2", 0, 0x02},
      {"version E7", 0, 0xE7},
      {"struct_size 10", 4, 0x10},
      {"struct_size 1A", 4, 0x1A},
      {"struct_size 39", 4, 0x39},
      {"struct_size 30 in the high byte", 7, 0x30},
  }};
  for (const Rejected_t& reject : rejected) {
    CAPTURE(reject.name);
    Frame_t frame = FRAME_AFTER_123;
    frame.at(20) = 0x55;
    frame.at(reject.offset) = reject.value;
    CHECK(descriptor->load_state(instance, frame.data(), frame.size()) ==
          peripheral_error);
    CHECK(save_frame(instance) == FRAME_AFTER_123);
  }
  for (size_t short_size : {23U, 7U, 0U}) {
    CAPTURE(short_size);
    Frame_t frame = FRAME_AFTER_123;
    frame.at(20) = 0x55;
    CHECK(descriptor->load_state(instance, frame.data(), short_size) ==
          peripheral_error);
    CHECK(save_frame(instance) == FRAME_AFTER_123);
  }
  CHECK(descriptor->load_state(instance, nullptr, FRAME_SIZE) ==
        peripheral_error);
  CHECK(descriptor->load_state(nullptr, FRAME_AFTER_123.data(), FRAME_SIZE) ==
        peripheral_error);
  CHECK(save_frame(instance) == FRAME_AFTER_123);
}

TEST_CASE("Printer Peripheral: A load puts the PROM back, whatever was shown") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);
  const uint8_t* normal = harness.rom_pointer(TEST_SLOT_1);

  harness.set_sink_ready(TEST_SLOT_1, false);
  harness.write_slot_reg(TEST_SLOT_1, 0, 'A');
  REQUIRE(harness.rom_pointer(TEST_SLOT_1) != normal);

  // The frame carries no wait, so the loaded machine shows the PROM; the
  // next strobe samples the sink again and parks once more, as it must.
  CHECK(descriptor->load_state(instance, FRAME_AFTER_123.data(),
                               FRAME_AFTER_123.size()) == peripheral_ok);
  CHECK(harness.rom_pointer(TEST_SLOT_1) == normal);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 3);

  harness.write_slot_reg(TEST_SLOT_1, 0, 'B');
  CHECK(harness.sink(TEST_SLOT_1).dropped == 2);
  CHECK(harness.rom_pointer(TEST_SLOT_1) != normal);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 4);
}

TEST_CASE("Printer Peripheral: Robustness with Null Host Callbacks") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  PrinterHarness harness;
  void* instance = harness.create_printer(TEST_SLOT_1);
  REQUIRE(instance != nullptr);

  harness.set_notify_activity_callback_enabled(false);
  harness.set_log_enabled(false);

  // Printing, waiting, thinking and resetting with nothing to notify and
  // nowhere to log must not crash; the wait still engages, silently.
  CHECK(harness.write_slot_reg(TEST_SLOT_1, 0, 'X') == 'X');
  harness.set_sink_ready(TEST_SLOT_1, false);
  harness.write_slot_reg(TEST_SLOT_1, 0, 'Y');
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 2);
  CHECK(harness.log_messages().empty());
  descriptor->think(instance, 1000);
  descriptor->reset(instance);
  harness.set_sink_ready(TEST_SLOT_1, true);
  descriptor->think(instance, 1000);
  CHECK(harness.rom_registrations(TEST_SLOT_1) == 3);
}

TEST_CASE("Printer Peripheral: The slot ROM is the PROM, byte for byte") {
  PrinterHarness harness;
  REQUIRE(harness.create_printer(TEST_SLOT_1) != nullptr);
  const uint8_t* registered = harness.rom_pointer(TEST_SLOT_1);
  REQUIRE(registered != nullptr);

  const auto first_difference = std::mismatch(
      PRINTER_ROM_LISTING.begin(), PRINTER_ROM_LISTING.end(), registered);
  const size_t offset = static_cast<size_t>(
      std::distance(PRINTER_ROM_LISTING.begin(), first_difference.first));
  CAPTURE(offset);
  CHECK(first_difference.first == PRINTER_ROM_LISTING.end());

  // The wait images at $80/$82 (BCC * and BCS *), the one store to the card
  // at $84 (STA $C080,Y), and the live branches at $C0/$C2 they stand in for.
  CHECK(registered[0x80] == 0x90);
  CHECK(registered[0x81] == 0xFE);
  CHECK(registered[0x82] == 0xB0);
  CHECK(registered[0x83] == 0xFE);
  CHECK(registered[0x84] == 0x99);
  CHECK(registered[0x85] == 0x80);
  CHECK(registered[0x86] == 0xC0);
  CHECK(registered[0xC0] == 0x90);
  CHECK(registered[0xC1] == 0x27);
  CHECK(registered[0xC2] == 0xB0);
  CHECK(registered[0xC3] == 0x00);
}

TEST_CASE("Printer Peripheral: A read answers with the host's floating bus") {
  PrinterHarness harness;
  REQUIRE(harness.create_printer(TEST_SLOT_1) != nullptr);
  REQUIRE(floating_bus_marker(7) != floating_bus_marker(42));

  // R/W is not wired to the card, so a read is a strobe of whatever byte the
  // undriven bus holds, and the 6502 reads that same byte back.
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 5, 0, 0, 7) ==
        floating_bus_marker(7));
  CHECK(harness.read_slot_reg(TEST_SLOT_1, 5, 0, 0, 42) ==
        floating_bus_marker(42));
  const std::vector<uint8_t> strobed = {floating_bus_marker(7),
                                        floating_bus_marker(42)};
  CHECK(harness.printed_chars(TEST_SLOT_1) == strobed);
}

TEST_CASE(
    "Printer Peripheral: A host missing a member gets no card, and hears why") {
  PrinterHarness harness;
  struct Missing_t {
    const char* name;
    void (*strip)(HostInterface_t*);
  };
  const std::array<Missing_t, 7> members = {{
      {"RegisterIO", [](HostInterface_t* h) { h->RegisterIO = nullptr; }},
      {"RegisterCxROM", [](HostInterface_t* h) { h->RegisterCxROM = nullptr; }},
      {"ReadFloatingBus",
       [](HostInterface_t* h) { h->ReadFloatingBus = nullptr; }},
      {"SinkOpen", [](HostInterface_t* h) { h->SinkOpen = nullptr; }},
      {"SinkWrite", [](HostInterface_t* h) { h->SinkWrite = nullptr; }},
      {"SinkReady", [](HostInterface_t* h) { h->SinkReady = nullptr; }},
      {"SinkClose", [](HostInterface_t* h) { h->SinkClose = nullptr; }},
  }};

  for (const Missing_t& member : members) {
    CAPTURE(member.name);
    HostInterface_t partial = *harness.host();
    member.strip(&partial);
    const size_t logged_before = harness.log_messages().size();
    CHECK(printer_descriptor()->init(TEST_SLOT_2, &partial) == nullptr);
    REQUIRE(harness.log_messages().size() == logged_before + 1);
    CHECK(harness.log_messages().back().find(member.name) != std::string::npos);
    CHECK(harness.log_messages().back().find("slot 2") != std::string::npos);
  }

  // A host that cannot even log is still refused, silently.
  HostInterface_t mute = *harness.host();
  mute.Log = nullptr;
  mute.SinkReady = nullptr;
  const size_t logged_before = harness.log_messages().size();
  CHECK(printer_descriptor()->init(TEST_SLOT_2, &mute) == nullptr);
  CHECK(harness.log_messages().size() == logged_before);

  // The complete host still gets its card.
  CHECK(harness.create_printer(TEST_SLOT_1) != nullptr);
}

}  // namespace

extern "C" auto printer_abi_c_state_size() -> size_t;
extern "C" auto printer_abi_c_version_offset() -> size_t;
extern "C" auto printer_abi_c_struct_size_offset() -> size_t;
extern "C" auto printer_abi_c_total_chars_printed_offset() -> size_t;
extern "C" auto printer_abi_c_busy_cycles_offset() -> size_t;
extern "C" auto printer_abi_c_data_latch_offset() -> size_t;
extern "C" auto printer_abi_c_status_latch_offset() -> size_t;
extern "C" auto printer_abi_c_is_online_offset() -> size_t;
extern "C" auto printer_abi_c_is_busy_offset() -> size_t;
extern "C" auto printer_abi_c_state_version() -> uint32_t;

TEST_CASE("Printer Peripheral: The C99 view of the state frame matches C++") {
  CHECK(printer_abi_c_state_size() == 24);
  CHECK(printer_abi_c_state_size() == sizeof(PrinterSaveState_t));
  CHECK(printer_abi_c_version_offset() == 0);
  CHECK(printer_abi_c_struct_size_offset() == 4);
  CHECK(printer_abi_c_total_chars_printed_offset() == 8);
  CHECK(printer_abi_c_busy_cycles_offset() == 16);
  CHECK(printer_abi_c_data_latch_offset() == 20);
  CHECK(printer_abi_c_status_latch_offset() == 21);
  CHECK(printer_abi_c_is_online_offset() == 22);
  CHECK(printer_abi_c_is_busy_offset() == 23);
  CHECK(printer_abi_c_state_version() == 1);
  CHECK(printer_abi_c_state_version() == PRINTER_STATE_VERSION);
}
