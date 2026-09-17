// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures_core.h"

auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                     uint32_t cycles) -> uint8_t;

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;
using TestFixtures::ScopedCore_t;

constexpr uint16_t ADDR_SPEAKER = 0xC030;
// A slot-0 soft switch nothing else claims, so a strobe registered there can
// only be the mock's.
constexpr uint16_t ADDR_MOCK_STROBE = 0xC0F0;
constexpr uint32_t NTSC_FRAME_CYCLES = 17030;
constexpr float EDGE_POSITIVE = 2.0f;

// --- The configurable mock peripheral ---

struct MockState_t {
  HostInterface_t* host = nullptr;
  int strobe_count = 0;
  bool answers_audio = true;
  bool registers_strobe = false;
  PeripheralAudioInfo_t info{};
};

MockState_t g_mock;

auto mock_strobe(void* instance) -> void {
  static_cast<MockState_t*>(instance)->strobe_count++;
}

auto mock_init(int slot, HostInterface_t* host) -> void* {
  (void)slot;
  g_mock.host = host;
  if (g_mock.registers_strobe && host != nullptr &&
      host->RegisterDirectIOStrobe != nullptr) {
    host->RegisterDirectIOStrobe(&g_mock, ADDR_MOCK_STROBE, mock_strobe);
  }
  return &g_mock;
}

auto mock_shutdown(void* instance) -> void { (void)instance; }

auto mock_query(void* instance, uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus_t {
  (void)instance;
  if (out_size == nullptr) {
    return peripheral_error;
  }
  if (cmd_id != PERIPHERAL_QUERY_AUDIO_INFO || !g_mock.answers_audio) {
    return peripheral_incompatible;
  }
  constexpr size_t required = sizeof(PeripheralAudioInfo_t);
  if (out == nullptr) {
    *out_size = required;
    return peripheral_ok;
  }
  if (*out_size < required) {
    *out_size = required;
    return peripheral_error;
  }
  std::memcpy(out, &g_mock.info, required);
  *out_size = required;
  return peripheral_ok;
}

Peripheral_t g_mock_descriptor = {LINAPPLE_ABI_VERSION,
                                  "test.mock.audio",
                                  "MockAudio",
                                  "Configurable audio mock",
                                  "LinApple Contributors",
                                  "1.0.0",
                                  0xFF,
                                  -1,
                                  mock_init,
                                  nullptr,
                                  mock_shutdown,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  mock_query};

/**
 * @brief RAII owner of the mock's configuration.
 *
 * The descriptor is a C-ABI struct of free functions, so what the mock
 * answers has to live in a file-static; this bounds its lifetime to one case.
 */
class ScopedMock_t {
 public:
  ScopedMock_t() { g_mock = MockState_t(); }
  ~ScopedMock_t() { g_mock = MockState_t(); }

  ScopedMock_t(const ScopedMock_t&) = delete;
  auto operator=(const ScopedMock_t&) -> ScopedMock_t& = delete;
  ScopedMock_t(ScopedMock_t&&) = delete;
  auto operator=(ScopedMock_t&&) -> ScopedMock_t& = delete;

  auto descriptor() const -> Peripheral_t* { return &g_mock_descriptor; }
  auto host() const -> HostInterface_t* { return g_mock.host; }
  auto strobe_count() const -> int { return g_mock.strobe_count; }

  auto register_strobe_on_init() -> void { g_mock.registers_strobe = true; }
  auto answer_nothing() -> void { g_mock.answers_audio = false; }

  auto answer_absolute(uint32_t rate_hz, uint32_t num_channels) -> void {
    g_mock.answers_audio = true;
    g_mock.info = PeripheralAudioInfo_t();
    g_mock.info.time_base = peripheral_audio_absolute;
    g_mock.info.sample_rate = rate_hz;
    g_mock.info.num_channels = num_channels;
    g_mock.info.peak_magnitude = 1.0f;
    for (uint32_t c = 0; c < num_channels; ++c) {
      g_mock.info.channels[c].default_pan_left = 1.0f;
      g_mock.info.channels[c].default_pan_right = 1.0f;
    }
  }
};

// --- The register-callback recorder ---

struct AnnounceRecord_t {
  int slot = -1;
  std::string id;
  PeripheralAudioInfo_t info{};
};

std::vector<AnnounceRecord_t> g_announcements;

auto record_announcement(int slot, const char* peripheral_id,
                         const PeripheralAudioInfo_t* info) -> void {
  AnnounceRecord_t record;
  record.slot = slot;
  record.id = (peripheral_id != nullptr) ? peripheral_id : "";
  record.info = *info;
  g_announcements.push_back(record);
}

class ScopedAnnounceRecorder_t {
 public:
  ScopedAnnounceRecorder_t() {
    g_announcements.clear();
    linapple_set_audio_source_register_callback(record_announcement);
  }

  ~ScopedAnnounceRecorder_t() {
    linapple_set_audio_source_register_callback(nullptr);
    g_announcements.clear();
  }

  ScopedAnnounceRecorder_t(const ScopedAnnounceRecorder_t&) = delete;
  auto operator=(const ScopedAnnounceRecorder_t&)
      -> ScopedAnnounceRecorder_t& = delete;
  ScopedAnnounceRecorder_t(ScopedAnnounceRecorder_t&&) = delete;
  auto operator=(ScopedAnnounceRecorder_t&&)
      -> ScopedAnnounceRecorder_t& = delete;

  auto count() const -> size_t { return g_announcements.size(); }
  auto at(size_t index) const -> const AnnounceRecord_t& {
    return g_announcements.at(index);
  }
};

// --- The channel-push recorder ---

std::vector<float> g_pushed_samples;
std::vector<std::string> g_pushed_ids;
size_t g_push_count = 0;
size_t g_pushed_channels = 0;
int g_pushed_slot = -1;

auto record_push(const char* peripheral_id, int slot,
                 const float* const* channels, size_t num_channels,
                 size_t num_samples) -> void {
  if (channels == nullptr || num_channels == 0 || channels[0] == nullptr ||
      num_samples == 0) {
    return;
  }
  g_push_count++;
  g_pushed_channels = num_channels;
  g_pushed_slot = slot;
  g_pushed_ids.emplace_back((peripheral_id != nullptr) ? peripheral_id : "");
  g_pushed_samples.insert(g_pushed_samples.end(), channels[0],
                          channels[0] + num_samples);
}

class ScopedPushRecorder_t {
 public:
  ScopedPushRecorder_t() {
    clear();
    linapple_set_audio_channel_callback(record_push);
  }

  ~ScopedPushRecorder_t() {
    linapple_set_audio_channel_callback(nullptr);
    clear();
  }

  ScopedPushRecorder_t(const ScopedPushRecorder_t&) = delete;
  auto operator=(const ScopedPushRecorder_t&) -> ScopedPushRecorder_t& = delete;
  ScopedPushRecorder_t(ScopedPushRecorder_t&&) = delete;
  auto operator=(ScopedPushRecorder_t&&) -> ScopedPushRecorder_t& = delete;

  auto push_count() const -> size_t { return g_push_count; }
  auto channels() const -> size_t { return g_pushed_channels; }
  auto slot() const -> int { return g_pushed_slot; }
  auto ids() const -> const std::vector<std::string>& { return g_pushed_ids; }
  auto samples() const -> const std::vector<float>& { return g_pushed_samples; }

  static auto clear() -> void {
    g_pushed_samples.clear();
    g_pushed_ids.clear();
    g_push_count = 0;
    g_pushed_channels = 0;
    g_pushed_slot = -1;
  }
};

}  // namespace

// =============================================================================
// The direct-I/O strobe bridge (Task A4)
// =============================================================================

TEST_CASE("Speaker Core Seam: The Strobe Bridge Strobes And Returns The Bus") {
  // A strobed device never drives the data bus, so the bridge must both
  // invoke the handler and answer with io_null: the floating bus on a read,
  // zero on a write. A bridge that strobes and returns zero on a read, or
  // returns the bus without strobing, fails here.
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedCore_t core(config);
  ScopedMock_t mock;
  mock.register_strobe_on_init();
  REQUIRE(peripheral_register(mock.descriptor(), 0) == 0);
  REQUIRE(mock.strobe_count() == 0);

  const uint8_t read_value = io_map_dispatch(0, ADDR_MOCK_STROBE, 0, 0, 12);
  CHECK(read_value == mem_read_floating_bus(12));
  CHECK(mock.strobe_count() == 1);

  const uint8_t write_value = io_map_dispatch(0, ADDR_MOCK_STROBE, 1, 0x55, 0);
  CHECK(write_value == 0);
  CHECK(mock.strobe_count() == 2);

  // A neighbouring address is a different soft switch, not a near miss.
  io_map_dispatch(0, ADDR_MOCK_STROBE + 1, 0, 0, 0);
  CHECK(mock.strobe_count() == 2);

  // The strobe shares the direct-I/O table, so tearing the slot down removes
  // it; a separate table would leave a dead instance strobed.
  REQUIRE(peripheral_unregister(0) == 0);
  io_map_dispatch(0, ADDR_MOCK_STROBE, 0, 0, 0);
  CHECK(mock.strobe_count() == 2);
}

TEST_CASE("Speaker Core Seam: A Read And A Write At $C030 Sound The Same") {
  // Any access to the soft switch toggles the flip-flop on real hardware, so
  // the two directions must reach the same handler and produce the same cone
  // motion. This is the harness-side single-registration case made real.
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedCore_t core(config);
  ScopedPushRecorder_t pushes;
  REQUIRE(peripheral_register(speaker_get_descriptor(), 0) == 0);

  io_map_dispatch(0, ADDR_SPEAKER, 0, 0, 0);
  cpu_calc_cycles(1);
  peripheral_manager_think(1);
  REQUIRE(pushes.push_count() == 1);
  const std::vector<float> from_read = pushes.samples();

  // Settle the cone so the next edge starts from rest, then drive it with a
  // write instead of a read.
  ScopedPushRecorder_t::clear();
  for (int frame = 0; frame < 12; ++frame) {
    cpu_calc_cycles(NTSC_FRAME_CYCLES * (frame + 2));
    peripheral_manager_think(NTSC_FRAME_CYCLES);
  }
  ScopedPushRecorder_t::clear();

  const uint32_t settled = NTSC_FRAME_CYCLES * 13;
  io_map_dispatch(0, ADDR_SPEAKER, 1, 0x00, settled);
  cpu_calc_cycles(settled + 1);
  peripheral_manager_think(1);
  REQUIRE(pushes.push_count() == 1);
  const std::vector<float>& from_write = pushes.samples();

  REQUIRE(from_read.size() == 1);
  REQUIRE(from_write.size() == 1);
  // Opposite polarity, identical magnitude: the second edge is the same step
  // in the other direction, which is what proves one handler served both.
  CHECK(from_read[0] == EDGE_POSITIVE);
  CHECK(from_write[0] == -EDGE_POSITIVE);
}

// =============================================================================
// Audio-source announcement (Task A3)
// =============================================================================

TEST_CASE("Speaker Core Seam: Registration Announces The Source Once") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedCore_t core(config);
  ScopedAnnounceRecorder_t recorder;
  ScopedMock_t mock;
  mock.answer_absolute(44100, 1);

  REQUIRE(peripheral_register(mock.descriptor(), 3) == 0);
  REQUIRE(recorder.count() == 1);
  CHECK(recorder.at(0).slot == 3);
  CHECK(recorder.at(0).id == "test.mock.audio");
  CHECK(recorder.at(0).info.time_base == peripheral_audio_absolute);
  CHECK(recorder.at(0).info.sample_rate == 44100);
  CHECK(recorder.at(0).info.num_channels == 1);
}

TEST_CASE("Speaker Core Seam: NotifyStatusChanged Re-Announces Its Own Slot") {
  // The verb already meant "re-read me", and a layout change is that kind of
  // event. A bridge that keeps (void)slot cannot satisfy the first notify.
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedCore_t core(config);
  ScopedAnnounceRecorder_t recorder;
  ScopedMock_t mock;
  mock.answer_absolute(44100, 1);

  REQUIRE(peripheral_register(mock.descriptor(), 3) == 0);
  REQUIRE(recorder.count() == 1);
  REQUIRE(mock.host() != nullptr);
  REQUIRE(mock.host()->NotifyStatusChanged != nullptr);

  mock.host()->NotifyStatusChanged(3);
  REQUIRE(recorder.count() == 2);
  CHECK(recorder.at(1).slot == 3);
  CHECK(std::memcmp(&recorder.at(0).info, &recorder.at(1).info,
                    sizeof(PeripheralAudioInfo_t)) == 0);

  mock.answer_absolute(44100, 2);
  mock.host()->NotifyStatusChanged(3);
  REQUIRE(recorder.count() == 3);
  CHECK(recorder.at(2).slot == 3);
  CHECK(recorder.at(2).info.num_channels == 2);
}

TEST_CASE("Speaker Core Seam: A Non-Audio Peripheral Announces Nothing") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedCore_t core(config);
  ScopedAnnounceRecorder_t recorder;
  ScopedMock_t mock;
  mock.answer_nothing();

  REQUIRE(peripheral_register(mock.descriptor(), 5) == 0);
  CHECK(recorder.count() == 0);

  REQUIRE(mock.host() != nullptr);
  mock.host()->NotifyStatusChanged(5);
  CHECK(recorder.count() == 0);
}

TEST_CASE("Speaker Core Seam: Registering The Speaker Announces It Once") {
  // The speaker's role in Task A3 is the static case: announced at
  // registration, never again, routing stable for the session.
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedCore_t core(config);
  ScopedAnnounceRecorder_t recorder;

  REQUIRE(peripheral_register(speaker_get_descriptor(), 0) == 0);
  REQUIRE(recorder.count() == 1);
  CHECK(recorder.at(0).slot == 0);
  CHECK(recorder.at(0).id == "linapple.speaker");
  CHECK(recorder.at(0).info.time_base == peripheral_audio_cpu_clocked);
  CHECK(recorder.at(0).info.cycle_divisor == 1);
  CHECK(recorder.at(0).info.num_channels == 1);
  CHECK(recorder.at(0).info.peak_magnitude == doctest::Approx(2.0f));
}

// =============================================================================
// The speaker reached through the real bridge and the real core
// =============================================================================

TEST_CASE("Speaker Core Seam: The Speaker Through The Real Host") {
  // The one case where the speaker and the bridge meet: no mocks anywhere.
  // A single soft-switch read, a single cycle of thinking, and the cone's
  // full 2.0 edge arrives at the frontend callback.
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedCore_t core(config);
  ScopedPushRecorder_t pushes;
  REQUIRE(peripheral_register(speaker_get_descriptor(), 0) == 0);

  const uint8_t bus_value = io_map_dispatch(0, ADDR_SPEAKER, 0, 0, 0);
  CHECK(bus_value == mem_read_floating_bus(0));

  cpu_calc_cycles(1);
  peripheral_manager_think(1);

  REQUIRE(pushes.push_count() == 1);
  REQUIRE(pushes.samples().size() == 1);
  CHECK(pushes.samples()[0] == EDGE_POSITIVE);
  CHECK(pushes.channels() == 1);
  CHECK(pushes.slot() == 0);
  REQUIRE(pushes.ids().size() == 1);
  CHECK(pushes.ids()[0] == "linapple.speaker");
}

TEST_CASE(
    "Speaker Core Seam: Direct IO Bridge Sub-Cycle Cycle Synchronization") {
  // The bridge brings the cumulative cycle count up to the current
  // instruction before dispatching, which is what makes GetCycles() inside a
  // strobe exact and is why the strobe needs no cycle count of its own.
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedCore_t core(config);
  REQUIRE(peripheral_register(speaker_get_descriptor(), 0) == 0);

  const uint64_t initial_cycles = cpu_get_cumulative_cycles();
  constexpr uint32_t instruction_cycle_offset = 512;

  io_map_dispatch(0, ADDR_SPEAKER, 0, 0, instruction_cycle_offset);

  CHECK(cpu_get_cumulative_cycles() ==
        initial_cycles + instruction_cycle_offset);
}

TEST_CASE(
    "Speaker Core Seam: Intra-Frame 1 kHz Tone Synthesis via Direct IO "
    "Dispatch") {
  // Strobes spread across one video frame must land at their own cycles.
  // Collapsing them onto sample zero at the frame boundary is what turns a
  // 1 kHz tone into 60 Hz mush.
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedCore_t core(config);
  ScopedPushRecorder_t pushes;
  REQUIRE(peripheral_register(speaker_get_descriptor(), 0) == 0);

  for (uint32_t cycle = 1000; cycle < 17000; cycle += 1000) {
    io_map_dispatch(0, ADDR_SPEAKER, 0, 0, cycle);
  }

  cpu_calc_cycles(NTSC_FRAME_CYCLES);
  peripheral_manager_think(NTSC_FRAME_CYCLES);

  // One sample per 6502 cycle, so the frame is exactly its own cycle count
  REQUIRE(pushes.samples().size() == NTSC_FRAME_CYCLES);

  const auto& mono = pushes.samples();

  // Nothing drives the cone before the first strobe at cycle 1000, and
  // previous_input equals the drive level, so those samples are true silence.
  CHECK(mono[999] == 0.0f);

  // The first edge is a step of exactly 2.0, emitted unclipped: the only
  // conversion to an integer format happens in the mixer.
  CHECK(mono[1000] == EDGE_POSITIVE);

  // Each of the sixteen strobes flips the output's sign, and an exponential
  // decay never crosses zero between them. The first edge leaves silence for
  // positive, which is not a crossing, so fifteen crossings is the whole
  // frame's count. A frame that collapsed into sample zero would give none.
  size_t zero_crossings = 0;
  for (size_t i = 0; i + 1 < mono.size(); ++i) {
    const bool was_negative = mono[i] < 0.0f;
    const bool is_negative = mono[i + 1] < 0.0f;
    zero_crossings += static_cast<size_t>(was_negative != is_negative);
  }
  CHECK(zero_crossings == 15);
}

// The warp-mode gate is not tested here. Task D1 is deferred: the core
// declining to ask needs a peripheral-declared fact that think has no
// bus-visible side effects, and that has to be designed against
// Mockingboard, whose 6522 timers and interrupts must keep running in warp.
// The push-time gate at the seam is all there is, and it has no observable
// effect on the speaker beyond the silence short-circuit already covered in
// test_speaker.cpp.
