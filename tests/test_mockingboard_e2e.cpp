// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/AudioMixer.h"
#include "test_fixtures_core.h"

auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                     uint32_t cycles) -> uint8_t;

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;
using TestFixtures::ScopedCore_t;

constexpr int CARD_SLOT = 4;
constexpr uint32_t NTSC_FRAME_CYCLES = 17030;
constexpr uint32_t DEVICE_RATE_HZ = 48000;
constexpr size_t CARD_VOICES = 6;
constexpr uint32_t CARD_CYCLE_DIVISOR = 8;

// Slot 4's VIA A. The card decodes the whole page, so $C400 is register 0.
constexpr uint16_t VIA_A = 0xC400;
constexpr uint16_t REG_ORB = 0x0;
constexpr uint16_t REG_ORA = 0x1;
constexpr uint16_t REG_DDRB = 0x2;
constexpr uint16_t REG_DDRA = 0x3;

constexpr uint8_t ORB_INACTIVE = 0x04;
constexpr uint8_t ORB_WRITE = 0x06;
constexpr uint8_t ORB_LATCH = 0x07;
constexpr uint8_t ORB_READ = 0x05;

constexpr uint8_t AY_TONE_A_FINE = 0x00;
constexpr uint8_t AY_TONE_A_COARSE = 0x01;
constexpr uint8_t AY_ENABLE = 0x07;
constexpr uint8_t AY_VOLUME_A = 0x08;

// Tone A on, everything else off; period 0x00FE; voice A at full volume.
constexpr uint8_t AY_ENABLE_TONE_A = 0x3E;
constexpr uint8_t AY_TONE_PERIOD_FINE = 0xFE;
constexpr uint8_t AY_TONE_PERIOD_COARSE = 0x00;
constexpr uint8_t AY_FULL_VOLUME = 0x0F;

// --- E6a goldens, and the arithmetic behind each one -------------------------

constexpr uint32_t TONE_FRAMES = 60;

// 60 x 17030 = 1,021,800 cycles, eight cycles to the AY tick.
constexpr size_t AY_TICKS_DRIVEN =
    (TONE_FRAMES * NTSC_FRAME_CYCLES) / CARD_CYCLE_DIVISOR;
static_assert(AY_TICKS_DRIVEN == 127725, "sixty NTSC frames of AY ticks");

// 1,020,484 / 8 / 48,000 = 2.65751 AY ticks to the output frame, and
// 127,725 / 2.65751 = 48,061.9, floored.
constexpr size_t EXPECTED_OUTPUT_FRAMES = 48061;

// One emulated frame is 801.03 output frames. Draining 800 keeps the ring
// from ever running dry mid-run, so every frame drained during the tone is a
// frame the resampler really produced; the sixty-odd that accumulate come out
// afterwards one at a time.
constexpr size_t FRAMES_PER_EMULATED_FRAME = 800;
constexpr size_t TAIL_PROBE_FRAMES = 512;

// 1,020,484 / (16 x 254) = 251.1 Hz.
constexpr double EXPECTED_TONE_HZ = 251.1;
constexpr double TONE_HZ_TOLERANCE = 1.0;

// The output coupling has a = 0.999 and the tone's half period is 254 ticks,
// so the square settles at 1 / (1 + a^254) = 0.56319 rather than the 0.5 of
// the long-period limit. The mixer's fan-in gain for three channels on one
// side is 1/3, so the unsmeared plateau is 0.56319 x 32767 / 3 = 6152. The
// 2.66-tick output window shaves 0.13 to 0.40 per cent depending on phase.
constexpr int PLATEAU_PCM_MIN = 6120;
constexpr int PLATEAU_PCM_MAX = 6152;
constexpr size_t PLATEAU_WINDOW_FRAMES = 50 * DEVICE_RATE_HZ / 1000;

// The coupling passes a step whole, so the first sample after the volume is
// set is the AY's full scale and not the plateau: 32767 / 3 = 10922, less the
// same window smear.
constexpr int ONSET_PCM_MIN = 10856;
constexpr int ONSET_PCM_MAX = 10922;

// Silence is reached when the conversion rounds to zero, which is half an
// output LSB: 1.5 / 32767 before the 1/3 gain. From the plateau that is
// ln((1.5 / 32767) / 0.56319) / ln(0.999) = 9413 ticks, and from the other
// end of the settled envelope, 0.437, it is 9159. At 2.65751 ticks to the
// frame the mute therefore goes quiet between these two.
constexpr size_t MUTE_SILENCE_FRAMES_MIN = 3447;
constexpr size_t MUTE_SILENCE_FRAMES_MAX = 3542;
constexpr uint32_t MUTE_FRAMES = 10;

// --- E6b ---------------------------------------------------------------------

constexpr uint32_t CPU_FRAMES = 10;
constexpr uint16_t PROGRAM_ADDR = 0x0300;
constexpr uint16_t WRITE_AY_ADDR = 0x0340;

/**
 * @brief The same register sequence as a 6502 program.
 *
 * Open both ports, park the bus inactive, then four calls to a subroutine
 * that takes the register in X and the value in Y and runs the AY's latch and
 * write strobes. Ends in a branch to itself so the frame count, and nothing
 * else, decides how long it runs.
 */
constexpr std::array<uint8_t, 44> program = {{
    0xA9, 0xFF,              // LDA #$FF
    0x8D, 0x02, 0xC4,        // STA $C402   DDRB
    0x8D, 0x03, 0xC4,        // STA $C403   DDRA
    0xA9, 0x04,              // LDA #$04
    0x8D, 0x00, 0xC4,        // STA $C400   bus inactive
    0xA2, 0x00, 0xA0, 0xFE,  // LDX #$00  LDY #$FE
    0x20, 0x40, 0x03,        // JSR $0340
    0xA2, 0x01, 0xA0, 0x00,  // LDX #$01  LDY #$00
    0x20, 0x40, 0x03,        // JSR $0340
    0xA2, 0x07, 0xA0, 0x3E,  // LDX #$07  LDY #$3E
    0x20, 0x40, 0x03,        // JSR $0340
    0xA2, 0x08, 0xA0, 0x0F,  // LDX #$08  LDY #$0F
    0x20, 0x40, 0x03,        // JSR $0340
    0x4C, 0x29, 0x03         // JMP $0329
}};

constexpr std::array<uint8_t, 27> write_ay_routine = {{
    0x8E, 0x01, 0xC4,  // STX $C401   register number on port A
    0xA9, 0x07,        // LDA #$07
    0x8D, 0x00, 0xC4,  // STA $C400   latch
    0xA9, 0x04,        // LDA #$04
    0x8D, 0x00, 0xC4,  // STA $C400   inactive
    0x8C, 0x01, 0xC4,  // STY $C401   value on port A
    0xA9, 0x06,        // LDA #$06
    0x8D, 0x00, 0xC4,  // STA $C400   write
    0xA9, 0x04,        // LDA #$04
    0x8D, 0x00, 0xC4,  // STA $C400   inactive
    0x60               // RTS
}};

PeripheralAudioInfo_t g_announced;
int g_announce_calls = 0;
size_t g_tap_calls = 0;

auto record_announce(int slot, const char* peripheral_id,
                     const PeripheralAudioInfo_t* info) -> void {
  audio_mixer_register_source(slot, peripheral_id, info);
  if (slot != CARD_SLOT || info == nullptr) {
    return;
  }
  g_announce_calls++;
  g_announced = *info;
}

auto record_tap(const char* peripheral_id, int slot,
                const float* const* channels, size_t num_channels,
                size_t num_samples) -> void {
  (void)peripheral_id;
  (void)slot;
  (void)channels;
  (void)num_channels;
  (void)num_samples;
  g_tap_calls++;
}

auto mockingboard_in_slot_4() -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[3] = "Mockingboard";
  return description;
}

/**
 * @brief Card, core, announce, mixer and resampler, wired as a frontend does.
 *
 * The card goes into its slot before any audio callback exists, which is the
 * order every frontend really has: the core is up long before the device is
 * opened. The mixer therefore learns of the card only through the replay that
 * installing the register callback triggers.
 */
class MockingboardChain_t {
 public:
  explicit MockingboardChain_t(const TestConfig_t& config) : core_(config) {
    g_announce_calls = 0;
    g_tap_calls = 0;
    std::memset(&g_announced, 0, sizeof(g_announced));

    // The configuration puts the card in its slot as the core comes up; the
    // manifest is what says the slot really holds a Mockingboard.
    SS_PERIPHERAL_MANIFEST manifest;
    peripheral_get_manifest(&manifest);
    registered_ =
        std::strcmp(manifest.peripherals[CARD_SLOT].name, "Mockingboard") == 0;

    audio_mixer_initialize(DEVICE_RATE_HZ);
    audio_mixer_set_channel_tap_callback(record_tap);
    linapple_set_audio_channel_callback(
        [](const char* peripheral_id, int slot, const float* const* channels,
           size_t num_channels, size_t num_samples) -> void {
          audio_mixer_upload_channels(peripheral_id, slot, channels,
                                      num_channels,
                                      static_cast<uint32_t>(num_samples));
        });
    linapple_set_audio_source_unregister_callback(
        [](int slot) -> void { audio_mixer_unregister_source(slot); });
    linapple_set_audio_source_register_callback(record_announce);
  }

  ~MockingboardChain_t() {
    audio_mixer_set_channel_tap_callback(nullptr);
    audio_mixer_destroy();
  }

  MockingboardChain_t(const MockingboardChain_t&) = delete;
  auto operator=(const MockingboardChain_t&) -> MockingboardChain_t& = delete;
  MockingboardChain_t(MockingboardChain_t&&) = delete;
  auto operator=(MockingboardChain_t&&) -> MockingboardChain_t& = delete;

  auto registered() const -> bool { return registered_; }

  static auto write_via(uint16_t reg, uint8_t value) -> void {
    io_map_dispatch(0, static_cast<uint16_t>(VIA_A + reg), 1, value, 0);
  }

  static auto read_via(uint16_t reg, uint32_t executed_cycles) -> uint8_t {
    return io_map_dispatch(0, static_cast<uint16_t>(VIA_A + reg), 0, 0,
                           executed_cycles);
  }

  static auto write_ay(uint8_t reg, uint8_t value) -> void {
    write_via(REG_ORA, reg);
    write_via(REG_ORB, ORB_LATCH);
    write_via(REG_ORB, ORB_INACTIVE);
    write_via(REG_ORA, value);
    write_via(REG_ORB, ORB_WRITE);
    write_via(REG_ORB, ORB_INACTIVE);
  }

  // Port A has to stop being driven before the strobe, or the chip has
  // nothing to put the register on.
  static auto read_ay(uint8_t reg, uint32_t executed_cycles) -> uint8_t {
    io_map_dispatch(0, VIA_A + REG_ORA, 1, reg, executed_cycles);
    io_map_dispatch(0, VIA_A + REG_ORB, 1, ORB_LATCH, executed_cycles);
    io_map_dispatch(0, VIA_A + REG_ORB, 1, ORB_INACTIVE, executed_cycles);
    io_map_dispatch(0, VIA_A + REG_DDRA, 1, 0x00, executed_cycles);
    io_map_dispatch(0, VIA_A + REG_ORB, 1, ORB_READ, executed_cycles);
    io_map_dispatch(0, VIA_A + REG_ORB, 1, ORB_INACTIVE, executed_cycles);
    return read_via(REG_ORA, executed_cycles);
  }

  // The card takes its whole time base from think, so a frame with no CPU in
  // it is a frame of cycles handed straight to the slot.
  auto run_frame(size_t drain_frames) -> void {
    peripheral_manager_think(NTSC_FRAME_CYCLES);
    append(drain(drain_frames));
  }

  auto drain(size_t frames) -> std::vector<int16_t> {
    std::vector<int16_t> block(frames * 2, 0);
    audio_mixer_get_samples(block.data(), block.size());
    return block;
  }

  auto append(const std::vector<int16_t>& block) -> void {
    output_.insert(output_.end(), block.begin(), block.end());
  }

  auto output() const -> const std::vector<int16_t>& { return output_; }
  auto clear_output() -> void { output_.clear(); }

  static auto left(const std::vector<int16_t>& stereo, size_t frame)
      -> int16_t {
    return stereo[frame * 2];
  }

  static auto right(const std::vector<int16_t>& stereo, size_t frame)
      -> int16_t {
    return stereo[(frame * 2) + 1];
  }

 private:
  ScopedCore_t core_;
  bool registered_ = false;
  std::vector<int16_t> output_;
};

auto count_zero_crossings(const std::vector<int16_t>& stereo, size_t frames)
    -> size_t {
  size_t crossings = 0;
  for (size_t i = 0; i + 1 < frames; ++i) {
    const bool was_negative = MockingboardChain_t::left(stereo, i) < 0;
    const bool is_negative = MockingboardChain_t::left(stereo, i + 1) < 0;
    crossings += static_cast<size_t>(was_negative != is_negative);
  }
  return crossings;
}

// The index one past the last frame carrying anything, which after a
// one-frame-at-a-time drain is the number of frames the resampler produced:
// an empty ring yields an exact zero rather than a fade.
auto frames_until_silence(const std::vector<int16_t>& stereo) -> size_t {
  size_t last_signal = 0;
  const size_t frames = stereo.size() / 2;
  for (size_t i = 0; i < frames; ++i) {
    const bool carries =
        (MockingboardChain_t::left(stereo, i) != 0) ||
        (MockingboardChain_t::right(stereo, i) != 0);
    last_signal = carries ? (i + 1) : last_signal;
  }
  return last_signal;
}

}  // namespace

TEST_CASE("Mockingboard End To End: A Tone Through The Whole Chain") {
  TestConfig_t config(mockingboard_in_slot_4());
  MockingboardChain_t chain(config);
  REQUIRE(chain.registered());

  // The card was in its slot before the register callback existed. What the
  // mixer knows about it can only have come from the replay.
  REQUIRE(g_announce_calls == 1);
  CHECK(g_announced.time_base == peripheral_audio_cpu_clocked);
  CHECK(g_announced.cycle_divisor == CARD_CYCLE_DIVISOR);
  CHECK(g_announced.num_channels == CARD_VOICES);
  CHECK(g_announced.sample_rate == 0);
  CHECK(g_announced.peak_magnitude == doctest::Approx(1.0F).epsilon(1e-6));

  MockingboardChain_t::write_via(REG_DDRB, 0xFF);
  MockingboardChain_t::write_via(REG_DDRA, 0xFF);
  MockingboardChain_t::write_via(REG_ORB, ORB_INACTIVE);
  MockingboardChain_t::write_ay(AY_TONE_A_FINE, AY_TONE_PERIOD_FINE);
  MockingboardChain_t::write_ay(AY_TONE_A_COARSE, AY_TONE_PERIOD_COARSE);
  MockingboardChain_t::write_ay(AY_ENABLE, AY_ENABLE_TONE_A);
  MockingboardChain_t::write_ay(AY_VOLUME_A, AY_FULL_VOLUME);

  for (uint32_t frame = 0; frame < TONE_FRAMES; ++frame) {
    chain.run_frame(FRAMES_PER_EMULATED_FRAME);
  }
  // One frame at a time, so the ring is emptied without a single faded frame
  // entering the count.
  for (size_t i = 0; i < TAIL_PROBE_FRAMES; ++i) {
    chain.append(chain.drain(1));
  }

  const std::vector<int16_t> tone = chain.output();
  const size_t produced = frames_until_silence(tone);
  CHECK(produced >= EXPECTED_OUTPUT_FRAMES - 1);
  CHECK(produced <= EXPECTED_OUTPUT_FRAMES + 1);

  const size_t crossings = count_zero_crossings(tone, produced);
  const double seconds =
      static_cast<double>(produced) / static_cast<double>(DEVICE_RATE_HZ);
  const double measured_hz = static_cast<double>(crossings) / (2.0 * seconds);
  CHECK(measured_hz >= EXPECTED_TONE_HZ - TONE_HZ_TOLERANCE);
  CHECK(measured_hz <= EXPECTED_TONE_HZ + TONE_HZ_TOLERANCE);

  // The onset, where the coupling passes the whole step.
  int onset = 0;
  for (size_t i = 0; i < produced; ++i) {
    onset = std::max<int>(onset, MockingboardChain_t::left(tone, i));
  }
  CHECK(onset >= ONSET_PCM_MIN);
  CHECK(onset <= ONSET_PCM_MAX);

  // And the plateau, over the last fifty milliseconds, by which time the
  // coupling has long since converged on 1 / (1 + a^254).
  int plateau = 0;
  for (size_t i = produced - PLATEAU_WINDOW_FRAMES; i < produced; ++i) {
    plateau = std::max<int>(plateau, MockingboardChain_t::left(tone, i));
  }
  CHECK(plateau >= PLATEAU_PCM_MIN);
  CHECK(plateau <= PLATEAU_PCM_MAX);

  // AY1 sits at volume zero, so its coupling has nothing to pass and the
  // right channel is silence rather than something small.
  int16_t loudest_right = 0;
  for (size_t i = 0; i < tone.size() / 2; ++i) {
    loudest_right = std::max<int16_t>(
        loudest_right, static_cast<int16_t>(
                           std::abs(MockingboardChain_t::right(tone, i))));
  }
  CHECK(loudest_right == 0);

  // Muting by disabling the tone would leave the voice sitting at its volume
  // as a DC level, which the coupling would decay to nothing anyway and the
  // case would pass for the wrong reason. The volume register is the mute.
  chain.clear_output();
  MockingboardChain_t::write_ay(AY_VOLUME_A, 0x00);
  for (uint32_t frame = 0; frame < MUTE_FRAMES; ++frame) {
    chain.run_frame(FRAMES_PER_EMULATED_FRAME);
  }

  const std::vector<int16_t> decay = chain.output();
  const size_t still_audible = frames_until_silence(decay);
  CHECK(still_audible >= MUTE_SILENCE_FRAMES_MIN);
  CHECK(still_audible <= MUTE_SILENCE_FRAMES_MAX);
}

TEST_CASE("Mockingboard End To End: A Real 6502 Programs The Card") {
  TestConfig_t config(mockingboard_in_slot_4());
  MockingboardChain_t chain(config);
  REQUIRE(chain.registered());

  ScopedCore_t::poke(PROGRAM_ADDR, program);
  ScopedCore_t::poke(WRITE_AY_ADDR, write_ay_routine);

  CpuRegisters_t* regs = cpu_get_registers();
  regs->pc = PROGRAM_ADDR;
  regs->sp = 0x01FF;
  regs->ps = 0x24;

  for (uint32_t frame = 0; frame + 1 < CPU_FRAMES; ++frame) {
    peripheral_manager_think(cpu_execute(NTSC_FRAME_CYCLES));
    chain.append(chain.drain(FRAMES_PER_EMULATED_FRAME));
  }

  // The readback rides the last frame's own cycle count, so it sees the card
  // where the frame left it and the closing think adds nothing on top.
  const uint32_t executed = cpu_execute(NTSC_FRAME_CYCLES);
  const uint8_t tone_fine =
      MockingboardChain_t::read_ay(AY_TONE_A_FINE, executed);
  const uint8_t tone_coarse =
      MockingboardChain_t::read_ay(AY_TONE_A_COARSE, executed);
  const uint8_t enable = MockingboardChain_t::read_ay(AY_ENABLE, executed);
  const uint8_t volume = MockingboardChain_t::read_ay(AY_VOLUME_A, executed);
  peripheral_manager_think(executed);
  chain.append(chain.drain(FRAMES_PER_EMULATED_FRAME));

  CHECK(tone_fine == AY_TONE_PERIOD_FINE);
  CHECK(tone_coarse == AY_TONE_PERIOD_COARSE);
  CHECK(enable == AY_ENABLE_TONE_A);
  CHECK(volume == AY_FULL_VOLUME);

  CHECK(g_tap_calls > 0);
  CHECK(frames_until_silence(chain.output()) > 0);
}
