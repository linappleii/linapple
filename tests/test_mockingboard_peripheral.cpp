// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include "doctest.h"

#include <cstring>
#include <vector>

#include "LinAppleCore.h"
#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "core/Common_Globals.h"
#include "core/Peripheral.h"

extern uint64_t g_nCumulativeCycles;

// Mock Host Interface
static bool g_irq_asserted = false;
static auto Mock_AssertIrq(int slot, bool assert) -> void {
  (void)slot;
  g_irq_asserted = assert;
}

static PeripheralIOHandler g_read_c0 = nullptr;
static PeripheralIOHandler g_write_c0 = nullptr;
static PeripheralIOHandler g_read_cx = nullptr;
static PeripheralIOHandler g_write_cx = nullptr;
static char g_config_type[16] = "";

static auto Mock_GetConfig(const char* section, const char* key, char* buffer,
                           size_t buffer_size) -> bool {
  if (strcmp(section, "Mockingboard") == 0 && strcmp(key, "Type") == 0) {
    if (g_config_type[0] != '\0') {
      strncpy(buffer, g_config_type, buffer_size);
      return true;
    }
  }
  return false;
}

static auto Mock_RegisterIO(int slot, PeripheralIOHandler r,
                            PeripheralIOHandler w, PeripheralIOHandler cr,
                            PeripheralIOHandler cw) -> void {
  (void)slot;
  g_read_c0 = r;
  g_write_c0 = w;
  g_read_cx = cr;
  g_write_cx = cw;
}

static HostInterface_t g_mock_host = {
    .AssertIrq = Mock_AssertIrq,
    .RegisterIO = Mock_RegisterIO,
    .GetConfig = Mock_GetConfig,
};

TEST_CASE("Mockingboard Peripheral: Standard Mode") {
  g_nCumulativeCycles = 10000;
  g_fCurrentCLK6502 = 1022727.0;
  g_irq_asserted = false;
  g_read_c0 = nullptr;
  g_write_c0 = nullptr;
  g_read_cx = nullptr;
  g_write_cx = nullptr;
  g_config_type[0] = '\0';
  auto* descriptor = Mockingboard_GetDescriptor();
  void* instance = descriptor->init(4, &g_mock_host);
  REQUIRE(instance != nullptr);
  descriptor->reset(instance);

  SUBCASE("Reset: Ensure IRQs are deasserted and registers are cleared") {
    descriptor->reset(instance);
    CHECK(g_irq_asserted == false);
  }

  SUBCASE("State Persistence: Save and Load register state") {
    REQUIRE(g_write_cx != nullptr);
    REQUIRE(g_read_cx != nullptr);

    // Set a register in VIA A (e.g. DDRB at offset 2)
    g_write_cx(instance, 0, 0xC002, 1, 0x55, 0);

    size_t state_size = 0;
    descriptor->save_state(instance, nullptr, &state_size);
    REQUIRE(state_size > 0);

    std::vector<uint8_t> buffer(state_size);
    REQUIRE(descriptor->save_state(instance, buffer.data(),
                                                &state_size) == PERIPHERAL_OK);

    // Reset state and verify it's cleared
    descriptor->reset(instance);
    CHECK(g_read_cx(instance, 0, 0xC002, 0, 0, 0) == 0);

    // Load state and verify it's restored
    REQUIRE(descriptor->load_state(instance, buffer.data(),
                                                state_size) == PERIPHERAL_OK);

    CHECK(g_read_cx(instance, 0, 0xC002, 0, 0, 0) == 0x55);
  }

  SUBCASE("VIA Timer 1: One-shot IRQ timing and acknowledgment") {
    descriptor->reset(instance);
    REQUIRE(g_write_cx != nullptr);

    // 1. Enable T1 interrupt in IER ($E) - Bit 6 + Bit 7 (SET bit)
    g_write_cx(instance, 0, 0xC00E, 1, 0xC0, 0);

    // 2. Set T1 period to 1000 cycles
    // T1L-L ($4) = 0xE8
    g_write_cx(instance, 0, 0xC004, 1, 0xE8, 0);
    // T1H-C ($5) = 0x03 (This starts the timer)
    g_write_cx(instance, 0, 0xC005, 1, 0x03, 0);

    CHECK(g_irq_asserted == false);

    // 3. Advance cycles partially
    g_nCumulativeCycles += 500;
    descriptor->think(instance, 0);
    CHECK(g_irq_asserted == false);

    // 4. Advance cycles beyond period
    g_nCumulativeCycles += 600; // Total 1100 > 1000
    descriptor->think(instance, 0);
    CHECK(g_irq_asserted == true);

    // 5. Clear IRQ by reading T1L-L ($4)
    g_read_cx(instance, 0, 0xC004, 0, 0, 0);
    descriptor->think(instance, 0);
    CHECK(g_irq_asserted == false);
  }

  SUBCASE("VIA Timer 1: Continuous mode periodicity") {
    descriptor->reset(instance);
    REQUIRE(g_write_cx != nullptr);

    // Set Continuous Mode in ACR (Bit 6 = 1)
    g_write_cx(instance, 0, 0xC00B, 1, 0x40, 0);
    // Enable T1 IRQ
    g_write_cx(instance, 0, 0xC00E, 1, 0xC0, 0);

    // Set period to 500 cycles (> 255 limitation)
    g_write_cx(instance, 0, 0xC004, 1, 0xF4, 0); // 500 & 0xFF = 0xF4
    g_write_cx(instance, 0, 0xC005, 1, 0x01, 0); // 500 >> 8 = 1

    // First underflow
    g_nCumulativeCycles += 501;
    descriptor->think(instance, 0);
    CHECK(g_irq_asserted == true);

    // Ack IRQ
    g_read_cx(instance, 0, 0xC004, 0, 0, 0);
    descriptor->think(instance, 0);
    CHECK(g_irq_asserted == false);

    // Second underflow (continuous mode should reload)
    g_nCumulativeCycles += 501;
    descriptor->think(instance, 0);
    CHECK(g_irq_asserted == true);
  }

  SUBCASE("AY-3-8910: Complex interaction via VIA registers") {
    // Set DDRs to output
    g_write_cx(instance, 0, 0xC002, 1, 0xFF, 0); // DDRB
    g_write_cx(instance, 0, 0xC003, 1, 0xFF, 0); // DDRA

    // Reset chips (Bit 2 of ORB high)
    g_write_cx(instance, 0, 0xC000, 1, 0x04, 0);

    // 1. Latch AY register 7 (Mixer)
    // ORA = 7
    g_write_cx(instance, 0, 0xC001, 1, 0x07, 0);
    // ORB = func_latch (BDIR=1, BC1=1) + RESET_N=1 -> 0x03 | 0x04 = 0x07
    g_write_cx(instance, 0, 0xC000, 1, 0x07, 0);
    // ORB = Inactive (BDIR=0, BC1=0) + RESET_N=1 -> 0x04
    g_write_cx(instance, 0, 0xC000, 1, 0x04, 0);

    // 2. Write 0x3F to Mixer
    // ORA = 0x3F
    g_write_cx(instance, 0, 0xC001, 1, 0x3F, 0);
    // ORB = func_write (BDIR=1, BC1=0) + RESET_N=1 -> 0x02 | 0x04 = 0x06
    g_write_cx(instance, 0, 0xC000, 1, 0x06, 0);
    // ORB = Inactive
    g_write_cx(instance, 0, 0xC000, 1, 0x04, 0);

    // 3. Verify AY register persists through state cycle
    size_t state_size = 0;
    descriptor->save_state(instance, nullptr, &state_size);
    std::vector<uint8_t> buffer(state_size);
    descriptor->save_state(instance, buffer.data(), &state_size);

    descriptor->reset(instance);
    REQUIRE(descriptor->load_state(instance, buffer.data(), state_size) == PERIPHERAL_OK);
  }

  descriptor->shutdown(instance);
}

TEST_CASE("Mockingboard Peripheral: Phasor Card Mode") {
  g_nCumulativeCycles = 10000;
  g_fCurrentCLK6502 = 1022727.0;
  g_irq_asserted = false;
  g_read_c0 = nullptr;
  g_write_c0 = nullptr;
  g_read_cx = nullptr;
  g_write_cx = nullptr;
  strcpy(g_config_type, "Phasor");

  auto* descriptor = Mockingboard_GetDescriptor();
  void* instance = descriptor->init(4, &g_mock_host);
  REQUIRE(instance != nullptr);
  descriptor->reset(instance);

  SUBCASE("Phasor: Native Mode Detection and chip selection") {
    REQUIRE(g_read_c0 != nullptr);
    // Access C0nX range (Phasor native select)
    // addr = $C0C1 (bit 0 high triggers native mode)
    g_read_c0(instance, 0, 0xC0C1, 0, 0, 0);

    // In native mode, addr bit 3 and 2 select the chip
    // cs = ((addr & 0x08) >> 2) | ((addr & 0x04) >> 2)
    // Select Chip A via bit 2 (cs=1). Use register 6 (T1L-H).
    g_write_c0(instance, 0, 0xC0C6, 1, 0xAA, 0);
    CHECK(g_read_c0(instance, 0, 0xC0C6, 0, 0, 0) == 0xAA);

    // Select Chip B via bit 3 (cs=2). Use register 0xA (SR).
    g_write_c0(instance, 0, 0xC0CA, 1, 0xBB, 0);
    CHECK(g_read_c0(instance, 0, 0xC0CA, 0, 0, 0) == 0xBB);
  }

  SUBCASE("Phasor: State preservation including card type") {
    // Set native mode
    g_read_c0(instance, 0, 0xC0C1, 0, 0, 0);
    // Write something in native mode
    g_write_c0(instance, 0, 0xC0C6, 1, 0xAA, 0);

    size_t state_size = 0;
    descriptor->save_state(instance, nullptr, &state_size);
    std::vector<uint8_t> buffer(state_size);
    descriptor->save_state(instance, buffer.data(), &state_size);

    descriptor->reset(instance);
    // After reset, native mode should be false
    // Write something else to Chip B (which C0C6 maps to when NOT in native mode)
    g_write_c0(instance, 0, 0xC0C6, 1, 0xEE, 0);

    REQUIRE(descriptor->load_state(instance, buffer.data(), state_size) == PERIPHERAL_OK);

    // Verify native mode restored by checking chip selection
    // Reg 6 of Chip A should have what we wrote in native mode
    CHECK(g_read_c0(instance, 0, 0xC0C6, 0, 0, 0) == 0xAA);
  }

  descriptor->shutdown(instance);
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
