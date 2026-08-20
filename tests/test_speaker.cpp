// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "core/Peripheral.h"
#include "doctest.h"

extern "C" uint64_t g_cumulative_cycles;
extern "C" double g_current_clk_6502;
extern "C" bool g_full_speed;

extern uint8_t* mem;
constexpr size_t MEMORY_SIZE_64K = 65536;
static std::array<uint8_t, MEMORY_SIZE_64K> dummy_mem{};

extern "C" auto video_get_scanner_address(uint32_t*, uint32_t) -> uint16_t {
  return 0;
}

namespace {

constexpr uint16_t addr_speaker = 0xC030;
constexpr int TEST_SLOT = 0;

constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;
constexpr int REGISTERS_PER_SLOT = 16;

constexpr double STANDARD_APPLE2_SPEED = 1022727.0;

constexpr uint64_t CYCLES_INITIAL = 1000;
constexpr uint32_t CYCLES_WAIT_LONG = 2000000;
constexpr double TIMEOUT_DIVISOR_HALF = 2.0;

constexpr int16_t DC_BLOCK_THRESHOLD = 100;

struct MockHandler {
  void* instance;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
};

static std::map<uint16_t, MockHandler> g_mock_handlers;
static std::vector<int16_t> g_captured_samples;

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
// Justification: ABI signature required by HostInterface_t.
auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                     PeripheralIOHandler write_c0, PeripheralIOHandler read_cx,
                     PeripheralIOHandler write_cx) -> void {
  (void)slot;
  (void)read_c0;
  (void)write_c0;
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

auto Mock_AudioPushSamples(void* instance, const int16_t* buffer,
                           size_t num_samples) -> void {
  (void)instance;
  if (buffer != nullptr && num_samples > 0) {
    const size_t original_size = g_captured_samples.size();
    g_captured_samples.resize(original_size + num_samples);
    std::copy_n(buffer, num_samples,
                g_captured_samples.begin() +
                    static_cast<std::ptrdiff_t>(original_size));
  }
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
  h.AudioPushSamples = Mock_AudioPushSamples;
  h.ResetSystem = nullptr;
  h.PrinterPutChar = nullptr;
  h.PrinterGetStatus = nullptr;
  h.SerialTransmitByte = nullptr;
  h.SerialUpdateState = nullptr;
  return h;
}();

static auto Speaker_Init_With_Mock(int slot) -> void* {
  mem = dummy_mem.data();
  g_cumulative_cycles = 0;
  g_current_clk_6502 = STANDARD_APPLE2_SPEED;
  g_full_speed = false;

  void* instance = speaker_get_descriptor()->init(slot, &mock_host);
  return instance;
}

static auto CheckIsActive(void* instance) -> bool {
  bool active = false;
  size_t size = sizeof(active);
  speaker_get_descriptor()->query(instance, speaker_query_is_active, &active,
                                  &size);
  return active;
}

}  // namespace

TEST_CASE("Speaker Peripheral: Registration and Lifecycle") {
  g_mock_handlers.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);
  REQUIRE(instance != nullptr);

  CHECK(g_mock_handlers.count(addr_speaker) > 0);
  CHECK(g_mock_handlers.at(addr_speaker).read != nullptr);
  CHECK(g_mock_handlers.at(addr_speaker).write != nullptr);

  speaker_get_descriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: Toggle and Event Tracking") {
  g_mock_handlers.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);
  g_cumulative_cycles = CYCLES_INITIAL;

  g_mock_handlers.at(addr_speaker).read(instance, 0, addr_speaker, 0, 0, 0);

  std::array<SpeakerEvent_t, max_speaker_events> events{};
  uint32_t count =
      speaker_get_events(instance, events.data(), max_speaker_events);
  CHECK(count == 1);
  CHECK(events.at(0).cycle == g_cumulative_cycles);
  CHECK(events.at(0).state == true);

  CHECK(speaker_get_events(instance, events.data(), max_speaker_events) == 0);

  speaker_get_descriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: Audio Generation and Filtering") {
  g_mock_handlers.clear();
  g_captured_samples.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);
  g_cumulative_cycles = CYCLES_INITIAL;

  speaker_generate_samples(instance, static_cast<uint32_t>(CYCLES_INITIAL));
  for (auto s : g_captured_samples) {
    CHECK(s == 0);
  }
  g_captured_samples.clear();

  g_mock_handlers.at(addr_speaker).read(instance, 0, addr_speaker, 0, 0, 0);
  g_cumulative_cycles += CYCLES_INITIAL;
  speaker_generate_samples(instance, static_cast<uint32_t>(CYCLES_INITIAL));

  bool has_audio = false;
  for (auto s : g_captured_samples) {
    if (s != 0) {
      has_audio = true;
      break;
    }
  }
  CHECK(has_audio == true);

  g_captured_samples.clear();
  g_cumulative_cycles += CYCLES_WAIT_LONG;
  speaker_generate_samples(instance, CYCLES_WAIT_LONG);

  int16_t last_sample = g_captured_samples.back();
  CHECK(std::abs(last_sample) < DC_BLOCK_THRESHOLD);

  speaker_get_descriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: Inactivity Tracking") {
  g_mock_handlers.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);
  g_cumulative_cycles = CYCLES_INITIAL;

  CHECK(CheckIsActive(instance) == false);

  g_mock_handlers.at(addr_speaker).read(instance, 0, addr_speaker, 0, 0, 0);
  speaker_get_descriptor()->think(instance, 0);
  CHECK(CheckIsActive(instance) == true);

  auto timeout_cycles =
      static_cast<uint32_t>(STANDARD_APPLE2_SPEED / TIMEOUT_DIVISOR_HALF);
  g_cumulative_cycles += timeout_cycles;
  speaker_get_descriptor()->think(instance, timeout_cycles);

  CHECK(CheckIsActive(instance) == false);

  speaker_get_descriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: Full-Speed Suppression") {
  g_mock_handlers.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);
  g_full_speed = true;

  g_mock_handlers.at(addr_speaker).read(instance, 0, addr_speaker, 0, 0, 0);

  CHECK(CheckIsActive(instance) == false);

  std::array<SpeakerEvent_t, max_speaker_events> events{};
  CHECK(speaker_get_events(instance, events.data(), max_speaker_events) == 0);

  speaker_get_descriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: State Persistence Integrity") {
  g_mock_handlers.clear();
  void* instance1 = Speaker_Init_With_Mock(TEST_SLOT);
  g_cumulative_cycles = CYCLES_INITIAL;

  g_mock_handlers.at(addr_speaker).read(instance1, 0, addr_speaker, 0, 0, 0);
  speaker_get_descriptor()->think(instance1, 0);
  REQUIRE(CheckIsActive(instance1) == true);

  size_t state_size = 0;
  speaker_get_descriptor()->save_state(instance1, nullptr, &state_size);
  std::vector<uint8_t> buffer(state_size);
  speaker_get_descriptor()->save_state(instance1, buffer.data(), &state_size);

  void* instance2 = Speaker_Init_With_Mock(TEST_SLOT);
  CHECK(CheckIsActive(instance2) == false);

  PeripheralStatus_t status = speaker_get_descriptor()->load_state(
      instance2, buffer.data(), state_size);
  CHECK(status == peripheral_ok);

  CHECK(CheckIsActive(instance2) == true);

  speaker_get_descriptor()->shutdown(instance1);
  speaker_get_descriptor()->shutdown(instance2);
}

TEST_CASE("Speaker Peripheral: Event Buffer Limits") {
  g_mock_handlers.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);

  for (size_t i = 0; i < max_speaker_events; ++i) {
    g_mock_handlers.at(addr_speaker).read(instance, 0, addr_speaker, 0, 0, 0);
    g_cumulative_cycles++;
  }

  g_mock_handlers.at(addr_speaker).read(instance, 0, addr_speaker, 0, 0, 0);

  std::array<SpeakerEvent_t, max_speaker_events> events{};
  uint32_t count =
      speaker_get_events(instance, events.data(), max_speaker_events);
  CHECK(count == max_speaker_events);

  speaker_get_descriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: Robustness and ABI") {
  g_mock_handlers.clear();

  CHECK(speaker_get_descriptor()->init(TEST_SLOT, nullptr) == nullptr);

  void* instance = Speaker_Init_With_Mock(TEST_SLOT);

  size_t size = 0;
  CHECK(speaker_get_descriptor()->query(instance, speaker_query_is_active,
                                        nullptr, &size) == peripheral_ok);
  CHECK(size == sizeof(bool));

  size = 0;
  CHECK(speaker_get_descriptor()->save_state(instance, nullptr, &size) ==
        peripheral_ok);
  CHECK(size == sizeof(SsIoSpeaker_t));

  speaker_get_descriptor()->shutdown(instance);
}
