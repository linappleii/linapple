// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "HeadlessHarness.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/printer/PrinterCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

// The card is reached the way the emulator reaches it, through the registry,
// so one test binary covers the built-in card and the loaded plugin alike.
auto printer_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.printer");
}

constexpr int test_slot_1 = 1;
constexpr int test_slot_2 = 2;
constexpr int test_slot_7 = 7;

constexpr uint16_t io_base_address = 0xC080;
constexpr int io_slot_shift = 4;
constexpr int registers_per_slot = 16;
constexpr size_t slot_rom_size = 256;
constexpr size_t wait_image_source = 0x80;
constexpr size_t wait_image_target = 0xC0;
constexpr size_t wait_image_length = 0x40;

constexpr uint8_t rom_first_byte = 0x18;
constexpr uint8_t rom_last_byte = 0x84;

constexpr size_t frame_size = sizeof(PrinterSaveState_t);
constexpr size_t frame_latch_offset = offsetof(PrinterSaveState_t, data_latch);
using Frame_t = std::array<uint8_t, frame_size>;

// Version 1, struct_size 24, the twelve bytes of total_chars_printed and
// busy_cycles zero, data_latch $33 after "123", then status_latch, is_online
// and is_busy zero.
constexpr Frame_t frame_after_123 = {
    {0x01, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00, 0x00}};

// A frame as the card wrote it before it lost its busy model: three
// characters counted, 5000 busy cycles pending, data_latch $33, a status
// read of $7F recorded, offline, busy.
constexpr Frame_t frame_from_old_card = {
    {0x01, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x88, 0x13, 0x00, 0x00, 0x33, 0x7F, 0x00, 0x01}};

// Apple's PROM 341-0005 as res/roms/Parallel.rom holds it, transcribed here
// so the bytes the card hands the host are checked against a second copy
// rather than against themselves.
constexpr std::array<uint8_t, slot_rom_size> printer_rom_listing = {
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

// The firmware's two entry points and the two branches it parks on: $Cn00
// (CLC) initialises the screen holes before printing, $Cn02 (SEC) prints;
// BCC PRNT at $CnC0 waits before a tab-fill blank, BCS *+2 at $CnC2 before
// every other character.
constexpr uint8_t rom_entry_init = 0x00;
constexpr uint8_t rom_entry_print = 0x02;
constexpr uint8_t rom_wait_blank = 0xC0;
constexpr uint8_t rom_wait_plain = 0xC2;

auto rom_address(int slot, uint8_t offset) -> uint16_t {
  return static_cast<uint16_t>(0xC000 + (slot << 8) + offset);
}

auto card_address(int slot, uint8_t offset = 0) -> uint16_t {
  return static_cast<uint16_t>(io_base_address + (slot << io_slot_shift) +
                               offset);
}

// The Monitor's zero page as the firmware and COUT1 use it, and the screen
// holes the firmware indexes with X = $Cn: the operands of its ,X loads are
// $04B8, $0538, $05B8, $0638, $06B8 and $0738, so slot n's holes are $0578+n
// (PWDTH), $05F8+n (MSTRT), $0678+n (MODE), $06F8+n (ESCHAR), $0778+n (FLAGS)
// and $07F8+n (COL).
constexpr uint16_t zp_wndlft = 0x20;
constexpr uint16_t zp_wndwdth = 0x21;
constexpr uint16_t zp_wndtop = 0x22;
constexpr uint16_t zp_wndbtm = 0x23;
constexpr uint16_t zp_ch = 0x24;
constexpr uint16_t zp_cv = 0x25;
constexpr uint16_t zp_basl = 0x28;
constexpr uint16_t zp_bash = 0x29;
constexpr uint16_t zp_invflg = 0x32;
constexpr uint16_t zp_cswl = 0x36;
constexpr uint16_t zp_cswh = 0x37;
constexpr uint16_t hole_operand_pwdth = 0x04B8;
constexpr uint16_t hole_operand_mstrt = 0x0538;
constexpr uint16_t hole_operand_mode = 0x05B8;
constexpr uint16_t hole_operand_eschar = 0x0638;
constexpr uint16_t hole_operand_flags = 0x06B8;
constexpr uint16_t hole_operand_col = 0x0738;

auto hole(uint16_t operand, int slot) -> uint16_t {
  return static_cast<uint16_t>(operand + 0xC0 + slot);
}

constexpr uint16_t monitor_reset = 0xFA62;
constexpr uint16_t monitor_iorts = 0xFF58;
constexpr uint8_t opcode_rts = 0x60;
constexpr uint8_t cout1_low = 0xF0;
constexpr uint8_t cout1_high = 0xFD;
constexpr uint16_t text_row_0 = 0x0400;
constexpr int text_columns = 40;
constexpr int text_rows = 24;

// What DEFAULT writes on the first call through $Cn00: Ctrl-I as the escape
// character, video-also and CRLF on with bit 3 as a non-zero sentinel, a
// 40-column width, and CSWL pointing later calls at $Cn02.
constexpr uint8_t default_eschar = 0x89;
constexpr uint8_t default_flags = 0x89;
constexpr uint8_t default_width = 0x28;
constexpr uint8_t cswl_after_default = 0x02;
constexpr uint8_t mode_after_escape = 0x80;

// Where a routine under test returns to, its stack, and the caps: a
// character takes a few hundred cycles, so 20,000 catches a runaway; a parked
// machine is left spinning for 3,000, well short of a frame, so the card's
// per-frame poll cannot release it by accident; the Monitor's reset runs
// until it has restored CSW, which is inside 100,000.
constexpr uint16_t return_sentinel = 0x0300;
constexpr uint16_t stack_top = 0x01FF;
constexpr uint32_t subroutine_cycle_cap = 20000;
constexpr uint32_t park_cycle_cap = 3000;
constexpr uint32_t reset_cycle_cap = 100000;
constexpr uint8_t status_interrupts_masked = 0x24;
constexpr uint8_t status_carry = 0x01;
constexpr uint32_t cycles_per_frame = 17030;

// High-ASCII as the Monitor and Applesoft hand characters to COUT.
constexpr uint8_t high_ctrl_i = 0x89;
constexpr uint8_t high_ctrl_a = 0x81;
constexpr uint8_t high_cr = 0x8D;
constexpr uint8_t high_lf = 0x8A;
constexpr uint8_t high_space = 0xA0;
constexpr uint8_t high_0 = 0xB0;
constexpr uint8_t high_8 = 0xB8;
constexpr uint8_t high_a = 0xC1;
constexpr uint8_t high_b = 0xC2;
constexpr uint8_t high_h = 0xC8;
constexpr uint8_t high_e = 0xC5;
constexpr uint8_t high_i = 0xC9;
constexpr uint8_t high_k = 0xCB;
constexpr uint8_t high_l = 0xCC;
constexpr uint8_t high_n = 0xCE;
constexpr uint8_t high_o = 0xCF;

// HELLO and a Return as the caller supplies them, and what the sink sees: the
// card passes bit 7 through and the firmware appends LF after CR because
// DEFAULT set FLAGS bit 0.
constexpr std::array<uint8_t, 4> ello = {{high_e, high_l, high_l, high_o}};
constexpr std::array<uint8_t, 7> hello_cr_stream = {
    {high_h, high_e, high_l, high_l, high_o, high_cr, high_lf}};
constexpr std::array<uint8_t, 3> a_cr_stream = {{high_a, high_cr, high_lf}};
constexpr std::array<uint8_t, 6> tab_fill_stream = {
    {high_space, high_space, high_space, high_space, high_space, high_a}};
constexpr std::array<uint8_t, 5> tab_fill_after_release = {
    {high_space, high_space, high_space, high_space, high_a}};

// Typed at the Applesoft prompt of an Enhanced //e with no disk: PR#1 hooks
// CSW, Applesoft's CRDO and the prompt go to the card (8D 8A DD); GETLN echoes
// PRINT "HELLO" and its Return, the statement prints HELLO, CRDO and the
// prompt follow; GETLN echoes PR#0 and its Return, then PR#0 restores CSW so
// nothing more arrives.
constexpr std::array<uint8_t, 34> applesoft_session_stream = {
    {0x8D, 0x8A, 0xDD, 0xD0, 0xD2, 0xC9, 0xCE, 0xD4, 0xA0, 0xA2, 0xC8, 0xC5,
     0xCC, 0xCC, 0xCF, 0xA2, 0x8D, 0x8A, 0xC8, 0xC5, 0xCC, 0xCC, 0xCF, 0x8D,
     0x8A, 0x8D, 0x8A, 0xDD, 0xD0, 0xD2, 0xA3, 0xB0, 0x8D, 0x8A}};

// After Ctrl-I 80N with video off, the firmware never emits a CR of its own:
// it sets CH to 32..39 while COL is within eight of the width so that
// Applesoft's own line logic breaks the line, and to 0 otherwise.
constexpr int wide_width = 80;
constexpr int wide_first_lying_column = wide_width - 7;
constexpr uint8_t wide_first_lie = 0x20;

// Distinct cycles must give distinct bytes, so a read that asks the host for
// the bus can be told apart from any constant.
auto floating_bus_marker(uint32_t executed_cycles) -> uint8_t {
  return static_cast<uint8_t>((executed_cycles * 0x1D) ^ 0xA5);
}

auto hex(const std::vector<uint8_t>& bytes) -> std::string {
  std::string text;
  std::array<char, 4> digits{};
  for (uint8_t byte : bytes) {
    snprintf(digits.data(), digits.size(), "%02X ", byte);
    text += digits.data();
  }
  return text;
}

template <size_t N>
auto hex(const std::array<uint8_t, N>& bytes) -> std::string {
  return hex(std::vector<uint8_t>(bytes.begin(), bytes.end()));
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

// A host built by hand, for what the real one cannot show: a member missing,
// the log line, the page the card registers and the frame it writes.
class MockHost_t {
 public:
  MockHost_t() {
    s_active_host = this;
    REQUIRE(printer_descriptor() != nullptr);
    host_.Log = mock_log;
    host_.AssertIrq = mock_assert_irq;
    host_.RegisterIO = mock_register_io;
    host_.RegisterCxROM = mock_register_cx_rom;
    host_.RegisterExpansionROM = mock_register_expansion_rom;
    host_.RegisterDirectIO = mock_register_direct_io;
    host_.NotifyActivityChanged = mock_notify_activity_changed;
    host_.ReadFloatingBus = mock_read_floating_bus;
    host_.SinkOpen = mock_sink_open;
    host_.SinkWrite = mock_sink_write;
    host_.SinkReady = mock_sink_ready;
    host_.SinkClose = mock_sink_close;
  }

  ~MockHost_t() {
    for (const auto& entry : instances_) {
      if (entry.second != nullptr) {
        printer_descriptor()->shutdown(entry.second);
      }
    }
    instances_.clear();
    s_active_host = nullptr;
  }

  MockHost_t(const MockHost_t&) = delete;
  auto operator=(const MockHost_t&) -> MockHost_t& = delete;
  MockHost_t(MockHost_t&&) = delete;
  auto operator=(MockHost_t&&) -> MockHost_t& = delete;

  auto host() -> HostInterface_t* { return &host_; }

  auto create_printer(int slot) -> void* {
    void* instance = printer_descriptor()->init(slot, &host_);
    if (instance != nullptr) {
      instances_[slot] = instance;
      for (uint8_t i = 0; i < registers_per_slot; ++i) {
        auto it = handlers_.find(card_address(slot, i));
        if (it != handlers_.end()) {
          it->second.instance = instance;
        }
      }
    }
    return instance;
  }

  auto shutdown_instance(int slot) -> void {
    auto it = instances_.find(slot);
    if (it == instances_.end()) {
      return;
    }
    if (it->second != nullptr) {
      printer_descriptor()->shutdown(it->second);
    }
    instances_.erase(it);
    for (uint8_t i = 0; i < registers_per_slot; ++i) {
      handlers_.erase(card_address(slot, i));
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

  auto printed(int slot) const -> const std::vector<uint8_t>& {
    return sink(slot).bytes;
  }

  auto set_log_enabled(bool enabled) -> void {
    host_.Log = enabled ? mock_log : nullptr;
  }

  auto activity_changes(int slot) const -> size_t {
    auto it = activity_counts_.find(slot);
    return (it != activity_counts_.end()) ? it->second : 0;
  }

  auto read_slot_reg(int slot, uint8_t offset, uint32_t cycles = 0) -> uint8_t {
    const uint16_t addr = card_address(slot, offset);
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      return it->second.read(it->second.instance, 0, addr, 0, 0, cycles);
    }
    return 0;
  }

  auto write_slot_reg(int slot, uint8_t offset, uint8_t val) -> uint8_t {
    const uint16_t addr = card_address(slot, offset);
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      return it->second.write(it->second.instance, 0, addr, 1, val, 0);
    }
    return 0;
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

  static MockHost_t* s_active_host;

  static auto sink_from_token(void* token) -> MockSink_t* {
    if (s_active_host == nullptr || token == nullptr) {
      return nullptr;
    }
    for (MockSink_t& record : s_active_host->sinks_) {
      if (&record == token) {
        return &record;
      }
    }
    return nullptr;
  }

  static auto mock_notify_activity_changed(int slot, bool active) -> void {
    (void)active;
    if (s_active_host != nullptr) {
      s_active_host->activity_counts_[slot]++;
    }
  }

  // NOLINTBEGIN(cert-dcl50-cpp, cppcoreguidelines-pro-type-vararg)
  // Justification: Log is variadic in the HostInterface_t ABI.
  static auto mock_log(void* instance, PeripheralLogLevel_t level,
                       const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    if (s_active_host == nullptr) {
      return;
    }
    std::array<char, 256> text{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(text.data(), text.size(), fmt, args);
    va_end(args);
    s_active_host->log_messages_.emplace_back(text.data());
  }
  // NOLINTEND(cert-dcl50-cpp, cppcoreguidelines-pro-type-vararg)

  static auto mock_read_floating_bus(uint32_t executed_cycles) -> uint8_t {
    return floating_bus_marker(executed_cycles);
  }

  static auto mock_sink_open(void* instance, int slot,
                             PeripheralSinkKind_t kind) -> void* {
    (void)instance;
    if (s_active_host == nullptr || slot < 1 || slot > 7) {
      return nullptr;
    }
    MockSink_t& record = s_active_host->sinks_.at(static_cast<size_t>(slot));
    record.kind = kind;
    return &record;
  }

  static auto mock_sink_write(void* token, uint8_t byte) -> void {
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

  static auto mock_sink_ready(void* token) -> bool {
    MockSink_t* record = sink_from_token(token);
    if (record == nullptr) {
      return false;
    }
    record->ready_polls++;
    return record->ready;
  }

  static auto mock_sink_close(void* token) -> void {
    MockSink_t* record = sink_from_token(token);
    if (record != nullptr) {
      record->closes++;
    }
  }

  static auto mock_assert_irq(int slot, bool assert_irq) -> void {
    (void)slot;
    (void)assert_irq;
  }

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  // Justification: Signature is required by HostInterface_t ABI.
  static auto mock_register_io(int slot, PeripheralIOHandler read_c0,
                               PeripheralIOHandler write_c0,
                               PeripheralIOHandler read_cx,
                               PeripheralIOHandler write_cx) -> void {
    (void)read_cx;
    (void)write_cx;
    if (s_active_host != nullptr &&
        (read_c0 != nullptr || write_c0 != nullptr)) {
      for (uint8_t i = 0; i < registers_per_slot; ++i) {
        s_active_host->handlers_[card_address(slot, i)] = {nullptr, read_c0,
                                                           write_c0};
      }
    }
  }
  // NOLINTEND(bugprone-easily-swappable-parameters)

  static auto mock_register_cx_rom(int slot, const uint8_t* rom_ptr) -> void {
    if (s_active_host != nullptr && rom_ptr != nullptr) {
      std::vector<uint8_t> rom_data(slot_rom_size);
      std::copy_n(rom_ptr, slot_rom_size, rom_data.begin());
      s_active_host->roms_[slot] = std::move(rom_data);
      s_active_host->rom_history_[slot].push_back(rom_ptr);
    }
  }

  static auto mock_register_expansion_rom(int slot, uint8_t* rom_ptr) -> void {
    (void)slot;
    (void)rom_ptr;
  }

  static auto mock_register_direct_io(void* instance, uint16_t addr,
                                      PeripheralIOHandler read,
                                      PeripheralIOHandler write) -> void {
    if (s_active_host != nullptr) {
      s_active_host->handlers_[addr] = {instance, read, write};
    }
  }
};

MockHost_t* MockHost_t::s_active_host = nullptr;

auto save_frame(void* instance) -> Frame_t {
  Frame_t frame{};
  size_t size = frame.size();
  REQUIRE(printer_descriptor()->save_state(instance, frame.data(), &size) ==
          peripheral_ok);
  REQUIRE(size == frame_size);
  return frame;
}

// The card in a slot of an Enhanced //e booted the way a frontend boots it,
// its bytes caught by the test's own sink, and the 6502 driven into the
// firmware the way COUT enters it. The sink is the first member so that it
// is installed before the core builds the card and still there when the
// card's shutdown closes it.
struct PrinterHarness_t {
  static auto describe(int slot)
      -> TestFixtures::ScopedTestConfig_t::Description_t {
    TestFixtures::ScopedTestConfig_t::Description_t description;
    description.slots.at(static_cast<size_t>(slot - 1)) = "Parallel Printer";
    return description;
  }

  TestFixtures::ScopedByteSink_t sink;
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;
  int slot;

  explicit PrinterHarness_t(int in_slot = test_slot_1)
      : config(describe(in_slot)), core(config), slot(in_slot) {
    peripheral_manager_init();
    linapple_register_peripherals();
    linapple_reset_hard();
    preset_monitor_state();
  }

  static auto poke(uint16_t addr, uint8_t byte) -> void {
    TestFixtures::ScopedCore_t::poke(addr, &byte, 1);
  }

  // What the Monitor leaves for COUT1 on a booted machine, since the firmware
  // echoes through it: a full 40 by 24 window, the cursor at the top left
  // with its base address, normal video. RAM comes up FF FF 00 00, so the
  // holes DEFAULT never writes (MODE, MSTRT, COL) are cleared as a fresh
  // PR#1 finds them after a boot; PWDTH, ESCHAR and FLAGS are left for
  // DEFAULT to write.
  auto preset_monitor_state() -> void {
    poke(zp_wndlft, 0);
    poke(zp_wndwdth, text_columns);
    poke(zp_wndtop, 0);
    poke(zp_wndbtm, text_rows);
    poke(zp_ch, 0);
    poke(zp_cv, 0);
    poke(zp_basl, static_cast<uint8_t>(text_row_0 & 0xFF));
    poke(zp_bash, static_cast<uint8_t>(text_row_0 >> 8));
    poke(zp_invflg, 0xFF);
    poke(hole(hole_operand_mode, slot), 0);
    poke(hole(hole_operand_mstrt, slot), 0);
    poke(hole(hole_operand_col, slot), 0);
  }

  // What DEFAULT and then Ctrl-I N would leave: 40 columns, Ctrl-I, video
  // off with CRLF on.
  auto preset_video_off() -> void {
    poke(hole(hole_operand_pwdth, slot), default_width);
    poke(hole(hole_operand_eschar, slot), default_eschar);
    poke(hole(hole_operand_flags, slot), default_flags & 0x7F);
  }

  auto peek_hole(uint16_t operand) const -> uint8_t {
    return mem[hole(operand, slot)];
  }

  // Runs the 6502 until it reaches the sentinel or spends the cap, giving
  // the cards their think once per frame as a running machine would, so a
  // card that releases a wait from think gets to.
  template <typename Stop_t>
  auto run_while_not(Stop_t stopped, uint32_t cap) -> uint32_t {
    uint32_t cycles = 0;
    uint32_t since_think = 0;
    while (!stopped() && cycles < cap) {
      const uint32_t step = cpu_execute(0);
      cycles += step;
      since_think += step;
      if (since_think >= cycles_per_frame) {
        peripheral_manager_think(since_think);
        since_think = 0;
      }
    }
    return cycles;
  }

  auto run_until(uint16_t sentinel, uint32_t cap) -> uint32_t {
    const CpuRegisters_t* regs = cpu_get_registers();
    return run_while_not([regs, sentinel] { return regs->pc == sentinel; },
                         cap);
  }

  // Continues from wherever the machine stopped, registers and stack as they
  // are: the way a parked machine resumes when the printer comes back.
  auto resume_until(uint16_t sentinel, uint32_t cap) -> uint32_t {
    return run_until(sentinel, cap);
  }

  // Runs the 6502 into a routine the way a JSR would: the sentinel's return
  // address sits on the stack, so the routine's own RTS lands there and the
  // loop stops. Returns the cycles spent; the caller checks the PC reached
  // the sentinel, since the cap is what ends a runaway or a park.
  auto call_subroutine(uint16_t entry, uint8_t accumulator,
                       uint32_t cap = subroutine_cycle_cap) -> uint32_t {
    const uint16_t pushed = return_sentinel - 1;
    poke(stack_top - 1, static_cast<uint8_t>(pushed & 0xFF));
    poke(stack_top, static_cast<uint8_t>(pushed >> 8));

    CpuRegisters_t* regs = cpu_get_registers();
    regs->pc = entry;
    regs->sp = stack_top - 2;
    regs->a = accumulator;
    regs->x = 0;
    regs->y = 0;
    regs->ps = status_interrupts_masked;
    return run_until(return_sentinel, cap);
  }

  auto call_init_entry(uint8_t character, uint32_t cap = subroutine_cycle_cap)
      -> uint32_t {
    return call_subroutine(rom_address(slot, rom_entry_init), character, cap);
  }

  auto call_print_entry(uint8_t character, uint32_t cap = subroutine_cycle_cap)
      -> uint32_t {
    return call_subroutine(rom_address(slot, rom_entry_print), character, cap);
  }

  // Every character through the print entry, each one returning.
  template <size_t N>
  auto print_each(const std::array<uint8_t, N>& characters) -> void {
    for (uint8_t character : characters) {
      CAPTURE(character);
      call_print_entry(character);
      CHECK(cpu_get_registers()->pc == return_sentinel);
    }
  }

  // The sink's bytes, every one of them checked to have come from this slot.
  auto stream() const -> std::vector<uint8_t> {
    std::vector<uint8_t> bytes;
    for (const TestFixtures::ScopedByteSink_t::Byte_t& entry : sink.bytes()) {
      CHECK(entry.slot == slot);
      bytes.push_back(entry.byte);
    }
    return bytes;
  }

  auto latch() const -> uint8_t {
    Frame_t frame{};
    size_t size = frame.size();
    peripheral_save_state(slot, frame.data(), &size);
    REQUIRE(size == frame_size);
    return frame.at(frame_latch_offset);
  }

  // The two bytes the 6502 fetches at the plain-character wait: B0 00 from
  // the PROM, B0 FE while the card holds the machine.
  auto wait_bytes() const -> std::array<uint8_t, 2> {
    return {{mem[rom_address(slot, rom_wait_plain)],
             mem[rom_address(slot, rom_wait_plain + 1)]}};
  }

  static auto text_row_0_bytes() -> std::array<uint8_t, text_columns> {
    std::array<uint8_t, text_columns> row{};
    for (size_t i = 0; i < row.size(); ++i) {
      row.at(i) = mem[text_row_0 + i];
    }
    return row;
  }
};

constexpr std::array<uint8_t, 2> prom_wait_bytes = {{0xB0, 0x00}};
constexpr std::array<uint8_t, 2> parked_wait_bytes = {{0xB0, 0xFE}};

TEST_CASE(
    "Printer Peripheral: The descriptor names the A2B0002 and its entry "
    "points") {
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

TEST_CASE(
    "Printer Peripheral: init claims sixteen addresses and one ROM page") {
  MockHost_t host;
  void* instance = host.create_printer(test_slot_1);
  REQUIRE(instance != nullptr);

  for (uint8_t i = 0; i < registers_per_slot; ++i) {
    CAPTURE(i);
    const uint16_t addr = card_address(test_slot_1, i);
    CHECK(host.has_handler(addr));
    CHECK(host.get_handler(addr).read != nullptr);
    CHECK(host.get_handler(addr).write != nullptr);
  }

  REQUIRE(host.has_rom(test_slot_1));
  const auto& rom = host.get_rom(test_slot_1);
  CHECK(rom.size() == slot_rom_size);
  CHECK(rom.at(0) == rom_first_byte);
  CHECK(rom.at(slot_rom_size - 1) == rom_last_byte);
  CHECK(host.rom_registrations(test_slot_1) == 1);
}

TEST_CASE("Printer Peripheral: The slot ROM is the PROM, byte for byte") {
  MockHost_t host;
  REQUIRE(host.create_printer(test_slot_1) != nullptr);
  const uint8_t* registered = host.rom_pointer(test_slot_1);
  REQUIRE(registered != nullptr);

  const auto first_difference = std::mismatch(
      printer_rom_listing.begin(), printer_rom_listing.end(), registered);
  const size_t offset = static_cast<size_t>(
      std::distance(printer_rom_listing.begin(), first_difference.first));
  CAPTURE(offset);
  CHECK(first_difference.first == printer_rom_listing.end());

  // The wait images at $80/$82 (BCC * and BCS *), the one store to the card
  // at $84 (STA $C080,Y), and the live branches at $C0/$C2 they stand in for.
  CHECK(registered[0x80] == 0x90);
  CHECK(registered[0x81] == 0xFE);
  CHECK(registered[0x82] == 0xB0);
  CHECK(registered[0x83] == 0xFE);
  CHECK(registered[0x84] == 0x99);
  CHECK(registered[0x85] == 0x80);
  CHECK(registered[0x86] == 0xC0);
  CHECK(registered[rom_wait_blank] == 0x90);
  CHECK(registered[rom_wait_blank + 1] == 0x27);
  CHECK(registered[rom_wait_plain] == 0xB0);
  CHECK(registered[rom_wait_plain + 1] == 0x00);
}

TEST_CASE("Printer Peripheral: The wait image is the PROM with A6 forced") {
  MockHost_t host;
  void* instance = host.create_printer(test_slot_1);
  REQUIRE(instance != nullptr);
  const uint8_t* normal = host.rom_pointer(test_slot_1);
  REQUIRE(normal != nullptr);

  host.set_sink_ready(test_slot_1, false);
  host.write_slot_reg(test_slot_1, 0, high_a);
  REQUIRE(host.rom_registrations(test_slot_1) == 2);
  const uint8_t* waiting = host.rom_pointer(test_slot_1);
  REQUIRE(waiting != nullptr);
  CHECK(waiting != normal);

  const auto& image = host.get_rom(test_slot_1);
  for (size_t i = 0; i < wait_image_target; ++i) {
    CAPTURE(i);
    CHECK(image.at(i) == printer_rom_listing.at(i));
  }
  for (size_t i = 0; i < wait_image_length; ++i) {
    CAPTURE(i);
    CHECK(image.at(wait_image_target + i) ==
          printer_rom_listing.at(wait_image_source + i));
  }
  CHECK(image.at(rom_wait_blank) == 0x90);
  CHECK(image.at(rom_wait_blank + 1) == 0xFE);
  CHECK(image.at(rom_wait_plain) == 0xB0);
  CHECK(image.at(rom_wait_plain + 1) == 0xFE);
}

TEST_CASE("Printer Peripheral: A read answers with the host's floating bus") {
  MockHost_t host;
  REQUIRE(host.create_printer(test_slot_1) != nullptr);
  REQUIRE(floating_bus_marker(7) != floating_bus_marker(42));

  // R/W is not wired to the card, so a read is a strobe of whatever byte the
  // undriven bus holds, and the 6502 reads that same byte back.
  CHECK(host.read_slot_reg(test_slot_1, 5, 7) == floating_bus_marker(7));
  CHECK(host.read_slot_reg(test_slot_1, 5, 42) == floating_bus_marker(42));
  const std::vector<uint8_t> strobed = {floating_bus_marker(7),
                                        floating_bus_marker(42)};
  CHECK(hex(host.printed(test_slot_1)) == hex(strobed));
}

TEST_CASE(
    "Printer Peripheral: A strobe into a printer that is off drops the "
    "byte, swaps the page and says so once") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  MockHost_t host;
  void* instance = host.create_printer(test_slot_1);
  REQUIRE(instance != nullptr);
  const uint8_t* normal = host.rom_pointer(test_slot_1);
  REQUIRE(normal != nullptr);
  REQUIRE(host.log_messages().empty());

  // The byte goes out first, as DEVICE SELECT latches and strobes before the
  // firmware can learn anything; then the page swaps and the log says why.
  host.set_sink_ready(test_slot_1, false);
  host.write_slot_reg(test_slot_1, 0, high_a);
  CHECK(host.sink(test_slot_1).dropped == 1);
  CHECK(host.sink(test_slot_1).ready_polls == 1);
  CHECK(host.rom_registrations(test_slot_1) == 2);
  CHECK(host.rom_pointer(test_slot_1) != normal);
  REQUIRE(host.log_messages().size() == 1);
  CHECK(host.log_messages().back().find("slot 1") != std::string::npos);
  CHECK(host.log_messages().back().find("not ready") != std::string::npos);

  // Still not ready: think polls once per call and registers nothing more.
  descriptor->think(instance, cycles_per_frame);
  descriptor->think(instance, cycles_per_frame);
  CHECK(host.sink(test_slot_1).ready_polls == 3);
  CHECK(host.rom_registrations(test_slot_1) == 2);
  CHECK(host.log_messages().size() == 1);

  // Two more strobes into the parked printer: dropped, no new page, no new
  // line in the log.
  host.write_slot_reg(test_slot_1, 0, high_b);
  host.write_slot_reg(test_slot_1, 0, high_h);
  CHECK(host.sink(test_slot_1).dropped == 3);
  CHECK(host.rom_registrations(test_slot_1) == 2);
  CHECK(host.log_messages().size() == 1);

  // Ready again: the next think puts the PROM back so BCS * falls through.
  host.set_sink_ready(test_slot_1, true);
  descriptor->think(instance, cycles_per_frame);
  CHECK(host.rom_registrations(test_slot_1) == 3);
  CHECK(host.rom_pointer(test_slot_1) == normal);
  CHECK(host.log_messages().size() == 1);

  // Not waiting: think reads nothing and registers nothing.
  const size_t polls_before = host.sink(test_slot_1).ready_polls;
  descriptor->think(instance, cycles_per_frame);
  CHECK(host.sink(test_slot_1).ready_polls == polls_before);
  CHECK(host.rom_registrations(test_slot_1) == 3);

  host.write_slot_reg(test_slot_1, 0, high_e);
  CHECK(hex(host.printed(test_slot_1)) == hex(std::vector<uint8_t>{high_e}));
  CHECK(host.rom_registrations(test_slot_1) == 3);
}

TEST_CASE(
    "Printer Peripheral: RESET reaches neither the data register nor a "
    "wait in progress") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  MockHost_t host;
  void* instance = host.create_printer(test_slot_1);
  REQUIRE(instance != nullptr);
  const uint8_t* normal = host.rom_pointer(test_slot_1);

  host.write_slot_reg(test_slot_1, 0, high_h);
  host.set_sink_ready(test_slot_1, false);
  host.write_slot_reg(test_slot_1, 0, high_e);
  REQUIRE(host.rom_registrations(test_slot_1) == 2);
  const uint8_t* waiting = host.rom_pointer(test_slot_1);

  descriptor->reset(instance);
  CHECK(host.rom_registrations(test_slot_1) == 2);
  CHECK(host.rom_pointer(test_slot_1) == waiting);
  CHECK(host.log_messages().size() == 1);
  CHECK(save_frame(instance).at(frame_latch_offset) == high_e);

  descriptor->think(instance, cycles_per_frame);
  CHECK(host.rom_registrations(test_slot_1) == 2);

  host.set_sink_ready(test_slot_1, true);
  descriptor->think(instance, cycles_per_frame);
  CHECK(host.rom_registrations(test_slot_1) == 3);
  CHECK(host.rom_pointer(test_slot_1) == normal);
}

TEST_CASE(
    "Printer Peripheral: init refuses a null host, a slot outside 1 to "
    "7, and a handler with no card behind it does nothing") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  CHECK(descriptor->init(test_slot_1, nullptr) == nullptr);
  descriptor->reset(nullptr);
  descriptor->think(nullptr, 1024);
  descriptor->shutdown(nullptr);

  MockHost_t host;
  for (int slot : {0, 8, -1, 255}) {
    CAPTURE(slot);
    const size_t logged_before = host.log_messages().size();
    CHECK(descriptor->init(slot, host.host()) == nullptr);
    REQUIRE(host.log_messages().size() == logged_before + 1);
    CHECK(host.log_messages().back().find("slot " + std::to_string(slot)) !=
          std::string::npos);
  }

  void* instance = host.create_printer(test_slot_1);
  REQUIRE(instance != nullptr);
  descriptor->reset(instance);
  descriptor->think(instance, 1024);

  const uint16_t base = card_address(test_slot_1);
  const auto& handler = host.get_handler(base);
  CHECK(handler.read(nullptr, 0, base, 0, 0, 7) == 0);
  CHECK(handler.write(nullptr, 0, base, 1, high_a, 0) == 0);
  CHECK(host.printed(test_slot_1).empty());
  CHECK(host.sink(test_slot_1).dropped == 0);
}

TEST_CASE(
    "Printer Peripheral: A host missing a member gets no card, and hears "
    "why") {
  MockHost_t host;
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
    HostInterface_t partial = *host.host();
    member.strip(&partial);
    const size_t logged_before = host.log_messages().size();
    CHECK(printer_descriptor()->init(test_slot_2, &partial) == nullptr);
    REQUIRE(host.log_messages().size() == logged_before + 1);
    CHECK(host.log_messages().back().find(member.name) != std::string::npos);
    CHECK(host.log_messages().back().find("slot 2") != std::string::npos);
  }

  // A host that cannot even log is still refused, silently.
  HostInterface_t mute = *host.host();
  mute.Log = nullptr;
  mute.SinkReady = nullptr;
  const size_t logged_before = host.log_messages().size();
  CHECK(printer_descriptor()->init(test_slot_2, &mute) == nullptr);
  CHECK(host.log_messages().size() == logged_before);

  CHECK(host.create_printer(test_slot_1) != nullptr);
}

TEST_CASE(
    "Printer Peripheral: A host with nowhere to log still gets the wait, "
    "silently") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  MockHost_t host;
  void* instance = host.create_printer(test_slot_1);
  REQUIRE(instance != nullptr);
  host.set_log_enabled(false);

  CHECK(host.write_slot_reg(test_slot_1, 0, high_h) == high_h);
  host.set_sink_ready(test_slot_1, false);
  host.write_slot_reg(test_slot_1, 0, high_e);
  CHECK(host.rom_registrations(test_slot_1) == 2);
  CHECK(host.log_messages().empty());
  descriptor->think(instance, 1000);
  descriptor->reset(instance);
  host.set_sink_ready(test_slot_1, true);
  descriptor->think(instance, 1000);
  CHECK(host.rom_registrations(test_slot_1) == 3);
  CHECK(host.activity_changes(test_slot_1) == 0);
}

TEST_CASE(
    "Printer Peripheral: The retired command and query ids answer "
    "incompatible") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  MockHost_t host;
  void* instance = host.create_printer(test_slot_1);
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

TEST_CASE(
    "Printer Peripheral: save_state sizes, refuses and copies the frame "
    "as the ABI says") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  MockHost_t host;
  void* instance = host.create_printer(test_slot_1);
  REQUIRE(instance != nullptr);

  size_t required_size = 0;
  CHECK(descriptor->save_state(instance, nullptr, &required_size) ==
        peripheral_ok);
  CHECK(required_size == frame_size);
  CHECK(descriptor->save_state(instance, nullptr, nullptr) == peripheral_error);

  std::array<uint8_t, frame_size - 1> undersized{};
  size_t too_small = undersized.size();
  CHECK(descriptor->save_state(instance, undersized.data(), &too_small) ==
        peripheral_error);

  Frame_t frame = frame_after_123;
  size_t size = frame.size();
  CHECK(descriptor->save_state(nullptr, frame.data(), &size) ==
        peripheral_error);
  CHECK(descriptor->load_state(nullptr, frame.data(), frame.size()) ==
        peripheral_error);
  CHECK(descriptor->load_state(instance, nullptr, frame.size()) ==
        peripheral_error);

  CHECK(descriptor->load_state(instance, frame.data(), frame.size()) ==
        peripheral_ok);
  CHECK(save_frame(instance) == frame_after_123);
}

TEST_CASE(
    "Printer Peripheral: The frame after 123 is the literal, an old "
    "frame loads, and a rejected one changes nothing") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  MockHost_t host;
  void* instance = host.create_printer(test_slot_1);
  REQUIRE(instance != nullptr);

  host.write_slot_reg(test_slot_1, 0, '1');
  host.write_slot_reg(test_slot_1, 0, '2');
  host.write_slot_reg(test_slot_1, 0, '3');
  CHECK(save_frame(instance) == frame_after_123);

  // A frame from the card as it was loads, and comes back with the dead
  // fields zeroed.
  CHECK(descriptor->load_state(instance, frame_from_old_card.data(),
                               frame_from_old_card.size()) == peripheral_ok);
  CHECK(save_frame(instance) == frame_after_123);

  // A slot buffer larger than the frame loads what the frame says it holds.
  std::array<uint8_t, 32> larger{};
  std::copy(frame_after_123.begin(), frame_after_123.end(), larger.begin());
  larger.at(frame_latch_offset) = 0x44;
  CHECK(descriptor->load_state(instance, larger.data(), larger.size()) ==
        peripheral_ok);
  CHECK(save_frame(instance).at(frame_latch_offset) == 0x44);
  CHECK(descriptor->load_state(instance, frame_after_123.data(),
                               frame_after_123.size()) == peripheral_ok);

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
    Frame_t frame = frame_after_123;
    frame.at(frame_latch_offset) = 0x55;
    frame.at(reject.offset) = reject.value;
    CHECK(descriptor->load_state(instance, frame.data(), frame.size()) ==
          peripheral_error);
    CHECK(save_frame(instance) == frame_after_123);
  }
  for (size_t short_size : {23U, 7U, 0U}) {
    CAPTURE(short_size);
    Frame_t frame = frame_after_123;
    frame.at(frame_latch_offset) = 0x55;
    CHECK(descriptor->load_state(instance, frame.data(), short_size) ==
          peripheral_error);
    CHECK(save_frame(instance) == frame_after_123);
  }
  CHECK(descriptor->load_state(instance, nullptr, frame_size) ==
        peripheral_error);
  CHECK(descriptor->load_state(nullptr, frame_after_123.data(), frame_size) ==
        peripheral_error);
  CHECK(save_frame(instance) == frame_after_123);
}

TEST_CASE("Printer Peripheral: A load puts the PROM back, whatever was shown") {
  auto* descriptor = printer_descriptor();
  REQUIRE(descriptor != nullptr);

  MockHost_t host;
  void* instance = host.create_printer(test_slot_1);
  REQUIRE(instance != nullptr);
  const uint8_t* normal = host.rom_pointer(test_slot_1);

  host.set_sink_ready(test_slot_1, false);
  host.write_slot_reg(test_slot_1, 0, high_a);
  REQUIRE(host.rom_pointer(test_slot_1) != normal);

  // The frame carries no wait, so the loaded machine shows the PROM; the
  // next strobe samples the sink again and parks once more, as it must.
  CHECK(descriptor->load_state(instance, frame_after_123.data(),
                               frame_after_123.size()) == peripheral_ok);
  CHECK(host.rom_pointer(test_slot_1) == normal);
  CHECK(host.rom_registrations(test_slot_1) == 3);

  host.write_slot_reg(test_slot_1, 0, high_b);
  CHECK(host.sink(test_slot_1).dropped == 2);
  CHECK(host.rom_pointer(test_slot_1) != normal);
  CHECK(host.rom_registrations(test_slot_1) == 4);
}

// The cases below put the card in a real Enhanced //e and run Apple's PROM
// on its 6502, catching the bytes at the host's sink.

TEST_CASE(
    "Printer Firmware: HELLO and a Return through PR#1 reach the sink "
    "as C8 C5 CC CC CF 8D 8A") {
  PrinterHarness_t machine;
  // The firmware learns its slot from the return address JSR $FF58 leaves on
  // the stack, which only works because that Monitor byte is an RTS.
  REQUIRE(mem[monitor_iorts] == opcode_rts);
  REQUIRE(machine.peek_hole(hole_operand_pwdth) != default_width);
  REQUIRE(machine.peek_hole(hole_operand_flags) != default_flags);

  // PR#1 enters $C100 for its first character: DEFAULT writes the holes and
  // CSWL before H goes out, and H is echoed to the screen since video-also
  // is on.
  const uint32_t first_cycles = machine.call_init_entry(high_h);
  CHECK(cpu_get_registers()->pc == return_sentinel);
  CHECK(first_cycles < subroutine_cycle_cap);
  CHECK(machine.peek_hole(hole_operand_pwdth) == default_width);
  CHECK(machine.peek_hole(hole_operand_eschar) == default_eschar);
  CHECK(machine.peek_hole(hole_operand_flags) == default_flags);
  CHECK(mem[zp_cswl] == cswl_after_default);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{high_h}));
  CHECK(machine.peek_hole(hole_operand_col) == 1);
  CHECK(mem[zp_ch] == 1);

  machine.print_each(ello);
  CHECK(machine.peek_hole(hole_operand_col) == 5);
  CHECK(mem[zp_ch] == 5);

  // $8D ^ $07 ^ $0A is $80, and ASL of that is zero: the firmware clears CH
  // and COL, then FLAGS bit 0 sends the LF it kept in Y.
  machine.call_print_entry(high_cr);
  CHECK(cpu_get_registers()->pc == return_sentinel);
  CHECK(hex(machine.stream()) == hex(hello_cr_stream));
  CHECK(machine.peek_hole(hole_operand_col) == 0);
  CHECK(mem[zp_ch] == 0);
  CHECK(mem[zp_cv] == 1);

  const auto row = PrinterHarness_t::text_row_0_bytes();
  CHECK(hex(std::vector<uint8_t>(row.begin(), row.begin() + 5)) ==
        hex(std::vector<uint8_t>{high_h, high_e, high_l, high_l, high_o}));

  // Seven strobes, one readiness sample each, nothing dropped, the PROM
  // still in place.
  CHECK(machine.sink.dropped() == 0);
  CHECK(machine.sink.ready_polls() == hello_cr_stream.size());
  CHECK(machine.wait_bytes() == prom_wait_bytes);
  CHECK(machine.latch() == high_lf);
}

TEST_CASE("Printer Firmware: Printing is not disk activity") {
  PrinterHarness_t machine;
  REQUIRE_FALSE(peripheral_is_any_active());

  machine.call_init_entry(high_h);
  machine.print_each(ello);
  machine.call_print_entry(high_cr);
  CHECK(hex(machine.stream()) == hex(hello_cr_stream));
  CHECK_FALSE(peripheral_is_any_active());
}

TEST_CASE(
    "Printer Firmware: Ctrl-I 80N sets an 80-column width with video "
    "off and emits nothing") {
  PrinterHarness_t machine;

  // Ctrl-I as the first character through $C100: DEFAULT runs, then the
  // character matches the escape character it just set and opens a command.
  machine.call_init_entry(high_ctrl_i);
  CHECK(cpu_get_registers()->pc == return_sentinel);
  CHECK(machine.peek_hole(hole_operand_pwdth) == default_width);
  CHECK(machine.peek_hole(hole_operand_flags) == default_flags);
  CHECK(machine.peek_hole(hole_operand_mode) == mode_after_escape);
  CHECK(machine.peek_hole(hole_operand_mstrt) == 0);
  CHECK(machine.stream().empty());

  // Each digit is added to ten times the accumulator: 8, then 80 = $50.
  machine.call_print_entry(high_8);
  CHECK(machine.peek_hole(hole_operand_pwdth) == 8);
  CHECK(machine.peek_hole(hole_operand_mstrt) == 8);
  machine.call_print_entry(high_0);
  CHECK(machine.peek_hole(hole_operand_pwdth) == 0x50);
  CHECK(machine.peek_hole(hole_operand_mstrt) == 0x50);

  // N: ROR of $7E gives $3F with carry clear, AND FLAGS $89 gives $09, and
  // the clear carry skips the EOR that would turn video back on.
  machine.call_print_entry(high_n);
  CHECK(machine.peek_hole(hole_operand_flags) == 0x09);
  CHECK((machine.peek_hole(hole_operand_mode) & 0x80) == 0);
  CHECK(machine.stream().empty());

  // With video off nothing reaches the screen, and COL 1 - 80 - 1 is far
  // from the width, so CH is set to 0.
  const auto row_before = PrinterHarness_t::text_row_0_bytes();
  machine.call_print_entry(high_a);
  machine.call_print_entry(high_cr);
  CHECK(hex(machine.stream()) == hex(a_cr_stream));
  CHECK(mem[zp_ch] == 0);
  CHECK(machine.peek_hole(hole_operand_col) == 0);
  CHECK(PrinterHarness_t::text_row_0_bytes() == row_before);
}

TEST_CASE(
    "Printer Firmware: Ctrl-I K turns the line feed off and Ctrl-I I "
    "turns it back on") {
  PrinterHarness_t machine;

  // K: ROR of $7B gives $3D with carry set, AND FLAGS $89 gives $09, and the
  // carry takes the EOR #$81: video stays on, CRLF goes off.
  machine.call_init_entry(high_ctrl_i);
  machine.call_print_entry(high_k);
  CHECK(machine.peek_hole(hole_operand_flags) == 0x88);
  CHECK((machine.peek_hole(hole_operand_mode) & 0x80) == 0);
  CHECK(machine.stream().empty());

  machine.call_print_entry(high_cr);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{high_cr}));
  CHECK(machine.peek_hole(hole_operand_col) == 0);

  // I: ROR of $79 gives $3C with carry set, AND FLAGS $88 gives $08, EOR #$81
  // gives $89 again.
  machine.call_print_entry(high_ctrl_i);
  machine.call_print_entry(high_i);
  CHECK(machine.peek_hole(hole_operand_flags) == default_flags);
  machine.call_print_entry(high_cr);
  CHECK(hex(machine.stream()) ==
        hex(std::vector<uint8_t>{high_cr, high_cr, high_lf}));
}

TEST_CASE(
    "Printer Firmware: A printer that is not ready takes one byte and "
    "parks the 6502 at $C1C2 until it is") {
  PrinterHarness_t machine;
  machine.sink.set_ready(false);

  // The firmware's only busy test is the branch at $C1C2, fetched before the
  // store: H is latched and strobed into the off printer, and only then does
  // the card present the wait image. The first call therefore returns.
  const uint32_t first_cycles = machine.call_init_entry(high_h, park_cycle_cap);
  CHECK(cpu_get_registers()->pc == return_sentinel);
  CHECK(first_cycles < park_cycle_cap);
  CHECK(machine.sink.dropped() == 1);
  CHECK(machine.sink.ready_polls() == 1);
  CHECK(machine.wait_bytes() == parked_wait_bytes);
  CHECK(machine.stream().empty());

  // E finds B0 FE at $C1C2 and spins there; no think runs inside 3,000
  // cycles, so the one poll is still the first strobe's own sample.
  const uint32_t parked_cycles =
      machine.call_print_entry(high_e, park_cycle_cap);
  CHECK(cpu_get_registers()->pc == rom_address(test_slot_1, rom_wait_plain));
  CHECK(parked_cycles >= park_cycle_cap);
  CHECK(machine.sink.dropped() == 1);
  CHECK(machine.sink.ready_polls() == 1);
  CHECK(machine.stream().empty());

  // RESET on the slot reaches no flip-flop in this model: the wait stays.
  peripheral_manager_reset();
  machine.resume_until(return_sentinel, 1000);
  CHECK(cpu_get_registers()->pc == rom_address(test_slot_1, rom_wait_plain));
  CHECK(machine.wait_bytes() == parked_wait_bytes);
  CHECK(machine.sink.dropped() == 1);
  CHECK(machine.sink.ready_polls() == 1);

  // The printer comes back: the card's next think puts the PROM back, the
  // spinning BCS falls through, and E goes out with the registers and stack
  // exactly as the park left them.
  machine.sink.set_ready(true);
  peripheral_manager_think(0);
  CHECK(machine.sink.ready_polls() == 2);
  CHECK(machine.wait_bytes() == prom_wait_bytes);
  machine.resume_until(return_sentinel, subroutine_cycle_cap);
  CHECK(cpu_get_registers()->pc == return_sentinel);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{high_e}));

  machine.print_each(std::array<uint8_t, 4>{{high_l, high_l, high_o, high_cr}});
  CHECK(hex(machine.stream()) ==
        hex(std::vector<uint8_t>{high_e, high_l, high_l, high_o, high_cr,
                                 high_lf}));
  CHECK(machine.sink.dropped() == 1);
  CHECK(machine.peek_hole(hole_operand_col) == 0);
}

TEST_CASE(
    "Printer Firmware: Ctrl-Reset while parked leaves the wait and the "
    "latch in place") {
  PrinterHarness_t machine;
  machine.sink.set_ready(false);
  machine.call_init_entry(high_h, park_cycle_cap);
  machine.call_print_entry(high_e, park_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == rom_address(test_slot_1, rom_wait_plain));
  REQUIRE(machine.sink.dropped() == 1);

  // Ctrl-Reset pulls the 6502 out of the spin and RESET reaches every slot;
  // the Monitor then restores CSW, so nothing more goes to the card. The //e
  // does part of its reset from the internal Cx ROM, so the slot's page is
  // read only once the Monitor has switched the slot ROMs back in.
  linapple_reset_soft();
  peripheral_manager_reset();
  CHECK(cpu_get_registers()->pc == monitor_reset);
  const uint32_t reset_cycles = machine.run_while_not(
      [] { return mem[zp_cswl] == cout1_low && mem[zp_cswh] == cout1_high; },
      reset_cycle_cap);
  CHECK(reset_cycles < reset_cycle_cap);
  CHECK(mem[zp_cswl] == cout1_low);
  CHECK(mem[zp_cswh] == cout1_high);
  const uint32_t internal_rom_cycles = machine.run_while_not(
      [] { return mem[rom_address(test_slot_1, 0)] == rom_first_byte; },
      reset_cycle_cap - reset_cycles);
  CHECK(reset_cycles + internal_rom_cycles < reset_cycle_cap);

  // The data register keeps H (its clear pin is tied high), the byte E that
  // never went out is not counted, and the card still shows the wait image
  // until the printer is ready.
  CHECK(machine.latch() == high_h);
  CHECK(machine.sink.dropped() == 1);
  CHECK(machine.stream().empty());
  CHECK(machine.wait_bytes() == parked_wait_bytes);

  machine.sink.set_ready(true);
  peripheral_manager_think(0);
  CHECK(machine.wait_bytes() == prom_wait_bytes);
}

TEST_CASE(
    "Printer Firmware: Ctrl-Reset inside an escape sequence abandons it "
    "without eating a character") {
  PrinterHarness_t machine;

  // As Ctrl-I 80 then Ctrl-Reset then PR#1 leave the holes: a command open
  // with 80 accumulated, three characters on the line, and the next
  // character entering $C100 because the Monitor restored CSW.
  PrinterHarness_t::poke(hole(hole_operand_mode, test_slot_1),
                         mode_after_escape);
  PrinterHarness_t::poke(hole(hole_operand_mstrt, test_slot_1), 0x50);
  PrinterHarness_t::poke(hole(hole_operand_col, test_slot_1), 3);
  peripheral_manager_reset();

  // The $C100 path skips the MODE test, DEFAULT rewrites its three holes, and
  // LSR MODE,X at $C1DE clears bit 7 before the character prints; MSTRT and
  // COL keep their stale values until the next escape or CR.
  machine.call_init_entry(high_h);
  CHECK(cpu_get_registers()->pc == return_sentinel);
  CHECK(machine.peek_hole(hole_operand_pwdth) == default_width);
  CHECK(machine.peek_hole(hole_operand_eschar) == default_eschar);
  CHECK(machine.peek_hole(hole_operand_flags) == default_flags);
  CHECK(machine.peek_hole(hole_operand_mode) == 0x40);
  CHECK(machine.peek_hole(hole_operand_mstrt) == 0x50);
  CHECK(machine.peek_hole(hole_operand_col) == 4);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{high_h}));
}

TEST_CASE(
    "Printer Firmware: A read of the card strobes the floating bus and "
    "returns it") {
  PrinterHarness_t machine;

  // The bus byte is whatever the video scanner is fetching on that cycle, so
  // a marker placed at the scanner's address for the cycle is what a read on
  // that cycle returns and what the printer receives.
  constexpr uint32_t first_cycle = 42;
  constexpr uint32_t second_cycle = 100;
  constexpr uint8_t first_marker = 0x5A;
  constexpr uint8_t second_marker = 0x3C;
  const uint16_t first_fetch = video_get_scanner_address(nullptr, first_cycle);
  const uint16_t second_fetch =
      video_get_scanner_address(nullptr, second_cycle);
  REQUIRE(first_fetch != second_fetch);
  PrinterHarness_t::poke(first_fetch, first_marker);
  PrinterHarness_t::poke(second_fetch, second_marker);

  CHECK(io_map_dispatch(0, card_address(test_slot_1, 5), 0, 0, first_cycle) ==
        first_marker);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{first_marker}));
  CHECK(machine.latch() == first_marker);

  CHECK(io_map_dispatch(0, card_address(test_slot_1, 5), 0, 0, second_cycle) ==
        second_marker);
  CHECK(hex(machine.stream()) ==
        hex(std::vector<uint8_t>{first_marker, second_marker}));
  CHECK(machine.latch() == second_marker);
  CHECK(machine.sink.dropped() == 0);
  CHECK(machine.wait_bytes() == prom_wait_bytes);
}

TEST_CASE(
    "Printer Firmware: Every one of the sixteen addresses strobes, "
    "written or read") {
  PrinterHarness_t machine;
  std::vector<uint8_t> expected;
  for (uint8_t offset = 0; offset < registers_per_slot; ++offset) {
    CAPTURE(offset);
    const uint8_t byte = static_cast<uint8_t>(high_a + offset);
    CHECK(io_map_dispatch(0, card_address(test_slot_1, offset), 1, byte, 0) ==
          byte);
    expected.push_back(byte);
  }
  CHECK(hex(machine.stream()) == hex(expected));

  for (uint8_t offset = 0; offset < registers_per_slot; ++offset) {
    CAPTURE(offset);
    const uint32_t cycle = 100 + (offset * 3U);
    const uint8_t marker = static_cast<uint8_t>(0x60 + offset);
    PrinterHarness_t::poke(video_get_scanner_address(nullptr, cycle), marker);
    CHECK(io_map_dispatch(0, card_address(test_slot_1, offset), 0, 0, cycle) ==
          marker);
    expected.push_back(marker);
  }
  CHECK(hex(machine.stream()) == hex(expected));
  CHECK(machine.stream().size() == 32);
}

TEST_CASE(
    "Printer Firmware: Every byte value goes out as written, bit 7 "
    "included") {
  PrinterHarness_t machine;
  std::vector<uint8_t> all_bytes(256);
  for (size_t i = 0; i < all_bytes.size(); ++i) {
    CAPTURE(i);
    all_bytes.at(i) = static_cast<uint8_t>(i);
    CHECK(io_map_dispatch(
              0, card_address(test_slot_1, static_cast<uint8_t>(i % 16)), 1,
              all_bytes.at(i), 0) == all_bytes.at(i));
  }
  CHECK(hex(machine.stream()) == hex(all_bytes));
  CHECK(machine.sink.dropped() == 0);
}

// The firmware learns its slot from the return address JSR $FF58 leaves on
// the stack: X = $Cn indexes the holes, and Y = $n0 lands STA $C080,Y on the
// slot's own sixteen addresses.
auto hello_in_slot(int slot) -> void {
  PrinterHarness_t machine(slot);
  REQUIRE(mem[rom_address(slot, 0)] == rom_first_byte);

  machine.call_init_entry(high_h);
  CHECK(cpu_get_registers()->pc == return_sentinel);
  CHECK(mem[hole(hole_operand_pwdth, slot)] == default_width);
  CHECK(mem[hole(hole_operand_eschar, slot)] == default_eschar);
  CHECK(mem[hole(hole_operand_flags, slot)] == default_flags);
  CHECK(mem[zp_cswl] == cswl_after_default);

  machine.print_each(ello);
  CHECK(mem[hole(hole_operand_col, slot)] == 5);
  machine.call_print_entry(high_cr);
  CHECK(mem[hole(hole_operand_col, slot)] == 0);
  CHECK(hex(machine.stream()) == hex(hello_cr_stream));
  CHECK(machine.latch() == high_lf);
  CHECK(machine.sink.dropped() == 0);
}

TEST_CASE(
    "Printer Firmware: The firmware finds its slot from the return "
    "address, in slot 2 and slot 7") {
  SUBCASE("slot 2: $C200, $C0A0, holes $057A to $07FA") {
    hello_in_slot(test_slot_2);
    CHECK(hole(hole_operand_pwdth, test_slot_2) == 0x057A);
    CHECK(hole(hole_operand_mstrt, test_slot_2) == 0x05FA);
    CHECK(hole(hole_operand_mode, test_slot_2) == 0x067A);
    CHECK(hole(hole_operand_eschar, test_slot_2) == 0x06FA);
    CHECK(hole(hole_operand_flags, test_slot_2) == 0x077A);
    CHECK(hole(hole_operand_col, test_slot_2) == 0x07FA);
    CHECK(card_address(test_slot_2) == 0xC0A0);
  }
  SUBCASE("slot 7: $C700, $C0F0, holes $057F to $07FF") {
    hello_in_slot(test_slot_7);
    CHECK(hole(hole_operand_pwdth, test_slot_7) == 0x057F);
    CHECK(hole(hole_operand_col, test_slot_7) == 0x07FF);
    CHECK(card_address(test_slot_7) == 0xC0F0);
    CHECK(rom_address(test_slot_7, 0) == 0xC700);
  }
}

TEST_CASE(
    "Printer Firmware: Two cards keep their bytes apart and one can "
    "leave") {
  TestFixtures::ScopedByteSink_t sink;
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots.at(0) = "Parallel Printer";
  description.slots.at(1) = "Parallel Printer";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  peripheral_manager_init();
  linapple_register_peripherals();
  linapple_reset_hard();

  io_map_dispatch(0, card_address(test_slot_1), 1, '1', 0);
  io_map_dispatch(0, card_address(test_slot_2), 1, '2', 0);
  io_map_dispatch(0, card_address(test_slot_1), 1, 'A', 0);
  REQUIRE(sink.bytes().size() == 3);
  CHECK(sink.bytes().at(0).slot == test_slot_1);
  CHECK(sink.bytes().at(0).byte == '1');
  CHECK(sink.bytes().at(1).slot == test_slot_2);
  CHECK(sink.bytes().at(1).byte == '2');
  CHECK(sink.bytes().at(2).slot == test_slot_1);
  CHECK(sink.bytes().at(2).byte == 'A');

  // Pulling slot 1's card closes its sink once; slot 2 keeps printing.
  CHECK(sink.closes() == 0);
  CHECK(peripheral_unregister(test_slot_1) == 0);
  CHECK(sink.closes() == 1);
  io_map_dispatch(0, card_address(test_slot_2), 1, 'B', 0);
  REQUIRE(sink.bytes().size() == 4);
  CHECK(sink.bytes().back().slot == test_slot_2);
  CHECK(sink.bytes().back().byte == 'B');
}

TEST_CASE(
    "Printer Firmware: The tab fill sends blanks up to CH and waits at "
    "$C1C0 before each one") {
  PrinterHarness_t machine;
  machine.preset_video_off();
  PrinterHarness_t::poke(zp_ch, 5);

  SUBCASE("with the printer ready, five blanks then the character") {
    machine.call_print_entry(high_a);
    CHECK(cpu_get_registers()->pc == return_sentinel);
    CHECK(hex(machine.stream()) == hex(tab_fill_stream));
    CHECK(machine.peek_hole(hole_operand_col) == 6);
    CHECK(mem[zp_ch] == 0);
    CHECK(machine.sink.dropped() == 0);
  }

  SUBCASE(
      "with the printer off, the first blank is lost and the machine "
      "parks on BCC") {
    machine.sink.set_ready(false);
    const uint32_t parked_cycles =
        machine.call_print_entry(high_a, park_cycle_cap);
    CHECK(cpu_get_registers()->pc == rom_address(test_slot_1, rom_wait_blank));
    CHECK((cpu_get_registers()->ps & status_carry) == 0);
    CHECK(parked_cycles >= park_cycle_cap);
    CHECK(machine.sink.dropped() == 1);
    CHECK(machine.sink.ready_polls() == 1);
    CHECK(machine.peek_hole(hole_operand_col) == 1);

    machine.sink.set_ready(true);
    peripheral_manager_think(0);
    machine.resume_until(return_sentinel, subroutine_cycle_cap);
    CHECK(cpu_get_registers()->pc == return_sentinel);
    CHECK(hex(machine.stream()) == hex(tab_fill_after_release));
    CHECK(machine.peek_hole(hole_operand_col) == 6);
    CHECK(machine.sink.dropped() == 1);
  }
}

TEST_CASE(
    "Printer Firmware: Ctrl-I followed by a letter outside H to O makes "
    "it the escape character") {
  PrinterHarness_t machine;
  machine.call_init_entry(high_ctrl_i);
  CHECK(machine.peek_hole(hole_operand_mode) == mode_after_escape);

  // A: $41 ^ $30 is $71, neither a digit nor H..O nor CR, so AND #$9F forces
  // it into the control range and stores Ctrl-A as the new escape character.
  machine.call_print_entry(high_a);
  CHECK(machine.peek_hole(hole_operand_eschar) == high_ctrl_a);
  CHECK((machine.peek_hole(hole_operand_mode) & 0x80) == 0);
  CHECK(machine.stream().empty());

  // Ctrl-I no longer matches, so it is printed as the control code it is.
  machine.call_print_entry(high_ctrl_i);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{high_ctrl_i}));
  CHECK(machine.peek_hole(hole_operand_col) == 0);

  machine.call_print_entry(high_ctrl_a);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{high_ctrl_i}));
  CHECK((machine.peek_hole(hole_operand_mode) & 0x80) != 0);
  CHECK(machine.peek_hole(hole_operand_mstrt) == 0);
}

TEST_CASE(
    "Printer Firmware: With video on, a character past column 39 is "
    "not echoed and CH is cleared") {
  PrinterHarness_t machine;
  machine.call_init_entry(high_h);
  REQUIRE(hex(machine.stream()) == hex(std::vector<uint8_t>{high_h}));

  // COL is moved with CH, as the firmware keeps them when it echoes, or the
  // fill would send blanks up to CH first. LDA #$27 / CMP CH with CH at 40
  // clears the carry: the firmware sets CH to 0 and returns without JMP
  // COUT1.
  PrinterHarness_t::poke(zp_ch, text_columns);
  PrinterHarness_t::poke(hole(hole_operand_col, test_slot_1), text_columns);
  const auto row_before = PrinterHarness_t::text_row_0_bytes();
  machine.call_print_entry(high_a);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{high_h, high_a}));
  CHECK(mem[zp_ch] == 0);
  CHECK(machine.peek_hole(hole_operand_col) == text_columns + 1);
  CHECK(PrinterHarness_t::text_row_0_bytes() == row_before);

  // At 39 the echo goes out to the last column, and COUT1's own advance
  // wraps the cursor.
  PrinterHarness_t::poke(zp_ch, text_columns - 1);
  PrinterHarness_t::poke(hole(hole_operand_col, test_slot_1), text_columns - 1);
  machine.call_print_entry(high_b);
  CHECK(hex(machine.stream()) ==
        hex(std::vector<uint8_t>{high_h, high_a, high_b}));
  CHECK(mem[text_row_0 + text_columns - 1] == high_b);
  CHECK(mem[zp_ch] == 0);
  CHECK(mem[zp_cv] == 1);
}

TEST_CASE(
    "Printer Firmware: After Ctrl-I 80N the firmware lies about CH "
    "within eight columns of the width") {
  PrinterHarness_t machine;
  machine.call_init_entry(high_ctrl_i);
  machine.print_each(std::array<uint8_t, 3>{{high_8, high_0, high_n}});
  REQUIRE(machine.peek_hole(hole_operand_pwdth) == wide_width);
  REQUIRE(machine.stream().empty());

  // COL - PWDTH - 1 (SBC with the carry clear from the video test) is below
  // $F8 until COL reaches 73, where it is $F8 and ADC #$27 with the carry
  // from CMP gives $20; at COL 80 it is $FF and gives $27; at 81 it wraps to
  // 0 and CH is cleared again.
  for (int column = 1; column <= wide_width + 1; ++column) {
    CAPTURE(column);
    machine.call_print_entry(high_a);
    CHECK(cpu_get_registers()->pc == return_sentinel);
    CHECK(machine.peek_hole(hole_operand_col) == column);
    uint8_t expected_ch = 0;
    if (column >= wide_first_lying_column && column <= wide_width) {
      expected_ch = static_cast<uint8_t>(wide_first_lie +
                                         (column - wide_first_lying_column));
    }
    CHECK(mem[zp_ch] == expected_ch);
  }
  CHECK(machine.stream().size() == static_cast<size_t>(wide_width + 1));
  CHECK(machine.sink.dropped() == 0);
}

// The frontend's own controller builds this machine, so the sink guard is
// constructed after it: whatever sink the controller installs, the guard
// takes over and puts back.
TEST_CASE(
    "Printer Firmware: PR#1, PRINT \"HELLO\" and PR#0 at the Applesoft "
    "prompt stream 34 bytes") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots.at(0) = "Parallel Printer";
  TestFixtures::ScopedTestConfig_t config(description);
  HeadlessHarness_t harness(config);
  TestFixtures::ScopedByteSink_t sink;

  // No disk controller, so the Autostart scan falls through to Applesoft.
  harness.boot();
  constexpr uint32_t prompt_frame_cap = 300;
  bool at_prompt = false;
  uint32_t frames = 0;
  while (!at_prompt && frames < prompt_frame_cap) {
    harness.run_frames(1);
    ++frames;
    for (int row = 0; row < text_rows; ++row) {
      if (harness.get_text_row(row) == "]") {
        at_prompt = true;
      }
    }
  }
  CAPTURE(frames);
  REQUIRE(at_prompt);
  REQUIRE(sink.bytes().empty());

  harness.type_string("PR#1\r", 2);
  harness.run_frames(4);
  harness.type_string("PRINT \"HELLO\"\r", 2);
  harness.run_frames(4);
  harness.type_string("PR#0\r", 2);
  harness.run_frames(4);

  std::vector<uint8_t> stream;
  for (const TestFixtures::ScopedByteSink_t::Byte_t& entry : sink.bytes()) {
    CHECK(entry.slot == test_slot_1);
    stream.push_back(entry.byte);
  }
  CHECK(hex(stream) == hex(applesoft_session_stream));
  CHECK(sink.dropped() == 0);
  CHECK_FALSE(peripheral_is_any_active());
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
