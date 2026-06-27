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

extern "C" uint64_t g_nCumulativeCycles;
extern "C" double g_fCurrentCLK6502;
extern "C" bool g_bFullSpeed;

extern uint8_t* mem;
constexpr size_t MEMORY_SIZE_64K = 65536;
static std::array<uint8_t, MEMORY_SIZE_64K> dummy_mem{};

extern "C" auto VideoGetScannerAddress(uint32_t*, uint32_t) -> uint16_t {
  return 0;
}

namespace {

constexpr uint16_t ADDR_SPEAKER = 0xC030;
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

auto Mock_Log(void* instance, PeripheralLogLevel level, const char* fmt, ...)
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

static HostInterface_t mock_host = {
    .Log = Mock_Log,
    .AssertIrq = Mock_AssertIrq,
    .RegisterIO = Mock_RegisterIO,
    .RegisterCxROM = Mock_RegisterCxROM,
    .RegisterExpansionROM = Mock_RegisterExpansionROM,
    .RegisterDirectIO = Mock_RegisterDirectIO,
    .GetMemPtr = nullptr,
    .GetCycles = nullptr,
    .GetConfig = nullptr,
    .SetConfig = nullptr,
    .NotifyStatusChanged = nullptr,
    .NotifyActivityChanged = nullptr,
    .RequestPreciseTiming = nullptr,
    .AudioPushSamples = Mock_AudioPushSamples,
    .ResetSystem = nullptr,
    .PrinterPutChar = nullptr,
    .PrinterGetStatus = nullptr,
    .SerialTransmitByte = nullptr,
    .SerialUpdateState = nullptr};

static auto Speaker_Init_With_Mock(int slot) -> void* {
  mem = dummy_mem.data();
  g_nCumulativeCycles = 0;
  g_fCurrentCLK6502 = STANDARD_APPLE2_SPEED;
  g_bFullSpeed = false;

  void* instance = Speaker_GetDescriptor()->init(slot, &mock_host);
  return instance;
}

static auto CheckIsActive(void* instance) -> bool {
  bool active = false;
  size_t size = sizeof(active);
  Speaker_GetDescriptor()->query(instance, speaker_query_is_active, &active,
                                 &size);
  return active;
}

}  // namespace

TEST_CASE("Speaker Peripheral: Registration and Lifecycle") {
  g_mock_handlers.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);
  REQUIRE(instance != nullptr);

  CHECK(g_mock_handlers.count(ADDR_SPEAKER) > 0);
  CHECK(g_mock_handlers.at(ADDR_SPEAKER).read != nullptr);
  CHECK(g_mock_handlers.at(ADDR_SPEAKER).write != nullptr);

  Speaker_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: Toggle and Event Tracking") {
  g_mock_handlers.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);
  g_nCumulativeCycles = CYCLES_INITIAL;

  g_mock_handlers.at(ADDR_SPEAKER).read(instance, 0, ADDR_SPEAKER, 0, 0, 0);

  std::array<SpeakerEvent_t, max_speaker_events> events{};
  uint32_t count =
      Speaker_GetEvents(instance, events.data(), max_speaker_events);
  CHECK(count == 1);
  CHECK(events.at(0).cycle == g_nCumulativeCycles);
  CHECK(events.at(0).state == true);

  CHECK(Speaker_GetEvents(instance, events.data(), max_speaker_events) == 0);

  Speaker_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: Audio Generation and Filtering") {
  g_mock_handlers.clear();
  g_captured_samples.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);
  g_nCumulativeCycles = CYCLES_INITIAL;

  Speaker_GenerateSamples(instance, static_cast<uint32_t>(CYCLES_INITIAL));
  for (auto s : g_captured_samples) {
    CHECK(s == 0);
  }
  g_captured_samples.clear();

  g_mock_handlers.at(ADDR_SPEAKER).read(instance, 0, ADDR_SPEAKER, 0, 0, 0);
  g_nCumulativeCycles += CYCLES_INITIAL;
  Speaker_GenerateSamples(instance, static_cast<uint32_t>(CYCLES_INITIAL));

  bool has_audio = false;
  for (auto s : g_captured_samples) {
    if (s != 0) {
      has_audio = true;
      break;
    }
  }
  CHECK(has_audio == true);

  g_captured_samples.clear();
  g_nCumulativeCycles += CYCLES_WAIT_LONG;
  Speaker_GenerateSamples(instance, CYCLES_WAIT_LONG);

  int16_t last_sample = g_captured_samples.back();
  CHECK(std::abs(last_sample) < DC_BLOCK_THRESHOLD);

  Speaker_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: Inactivity Tracking") {
  g_mock_handlers.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);
  g_nCumulativeCycles = CYCLES_INITIAL;

  CHECK(CheckIsActive(instance) == false);

  g_mock_handlers.at(ADDR_SPEAKER).read(instance, 0, ADDR_SPEAKER, 0, 0, 0);
  Speaker_GetDescriptor()->think(instance, 0);
  CHECK(CheckIsActive(instance) == true);

  auto timeout_cycles =
      static_cast<uint32_t>(STANDARD_APPLE2_SPEED / TIMEOUT_DIVISOR_HALF);
  g_nCumulativeCycles += timeout_cycles;
  Speaker_GetDescriptor()->think(instance, timeout_cycles);

  CHECK(CheckIsActive(instance) == false);

  Speaker_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: Full-Speed Suppression") {
  g_mock_handlers.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);
  g_bFullSpeed = true;

  g_mock_handlers.at(ADDR_SPEAKER).read(instance, 0, ADDR_SPEAKER, 0, 0, 0);

  CHECK(CheckIsActive(instance) == false);

  std::array<SpeakerEvent_t, max_speaker_events> events{};
  CHECK(Speaker_GetEvents(instance, events.data(), max_speaker_events) == 0);

  Speaker_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: State Persistence Integrity") {
  g_mock_handlers.clear();
  void* instance1 = Speaker_Init_With_Mock(TEST_SLOT);
  g_nCumulativeCycles = CYCLES_INITIAL;

  g_mock_handlers.at(ADDR_SPEAKER).read(instance1, 0, ADDR_SPEAKER, 0, 0, 0);
  Speaker_GetDescriptor()->think(instance1, 0);
  REQUIRE(CheckIsActive(instance1) == true);

  size_t state_size = 0;
  Speaker_GetDescriptor()->save_state(instance1, nullptr, &state_size);
  std::vector<uint8_t> buffer(state_size);
  Speaker_GetDescriptor()->save_state(instance1, buffer.data(), &state_size);

  void* instance2 = Speaker_Init_With_Mock(TEST_SLOT);
  CHECK(CheckIsActive(instance2) == false);

  PeripheralStatus status =
      Speaker_GetDescriptor()->load_state(instance2, buffer.data(), state_size);
  CHECK(status == PERIPHERAL_OK);

  CHECK(CheckIsActive(instance2) == true);

  Speaker_GetDescriptor()->shutdown(instance1);
  Speaker_GetDescriptor()->shutdown(instance2);
}

TEST_CASE("Speaker Peripheral: Event Buffer Limits") {
  g_mock_handlers.clear();
  void* instance = Speaker_Init_With_Mock(TEST_SLOT);

  for (size_t i = 0; i < max_speaker_events; ++i) {
    g_mock_handlers.at(ADDR_SPEAKER).read(instance, 0, ADDR_SPEAKER, 0, 0, 0);
    g_nCumulativeCycles++;
  }

  g_mock_handlers.at(ADDR_SPEAKER).read(instance, 0, ADDR_SPEAKER, 0, 0, 0);

  std::array<SpeakerEvent_t, max_speaker_events> events{};
  uint32_t count =
      Speaker_GetEvents(instance, events.data(), max_speaker_events);
  CHECK(count == max_speaker_events);

  Speaker_GetDescriptor()->shutdown(instance);
}

TEST_CASE("Speaker Peripheral: Robustness and ABI") {
  g_mock_handlers.clear();

  CHECK(Speaker_GetDescriptor()->init(TEST_SLOT, nullptr) == nullptr);

  void* instance = Speaker_Init_With_Mock(TEST_SLOT);

  size_t size = 0;
  CHECK(Speaker_GetDescriptor()->query(instance, speaker_query_is_active,
                                       nullptr, &size) == PERIPHERAL_OK);
  CHECK(size == sizeof(bool));

  size = 0;
  CHECK(Speaker_GetDescriptor()->save_state(instance, nullptr, &size) ==
        PERIPHERAL_OK);
  CHECK(size == sizeof(SS_IO_Speaker));

  Speaker_GetDescriptor()->shutdown(instance);
}
