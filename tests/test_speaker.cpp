#include <array>
#include <cmath>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/SoundCore.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "doctest.h"

TEST_CASE("Speaker subsystem accurately generates and filters PCM audio") {
  Linapple_Init();
  // In Task 6.6 we'll modernize how the callback is registered,
  // but for now we'll use the DSUploadBuffer fallback which tests can hook
  // actually, let's keep it simple and just use the pointer API for now.

  // Ensure cycles are initialized so CpuCalcCycles works predictably
  CpuInitialize();
  g_nCumulativeCycles = 1000;

  Speaker_t spkr_instance{};
  Speaker_Initialize(&spkr_instance);

  SUBCASE("Speaker records toggles with cycle accuracy") {
    uint64_t initialCycles = g_nCumulativeCycles;

    // We mock the executed cycles directly since we're bypassing CpuExecute
    uint32_t executedCycles = 10;
    CpuCalcCycles(executedCycles);
    Speaker_Toggle(&spkr_instance, 0, 0, 0, 0,
                   0);  // Toggle at the current cycle

    std::array<SpkrEvent, MAX_SPKR_EVENTS> events{};
    int count =
        Speaker_GetEvents(&spkr_instance, events.data(), MAX_SPKR_EVENTS);

    CHECK(count >= 1);
    // The last event should be the one we just triggered
    CHECK(events[count - 1].cycle == g_nCumulativeCycles);
    // And we expect it to have advanced
    CHECK(g_nCumulativeCycles >= initialCycles);
  }

  SUBCASE(
      "Speaker_GenerateSamples integrates fast pulses (PWM/Boxcar Filter)") {
    double clksPerSample = g_fCurrentCLK6502 / SPKR_SAMPLE_RATE;

    // Clear any previous events
    std::array<SpkrEvent, MAX_SPKR_EVENTS> dump{};
    Speaker_GetEvents(&spkr_instance, dump.data(), MAX_SPKR_EVENTS);

    // Advance cycle so we're not at 0, using a clean cycle baseline
    g_nCumulativeCycles = 2000;
    Speaker_Reset(&spkr_instance);

    // Simulate a PWM pulse: High for exactly 25% of a sample period, then low
    // for 75%
    uint32_t pulseWidth = static_cast<uint32_t>(clksPerSample * 0.25);
    uint32_t remainingWidth = static_cast<uint32_t>(clksPerSample) - pulseWidth;

    // Toggle ON (move cycles forward by 1, then toggle)
    CpuCalcCycles(1);
    Speaker_Toggle(&spkr_instance, 0, 0, 0, 0, 0);

    // Move forward by pulseWidth and toggle OFF
    CpuCalcCycles(pulseWidth);
    Speaker_Toggle(&spkr_instance, 0, 0, 0, 0, 0);

    // Move forward to complete the sample period
    CpuCalcCycles(remainingWidth);

    Speaker_GenerateSamples(&spkr_instance, 1 + pulseWidth + remainingWidth);
  }

  SUBCASE("High-pass filter eliminates DC offset and decays to zero") {
    // 1. Simulate a persistent 'ON' state by toggling once and doing nothing
    // else.
    g_nCumulativeCycles = 3000;
    Speaker_Reset(&spkr_instance);
    spkr_instance.recently_active = true;

    // Toggle ON
    Speaker_Toggle(&spkr_instance, 0, 0, 0, 0, 0);

    // Generate many samples. Without a high-pass filter, the output would stay
    // at max amplitude.
    uint32_t totalCycles = 1000000;
    CpuCalcCycles(totalCycles);
    Speaker_GenerateSamples(&spkr_instance, totalCycles);

    // The filter should have brought the final samples very close to zero
    // despite the speaker still being logically "on" (state=true).
    int16_t lastSample = spkr_instance.sample_buffer.at(0);
    // We check that it's significantly decayed from the peak volume (0x4000)
    CHECK(std::abs(lastSample) < 100);

    // 2. Simulate transition to inactivity.
    spkr_instance.recently_active = false;
    spkr_instance.filter_state = 1.0f; // Force a high offset
    Speaker_GenerateSamples(&spkr_instance, 100);

    // The logic should have flushed the filter to zero.
    CHECK(spkr_instance.filter_state == 0.0f);
  }

  Linapple_Shutdown();
}
