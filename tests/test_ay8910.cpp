// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays) Justification: Hardware register values, divisor goldens and planar output buffers
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "apple2/chips/AY8910.h"
#include "doctest.h"

namespace {

constexpr size_t scratch_ticks = 4096;

// Owns the three planar buffers a step writes into so a case can talk about
// ticks and levels and nothing else.
class Renderer {
 public:
  Renderer() {
    for (size_t v = 0; v < AY8910_NUM_VOICES; ++v) {
      buffers_[v].resize(scratch_ticks, 0.0F);
      pointers_[v] = buffers_[v].data();
    }
    ay8910_reset(&chip_);
  }

  auto chip() -> Ay8910_t* { return &chip_; }

  auto write(uint8_t reg, uint8_t val) -> void {
    ay8910_write(&chip_, reg, val);
  }

  auto step(size_t ticks) -> void {
    size_t remaining = ticks;
    while (remaining > 0) {
      const size_t n = (remaining < scratch_ticks) ? remaining : scratch_ticks;
      ay8910_step(&chip_, n, pointers_.data(), scratch_ticks);
      last_.assign(buffers_[0].begin(),
                   buffers_[0].begin() + static_cast<long>(n));
      last_b_.assign(buffers_[1].begin(),
                     buffers_[1].begin() + static_cast<long>(n));
      remaining -= n;
    }
  }

  // The level the most recent step left on voice A.
  auto level_a() -> float { return last_.empty() ? 0.0F : last_.back(); }
  auto level_b() -> float { return last_b_.empty() ? 0.0F : last_b_.back(); }
  auto chunk_a() const -> const std::vector<float>& { return last_; }

  auto edges_a(size_t ticks) -> int {
    int edges = 0;
    float previous = level_a();
    for (size_t i = 0; i < ticks; ++i) {
      step(1);
      if (level_a() != previous) {
        ++edges;
        previous = level_a();
      }
    }
    return edges;
  }

 private:
  Ay8910_t chip_;
  std::array<std::vector<float>, AY8910_NUM_VOICES> buffers_;
  std::array<float*, AY8910_NUM_VOICES> pointers_{};
  std::vector<float> last_;
  std::vector<float> last_b_;
};

auto set_tone_a(Renderer& r, uint16_t period) -> void {
  r.write(0, static_cast<uint8_t>(period & 0xFF));
  r.write(1, static_cast<uint8_t>(period >> 8));
}

// Tone A on, everything else off, voice A at a fixed volume.
auto voice_a_tone(Renderer& r, uint16_t period, uint8_t volume) -> void {
  set_tone_a(r, period);
  r.write(7, 0x3E);
  r.write(8, volume);
}

}  // namespace

TEST_CASE("AY-3-8910: Reset Leaves A Live Noise LFSR And Silence") {
  Renderer r;
  r.write(0, 0x34);
  r.write(8, 0x0F);
  r.write(13, 0x0A);
  ay8910_reset(r.chip());

  CHECK(r.chip()->rng == 1);
  CHECK(r.chip()->regs[0] == 0);
  CHECK(r.chip()->regs[8] == 0);
  CHECK(r.chip()->envelope_step == 0);
  CHECK_FALSE(r.chip()->env_holding);

  r.step(1000);
  CHECK(r.level_a() == doctest::Approx(0.0F).epsilon(1e-6));
}

TEST_CASE("AY-3-8910: Tone Output Toggles Every TP Ticks") {
  constexpr uint16_t period = 254;
  Renderer r;
  voice_a_tone(r, period, 0x0F);

  // A square wave of period 2 * TP ticks puts 200 edges in 100 periods.
  CHECK(r.edges_a(2 * period * 100) == 200);
}

TEST_CASE("AY-3-8910: A Short Tone Period Toggles On Schedule Too") {
  constexpr uint16_t period = 3;
  Renderer r;
  voice_a_tone(r, period, 0x0F);
  CHECK(r.edges_a(2 * period * 100) == 200);
}

TEST_CASE("AY-3-8910: Tone Period Zero Forces The Output High") {
  Renderer r;
  voice_a_tone(r, 0, 0x0F);
  r.step(1);
  CHECK(r.level_a() == doctest::Approx(1.0F).epsilon(1e-6));
  CHECK(r.edges_a(10000) == 0);
  CHECK(r.level_a() == doctest::Approx(1.0F).epsilon(1e-6));
}

TEST_CASE("AY-3-8910: Full Volume Is Exactly One And The Table Is Monotonic") {
  Renderer r;
  voice_a_tone(r, 0, 0x0F);
  r.step(1);
  CHECK(r.level_a() == 1.0F);

  float previous = -1.0F;
  for (uint8_t v = 0; v <= 0x0F; ++v) {
    r.write(8, v);
    r.step(1);
    const float level = r.level_a();
    CHECK(level > previous);
    CHECK(level >= 0.0F);
    CHECK(level <= 1.0F);
    previous = level;
  }
  CHECK(previous == 1.0F);
}

TEST_CASE("AY-3-8910: Noise LFSR Advances Every Two NP Ticks") {
  constexpr uint8_t noise_period = 5;

  Ay8910_t reference;
  ay8910_reset(&reference);
  Renderer r;
  r.write(6, noise_period);
  r.write(7, 0x3F);

  // Six shifts of the reference LFSR, reproduced by hand from the same taps
  // the chip uses, must match six 2 * NP tick spans of the real thing.
  for (int k = 1; k <= 6; ++k) {
    reference.rng = (reference.rng >> 1) |
                    (((reference.rng & 1) ^ ((reference.rng >> 3) & 1)) << 16);
    r.step(2 * noise_period);
    CHECK(r.chip()->rng == reference.rng);
  }

  // One tick short of the next boundary and it has not shifted again.
  r.step((2 * noise_period) - 1);
  CHECK(r.chip()->rng == reference.rng);
  r.step(1);
  CHECK(r.chip()->rng != reference.rng);
}

TEST_CASE("AY-3-8910: Noise Period Zero Behaves As One") {
  Ay8910_t reference;
  ay8910_reset(&reference);
  Renderer r;
  r.write(6, 0x00);
  r.write(7, 0x3F);

  r.step(1);
  CHECK(r.chip()->rng == reference.rng);
  r.step(1);
  reference.rng = (reference.rng >> 1) |
                  (((reference.rng & 1) ^ ((reference.rng >> 3) & 1)) << 16);
  CHECK(r.chip()->rng == reference.rng);
}

TEST_CASE("AY-3-8910: Envelope Steps Every Two EP Ticks") {
  constexpr uint16_t env_period = 7;
  constexpr size_t step_ticks = 2 * env_period;
  Renderer r;
  voice_a_tone(r, 0, 0x10);
  r.write(11, static_cast<uint8_t>(env_period));
  r.write(12, 0x00);
  r.write(13, 0x00);

  // Shape 0x0 is a single decay: the level drops one step per 2 * EP ticks
  // from full scale and is silent after fifteen of them. An envelope divisor
  // sixteen times too large would still be at full volume here.
  r.step(1);
  CHECK(r.level_a() == 1.0F);

  for (int k = 1; k <= 15; ++k) {
    r.step(step_ticks);
    CHECK(r.chip()->envelope_vol == static_cast<uint8_t>(15 - k));
  }
  CHECK(r.level_a() == doctest::Approx(0.0F).epsilon(1e-6));

  // The sixteenth step ends the cycle and it holds at silence from there.
  r.step(step_ticks);
  CHECK(r.chip()->env_holding);
  CHECK(r.level_a() == doctest::Approx(0.0F).epsilon(1e-6));
  r.step(step_ticks * 64);
  CHECK(r.level_a() == doctest::Approx(0.0F).epsilon(1e-6));
}

TEST_CASE("AY-3-8910: Envelope Period Zero Behaves As One") {
  Renderer r;
  voice_a_tone(r, 0, 0x10);
  r.write(11, 0x00);
  r.write(12, 0x00);
  r.write(13, 0x00);

  r.step(2);
  CHECK(r.chip()->envelope_vol == 14);
  r.step(2);
  CHECK(r.chip()->envelope_vol == 13);
}

TEST_CASE("AY-3-8910: A Register 13 Write Restarts The Envelope") {
  constexpr uint16_t env_period = 4;
  Renderer r;
  voice_a_tone(r, 0, 0x10);
  r.write(11, static_cast<uint8_t>(env_period));
  r.write(13, 0x00);
  r.step(2 * env_period * 5);
  REQUIRE(r.chip()->envelope_vol == 10);

  r.write(13, 0x00);
  CHECK(r.chip()->envelope_step == 0);
  CHECK(r.chip()->count_e == 0);
  CHECK(r.chip()->envelope_vol == 15);
  CHECK_FALSE(r.chip()->env_holding);
}

TEST_CASE("AY-3-8910: The Sixteen Envelope Shapes Match The Data Sheet") {
  constexpr uint16_t env_period = 1;
  constexpr size_t step_ticks = 2 * env_period;

  // Per shape: the volume at the first step, the volume fifteen steps later
  // (the end of the first sweep), and the volume exactly two whole cycles in.
  // The non-continue shapes and the two that hold at silence are the ones the
  // old model got backwards.
  struct Expectation_t {
    uint8_t shape;
    uint8_t first;
    uint8_t end_of_sweep;
    uint8_t at_two_cycles;
  };
  constexpr Expectation_t expectations[16] = {
      {0x0, 15, 0, 0},  {0x1, 15, 0, 0},  {0x2, 15, 0, 0},  {0x3, 15, 0, 0},
      {0x4, 0, 15, 0},  {0x5, 0, 15, 0},  {0x6, 0, 15, 0},  {0x7, 0, 15, 0},
      {0x8, 15, 0, 15}, {0x9, 15, 0, 0},  {0xA, 15, 0, 15}, {0xB, 15, 0, 15},
      {0xC, 0, 15, 0},  {0xD, 0, 15, 15}, {0xE, 0, 15, 0},  {0xF, 0, 15, 0}};

  for (const auto& e : expectations) {
    Renderer r;
    voice_a_tone(r, 0, 0x10);
    r.write(11, static_cast<uint8_t>(env_period));
    r.write(12, 0x00);
    r.write(13, e.shape);

    CHECK(r.chip()->envelope_vol == e.first);
    r.step(step_ticks * 15);
    CHECK(r.chip()->envelope_vol == e.end_of_sweep);
    r.step(step_ticks * 17);
    CHECK(r.chip()->envelope_vol == e.at_two_cycles);
  }
}

TEST_CASE("AY-3-8910: Alternating Shapes Keep Alternating Across Steps") {
  constexpr uint16_t env_period = 1;
  constexpr size_t cycle_ticks = 2 * env_period * 16;
  Renderer r;
  voice_a_tone(r, 0, 0x10);
  r.write(11, static_cast<uint8_t>(env_period));
  r.write(13, 0x0A);

  // Shape 0xA sweeps down, then up, then down again. A model that re-derived
  // the direction from the shape register on every call would never leave the
  // first sweep.
  CHECK(r.chip()->envelope_vol == 15);
  r.step(cycle_ticks);
  CHECK(r.chip()->env_attack);
  r.step(cycle_ticks);
  CHECK_FALSE(r.chip()->env_attack);
  r.step(cycle_ticks);
  CHECK(r.chip()->env_attack);
}

TEST_CASE("AY-3-8910: Amplitude Bit 0x10 Selects The Envelope") {
  constexpr uint16_t env_period = 2;
  Renderer r;
  voice_a_tone(r, 0, 0x08);
  r.write(11, static_cast<uint8_t>(env_period));
  r.write(13, 0x00);

  r.step(2 * env_period * 8);
  const float fixed = r.level_a();
  REQUIRE(r.chip()->envelope_vol == 7);

  // A fixed volume of 8 is not the envelope's level of 7, so the switch is
  // observable in the output and not just in the register.
  r.write(8, 0x10);
  r.step(1);
  CHECK(r.level_a() != fixed);
  CHECK(r.level_a() < fixed);
}

TEST_CASE("AY-3-8910: Mixer Bits Gate Tone And Noise Per Voice") {
  constexpr uint16_t period = 4;

  SUBCASE("Everything disabled leaves both gates high") {
    Renderer r;
    set_tone_a(r, period);
    r.write(2, static_cast<uint8_t>(period));
    r.write(8, 0x0F);
    r.write(9, 0x0F);
    r.write(7, 0x3F);
    r.step(2 * period * 10);
    CHECK(r.level_a() == 1.0F);
    CHECK(r.level_b() == 1.0F);
  }

  SUBCASE("Tone A enabled gates voice A and leaves voice B alone") {
    Renderer r;
    set_tone_a(r, period);
    r.write(2, static_cast<uint8_t>(period));
    r.write(8, 0x0F);
    r.write(9, 0x0F);
    r.write(7, 0x3E);
    CHECK(r.edges_a(2 * period * 10) == 20);
    CHECK(r.level_b() == 1.0F);
  }

  SUBCASE("Noise on A with tone off gates voice A from the LFSR") {
    Renderer r;
    r.write(6, 0x01);
    r.write(8, 0x0F);
    r.write(9, 0x0F);
    r.write(7, 0x37);
    int noise_edges = 0;
    float previous = r.level_a();
    for (int i = 0; i < 4000; ++i) {
      r.step(1);
      if (r.level_a() != previous) {
        ++noise_edges;
        previous = r.level_a();
      }
    }
    CHECK(noise_edges > 0);
    CHECK(r.level_b() == 1.0F);
  }
}

TEST_CASE("AY-3-8910: Register Writes Are Masked To Their Data-Sheet Width") {
  Renderer r;
  for (uint8_t reg = 0; reg < AY8910_NUM_REGISTERS; ++reg) {
    r.write(reg, 0xFF);
  }
  CHECK(r.chip()->regs[0] == 0xFF);
  CHECK(r.chip()->regs[1] == 0x0F);
  CHECK(r.chip()->regs[2] == 0xFF);
  CHECK(r.chip()->regs[3] == 0x0F);
  CHECK(r.chip()->regs[4] == 0xFF);
  CHECK(r.chip()->regs[5] == 0x0F);
  CHECK(r.chip()->regs[6] == 0x1F);
  CHECK(r.chip()->regs[7] == 0xFF);
  CHECK(r.chip()->regs[8] == 0x1F);
  CHECK(r.chip()->regs[9] == 0x1F);
  CHECK(r.chip()->regs[10] == 0x1F);
  CHECK(r.chip()->regs[11] == 0xFF);
  CHECK(r.chip()->regs[12] == 0xFF);
  CHECK(r.chip()->regs[13] == 0x0F);
  CHECK(r.chip()->regs[14] == 0xFF);
  CHECK(r.chip()->regs[15] == 0xFF);
}

TEST_CASE("AY-3-8910: Every Output Stays Inside Zero To One") {
  Renderer r;
  r.write(0, 0x11);
  r.write(2, 0x07);
  r.write(4, 0x03);
  r.write(6, 0x02);
  r.write(7, 0x00);
  r.write(8, 0x1F);
  r.write(9, 0x1F);
  r.write(10, 0x0F);
  r.write(11, 0x01);
  r.write(13, 0x0E);

  r.step(20000);
  for (float sample : r.chunk_a()) {
    CHECK(sample >= 0.0F);
    CHECK(sample <= 1.0F);
  }
}

TEST_CASE("AY-3-8910: Step Never Writes Past The Buffer It Was Given") {
  Ay8910_t chip;
  ay8910_reset(&chip);
  ay8910_write(&chip, 7, 0x3F);
  ay8910_write(&chip, 8, 0x0F);

  std::array<std::array<float, 8>, AY8910_NUM_VOICES> small{};
  std::array<float*, AY8910_NUM_VOICES> pointers = {
      {small[0].data(), small[1].data(), small[2].data()}};
  ay8910_step(&chip, 1000, pointers.data(), 4);

  CHECK(small[0][3] == 1.0F);
  CHECK(small[0][4] == 0.0F);
  CHECK(small[0][7] == 0.0F);
}

TEST_CASE("AY-3-8910: Null Instance And Null Buffers Are Inert") {
  Ay8910_t chip;
  ay8910_reset(&chip);
  ay8910_reset(nullptr);
  ay8910_write(nullptr, 0, 0xFF);
  ay8910_write(&chip, AY8910_NUM_REGISTERS, 0xFF);
  ay8910_write(&chip, 0xFF, 0xFF);
  ay8910_step(nullptr, 10, nullptr, 0);
  ay8910_step(&chip, 10, nullptr, 0);
  CHECK(chip.regs[0] == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
