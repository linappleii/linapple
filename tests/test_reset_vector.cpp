// SPDX-License-Identifier: GPL-2.0-only
// Where the 6502 fetches its reset vector from after Ctrl-Reset, per machine.

#include <array>
#include <cstdint>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using Config_t = TestFixtures::ScopedTestConfig_t;

constexpr uint16_t reset_vector_low = 0xFFFC;
constexpr uint16_t reset_vector_high = 0xFFFD;
// The Enhanced //e ROM and the II Plus Autostart ROM both vector here
// (Sather, Understanding the Apple IIe, 2-6; Understanding the Apple II, 2-6).
constexpr uint16_t monitor_reset = 0xFA62;
constexpr uint16_t ram_vector = 0x1234;
constexpr uint16_t program_base = 0x0300;
constexpr uint16_t rdlcram_copy = 0x03F0;
constexpr uint16_t addr_rdlcram = 0xC012;
constexpr uint8_t high_ram_read_bit = 0x80;
constexpr uint32_t program_cycle_cap = 200;

// Two odd reads of $C083 read-enable high RAM bank 2 and arm then grant the
// write, so the STAs land in the card's RAM at the vector. RDLCRAM is copied
// out last so a case can see which bank the program ran under.
constexpr std::array<uint8_t, 22> program = {
    0xAD, 0x83, 0xC0,  // LDA $C083
    0xAD, 0x83, 0xC0,  // LDA $C083
    0xA9, 0x34,        // LDA #$34
    0x8D, 0xFC, 0xFF,  // STA $FFFC
    0xA9, 0x12,        // LDA #$12
    0x8D, 0xFD, 0xFF,  // STA $FFFD
    0xAD, 0x12, 0xC0,  // LDA $C012
    0x8D, 0xF0, 0x03,  // STA $03F0
};
constexpr uint16_t program_end =
    static_cast<uint16_t>(program_base + program.size());

struct Model_t {
  Apple2Type_t type;
  int config_machine;
};

constexpr Model_t enhanced_2e = {A2TYPE_APPLE2EENHANCED,
                                 Config_t::machine_apple2e_enhanced};
constexpr Model_t ii_plus = {A2TYPE_APPLE2PLUS, Config_t::machine_apple2_plus};
constexpr std::array<Model_t, 2> both_models = {{enhanced_2e, ii_plus}};

// The core takes its model from the process global a frontend sets before
// linapple_init, not from the config file, so the guard sets it the same way
// and puts the previous model back for the next case.
struct ScopedModel_t {
  Apple2Type_t previous{linapple_get_apple2_type()};

  explicit ScopedModel_t(Apple2Type_t type) { linapple_set_apple2_type(type); }
  ~ScopedModel_t() { linapple_set_apple2_type(previous); }

  ScopedModel_t(const ScopedModel_t&) = delete;
  auto operator=(const ScopedModel_t&) -> ScopedModel_t& = delete;
  ScopedModel_t(ScopedModel_t&&) = delete;
  auto operator=(ScopedModel_t&&) -> ScopedModel_t& = delete;
};

auto describe(int config_machine) -> Config_t::Description_t {
  Config_t::Description_t description = Config_t::enhanced_2e_only();
  description.machine_type = config_machine;
  return description;
}

// The machine as a frontend brings it up, then powered on.
struct Machine_t {
  ScopedModel_t model;
  Config_t config;
  TestFixtures::ScopedCore_t core;

  explicit Machine_t(const Model_t& selected)
      : model(selected.type),
        config(describe(selected.config_machine)),
        core(config) {
    linapple_reset_hard();
  }
};

// The vector as the 6502 sees it: the memory image is what cpu_reset reads.
auto vector_in_image() -> uint16_t {
  return static_cast<uint16_t>(mem[reset_vector_low] |
                               (mem[reset_vector_high] << 8));
}

auto read_rdlcram() -> uint8_t {
  return io_map_dispatch(program_base, addr_rdlcram, 0, 0, 0);
}

auto bank_ram_in_and_overwrite_vector() -> void {
  TestFixtures::ScopedCore_t::poke(program_base, program);
  TestFixtures::enter_at({program_base, 0, 0, 0});
  TestFixtures::step_until_pc(program_end, program_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == program_end);
  REQUIRE(vector_in_image() == ram_vector);
}

}  // namespace

// The //e MMU has no RESET' pin. It recognises the 6502's reset fingerprint,
// three page-1 accesses then $FFFC, and turns every soft switch off as that
// fetch begins, so the vector comes out of ROM however high RAM was banked
// (Sather, Understanding the Apple IIe, 4-14 to 4-15, 5-23 and 5-29).
TEST_CASE("Soft reset: the Enhanced //e fetches the vector from ROM") {
  Machine_t machine(enhanced_2e);
  REQUIRE(linapple_get_apple2_type() == enhanced_2e.type);
  REQUIRE(vector_in_image() == monitor_reset);

  bank_ram_in_and_overwrite_vector();
  REQUIRE((mem[rdlcram_copy] & high_ram_read_bit) != 0);

  linapple_reset_soft();

  CHECK(cpu_get_registers()->pc == monitor_reset);
  CHECK(vector_in_image() == monitor_reset);
  CHECK((read_rdlcram() & high_ram_read_bit) == 0);
}

// RESET' at pin 31 of the 16K RAM card performs no hardware function; only
// the power-up circuit on the card and the reset handler configure it, so a
// II Plus fetches the vector from whatever is read-enabled (Sather,
// Understanding the Apple II, 5-28 and 5-30).
TEST_CASE("Soft reset: the II Plus fetches the vector from the RAM card") {
  Machine_t machine(ii_plus);
  REQUIRE(linapple_get_apple2_type() == ii_plus.type);
  REQUIRE(vector_in_image() == monitor_reset);

  bank_ram_in_and_overwrite_vector();

  linapple_reset_soft();

  CHECK(cpu_get_registers()->pc == ram_vector);
  CHECK(vector_in_image() == ram_vector);
}

// Power-up read-disables high RAM on both: the MMU reset on the //e, the R5/C1
// circuit on the RAM card (Sather, Understanding the Apple II, 5-28).
TEST_CASE("Hard reset: both machines fetch the vector from ROM") {
  for (const Model_t& selected : both_models) {
    CAPTURE(selected.type);
    Machine_t machine(selected);
    bank_ram_in_and_overwrite_vector();

    linapple_reset_hard();

    CHECK(cpu_get_registers()->pc == monitor_reset);
    CHECK(vector_in_image() == monitor_reset);
  }
}
