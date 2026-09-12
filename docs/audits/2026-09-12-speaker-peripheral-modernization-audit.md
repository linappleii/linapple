# LinApple — Apple II Speaker Peripheral Architecture & Conformance Audit

**Date:** 2026-09-12  
**Auditors:** Peripheral Architect & Test Architect Specialists  
**Standards:** [`AGENTS.md`](file:///home/maxolasersquad/code/linapple/AGENTS.md), [`src/core/Peripheral.h`](file:///home/maxolasersquad/code/linapple/src/core/Peripheral.h), [`.agents/agents/test_architect.md`](file:///home/maxolasersquad/code/linapple/.agents/agents/test_architect.md)  
**Target Subsystem:** Apple II Built-in Speaker Peripheral & Audio Synthesis  
**Target Files:**
- Header: [`src/apple2/peripherals/speaker/Speaker.h`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.h)
- Implementation: [`src/apple2/peripherals/speaker/Speaker.cpp`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp)
- Test Suite: [`tests/test_speaker.cpp`](file:///home/maxolasersquad/code/linapple/tests/test_speaker.cpp)

---

## 1. Executive Summary & Verdict

### Subagent Verdict: CONDITIONALLY CONFORMANT
The Apple II Speaker peripheral module correctly implements the core [`Peripheral_t`](file:///home/maxolasersquad/code/linapple/src/core/Peripheral.h) ABI contract, adheres to modern C-like C++11 LinApple conventions, properly self-registers via `PERIPHERAL_REGISTER`, and encapsulates instance state cleanly without global instance pollution.

However, a dual audit by the **Peripheral Architect** and **Test Architect** identified:
1. **Two Critical Operational Defects**: An unsigned integer cycle underflow that permanently mutes audio output upon early synthesis calls, and float-to-`int16_t` undefined behavior without output clamping that causes negative polarity inversion clicks.
2. **Six Architectural Breaches of Tiered Decoupling**: Direct peripheral dependencies on Tier 3 emulator speed state (`g_full_speed`), Tier 3 core audio mixer headers (`AudioMixer.h`), mutable global clock variables (`g_current_clk_6502`), and uncoordinated mutation of CPU internal cycle counters (`cpu_calc_cycles`).
3. **Memory & Stack Risk**: A ~295 KB stack allocation occurring during each peripheral reset call.
4. **Test Suite Anti-Patterns**: White-box backdoor API exports in `Speaker.h`, conditional branching inside test bodies, and weak/fuzzy threshold assertions in `tests/test_speaker.cpp`.

This document establishes the detailed findings and the definitive 5-stage remediation plan to achieve 100% architectural conformance and test excellence.

---

## 2. Peripheral Conformance Audit Findings

### A. Operational Defects & Numerical Robustness

#### 1. Unsigned Cycle Underflow / Permanent Audio Muting
- **Location:** [`Speaker.cpp:L302-L309`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L302-L309)
- **Mechanism:**
  ```cpp
  const uint64_t start_cycle = get_cycles(speaker_peripheral->host) - elapsed_cycles;
  const uint64_t end_cycle = get_cycles(speaker_peripheral->host);
  if (speaker_peripheral->next_sample_cycle < static_cast<double>(start_cycle)) {
    speaker_peripheral->next_sample_cycle = static_cast<double>(start_cycle);
  }
  ```
- **Vulnerability:** If `get_cycles() < elapsed_cycles` (e.g. at boot, after cycle counter reset, or early frame synthesis), `start_cycle` underflows to $\approx 1.84 \times 10^{19}$. `next_sample_cycle` jumps forward to this astronomical number, causing all subsequent `while (next_sample_cycle <= end_cycle)` checks to evaluate to false. The speaker is permanently muted until cycles reach $1.84 \times 10^{19}$ (~584,000 years).

#### 2. Float to `int16_t` Undefined Behavior & Inversion Clicks
- **Location:** [`Speaker.cpp:L364`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L364), [`L379`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L379)
- **Mechanism:**
  ```cpp
  const auto val = static_cast<int16_t>(speaker_peripheral->filter_state * speaker_sample_volume);
  ```
- **Vulnerability:** `speaker_sample_volume` is `0x4000` (16,384). The DC blocker difference equation $y[n] = x[n] - x[n-1] + R \cdot y[n-1]$ with $R = 0.999$ can produce $|y[n]| \ge 2.0$ upon rapid state reversals (e.g., $1.0 - (-1.0) = 2.0$). When `filter_state >= 2.0f`, `val_float >= 32768.0f`, exceeding `INT16_MAX` (32,767). In C++, casting an out-of-range floating point value to a signed integer is Undefined Behavior (UB). On x86_64, this wraps around to -32768, creating loud negative inversion clicks in the output stream. `std::clamp` is required.

#### 3. Stack Exhaustion Risk During Reset
- **Location:** [`Speaker.cpp:L68`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L68)
- **Mechanism:**
  ```cpp
  *speaker_peripheral = SpeakerPeripheral_t();
  ```
- **Vulnerability:** `SpeakerPeripheral_t` contains an array of 16,384 `SpeakerEvent_t` structs (256 KB) and a 16,384-sample buffer (32 KB), giving `sizeof(SpeakerPeripheral_t) \approx 295` KB. Allocating this temporary on the stack during reset risks stack overflow on constrained threads or platforms with small stack limits (e.g. musl libc default stack of 128KB/512KB).

#### 4. Decay Spindown Timeline Discontinuity
- **Location:** [`Speaker.cpp:L314-L320`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L314-L320), [`L374-L387`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L374-L387)
- **Mechanism:** When `!is_active`, the code first fills the sample buffer up to `end_cycle` with zeros (`0`). Immediately following that, if `filter_state != 0.0f`, it loops decaying `filter_state` and appends non-zero decay samples *after* the silence block without updating `next_sample_cycle`. This desynchronizes the audio timeline and outputs silence followed by an out-of-order decay tail.

#### 5. Unread Event Discard in Event Drain
- **Location:** [`Speaker.cpp:L404-L408`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L404-L408)
- **Mechanism:**
  ```cpp
  const auto count = std::min(speaker_peripheral->event_count, buffer_capacity);
  if (count > 0) {
    std::copy_n(speaker_peripheral->events.begin(), count, event_buffer);
    speaker_peripheral->event_count = 0;
  }
  ```
- **Vulnerability:** If `buffer_capacity < event_count`, unread events are permanently discarded instead of being shifted down by `count`.

#### 6. Snapshot Restore Discontinuity (DC Pop)
- **Location:** [`Speaker.cpp:L218-L237`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L218-L237)
- **Mechanism:** `previous_input` is omitted from `SsIoSpeaker_t` due to legacy `.aws` binary format constraints. On `speaker_load_state`, `previous_input` remains stale/uninitialized, inducing a DC step on the first synthesized sample after load.

---

## 3. Architectural Breaches of Tiered Decoupling

LinApple mandates a decoupled tiered architecture ([`AGENTS.md`](file:///home/maxolasersquad/code/linapple/AGENTS.md)):
- **Tier 1: User (I/O)** ⇆ **Tier 2: Frontend (SDL3/Headless)** ⇆ **Tier 3: Logic (Core Bridge)** ⇆ **Tier 4: Hardware (Registers/Emulation)**.

The speaker peripheral currently commits six direct architectural breaches:

| # | Breach Site | Violating Code | Architectural Rationale |
| :--- | :--- | :--- | :--- |
| **1** | [`Speaker.cpp:L15`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L15) | `#include "core/AudioMixer.h"` | Tier 4 hardware peripheral directly includes Tier 3 Core audio subsystem header solely to obtain `SPKR_SAMPLE_RATE = 44100`. |
| **2** | [`Speaker.cpp:L131`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L131) | `if (!g_full_speed)` | Tier 4 hardware peripheral inspects Tier 3 emulator speed state. Real Apple II speaker hardware has no concept of "emulator warp speed". Warp audio suppression belongs at the host audio callback seam. |
| **3** | [`Speaker.cpp:L97`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L97), [`L297`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L297) | `g_current_clk_6502` | Tier 4 peripheral accesses a mutable global from [`LinAppleCore.h`](file:///home/maxolasersquad/code/linapple/src/core/LinAppleCore.h) rather than hardware constants (`CLOCK_6502`) or host queries. |
| **4** | [`Speaker.cpp:L128`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L128) | `cpu_calc_cycles(remaining_cycles)` | Peripheral I/O handler mutates CPU internal cycle counters inside [`apple2/CPU.cpp`](file:///home/maxolasersquad/code/linapple/src/apple2/CPU.cpp). The CPU/bus bridge is responsible for sub-cycle synchronization prior to handler dispatch. |
| **5** | [`Speaker.cpp:L124`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L124), [`L148`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp#L148) | `mem_read_floating_bus(...)` | Peripheral bypasses [`HostInterface_t`](file:///home/maxolasersquad/code/linapple/src/core/Peripheral.h) to access motherboard floating bus directly from [`apple2/Memory.h`](file:///home/maxolasersquad/code/linapple/src/apple2/Memory.h). |
| **6** | [`Speaker.h:L40-L43`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.h#L40-L43) | `speaker_generate_samples`, `speaker_get_events`, `speaker_get_last_cycle` | Publicly exports raw synthesis functions in the identity header. Codebase analysis confirms these are never called anywhere in `src/` outside `Speaker.cpp` itself; they serve only as white-box test-spy backdoors violating the Black-Box contract. |

---

## 4. Test Suite Audit Findings (`tests/test_speaker.cpp`)

An audit of [`tests/test_speaker.cpp`](file:///home/maxolasersquad/code/linapple/tests/test_speaker.cpp) against Test Architect standards revealed:
1. **Conditional Branching in Tests:** Lines 384–390 execute loops with `if (s > max_sample) ... if (s < min_sample) ...` to find peak amplitudes dynamically instead of using deterministic golden checks.
2. **Weak & Fuzzy Assertions:** Lines 392–393 assert `CHECK(min_sample > 0);` and `CHECK(samples.front() > samples.back());`. Lines 402–403 assert `CHECK(std::abs(last_sample) < DC_BLOCK_THRESHOLD);` and `CHECK(std::abs(last_sample) <= 10);`.
3. **Missing Seam Contracts:**
   - Zero assertions verifying the floating bus return value of `$C030` reads or writes.
   - Zero assertions testing `speaker_get_descriptor()->reset(instance)`.
   - Zero boundary testing of the exact cycle when inactivity triggers (`N = threshold` remains active, `N + 1` deactivates).
   - Zero testing of synthesis behavior when `host->AudioPushSamples` is `nullptr`.

---

## 5. Master Remediation Plan

### Work Package 1: Operational Defects & Numerical Hardening
- **Target:** [`src/apple2/peripherals/speaker/Speaker.cpp`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp)
- **Actions:**
  1. Underflow guard & backward clock resync:
     - Clamp start cycle:
       ```cpp
       const uint64_t end_cycle = get_cycles(speaker_peripheral->host);
       const uint64_t start_cycle = (end_cycle >= elapsed_cycles) ? (end_cycle - elapsed_cycles) : 0;
       ```
     - Resynchronize on backward clock jumps/rewinds:
       ```cpp
       if (speaker_peripheral->next_sample_cycle < static_cast<double>(start_cycle) ||
           speaker_peripheral->next_sample_cycle > static_cast<double>(end_cycle) + (2.0 * cycles_per_sample)) {
         speaker_peripheral->next_sample_cycle = static_cast<double>(start_cycle);
       }
       ```
  2. Add C++11 compliant sample clamping before casting to `int16_t` (`std::clamp` is C++17):
     ```cpp
     const float raw_sample = speaker_peripheral->filter_state * speaker_sample_volume;
     const auto clamped_sample = std::max(-32768.0f, std::min(32767.0f, raw_sample));
     const auto val = static_cast<int16_t>(clamped_sample);
     ```
  3. Eliminate 295 KB stack allocation with complete explicit in-place scalar resets in `speaker_initialize`:
     ```cpp
     speaker_peripheral->event_count = 0;
     speaker_peripheral->current_state = false;
     speaker_peripheral->quiet_cycle_count = 0;
     speaker_peripheral->is_active = false;
     speaker_peripheral->has_strobe = false;
     speaker_peripheral->sound_mode = sound_wave;
     speaker_peripheral->last_sample_state = false;
     speaker_peripheral->filter_state = 0.0f;
     speaker_peripheral->previous_input = 0.0f;
     speaker_peripheral->last_update_cycle = get_cycles(host);
     speaker_peripheral->next_sample_cycle = static_cast<double>(speaker_peripheral->last_update_cycle);
     ```
  4. Blend spindown decay iteratively into trailing silence rather than appending out-of-order blocks.
  5. State restore sanitization:
     - Strict size equality: Enforce `if (buffer_size != required_size) return peripheral_error;` to reject corrupted/truncated snapshots early.
     - Seed `previous_input` from `save_state_ptr->last_sample_state ? 1.0f : -1.0f` during state load to eliminate DC pop.
     - Defensively validate `filter_state` with `std::isfinite()` to guard against corrupted or poisoned save states (`NaN`/`Inf`).
     - Validate `std::isfinite(save_state_ptr->next_sample_cycle)` and non-negativity.
     - Flush event queue on restore (`speaker_peripheral->event_count = 0;`) to prevent stale pre-load events leaking into restored playback.
  6. Unconditional latch toggling: Decouple physical flip-flop state mutation (`current_state = !current_state;`) from event buffer storage capacity so cone polarity doesn't lock up if the event buffer saturates.
  7. Purge dead backdoor extraction code: Completely delete `speaker_get_events()` and `speaker_get_last_cycle()` from both `Speaker.cpp` and `Speaker.h`.

### Work Package 2: Tiered Decoupling & Host Seam Isolation
- **Target:** [`src/core/Peripheral.cpp`](file:///home/maxolasersquad/code/linapple/src/core/Peripheral.cpp) & [`src/apple2/peripherals/speaker/Speaker.cpp`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp)
- **Actions:**
  1. Move warp-speed audio drop logic into `host_audio_push_samples` in `Peripheral.cpp`:
     ```cpp
     if (g_full_speed) {
       return; // Host logic suppresses audio output in warp mode
     }
     ```
  2. Remove `if (!g_full_speed)` from `Speaker.cpp` so the peripheral is a pure, unthrottled hardware simulation.
  3. Define local constant `constexpr uint32_t speaker_sample_rate = 44100;` and remove `#include "core/AudioMixer.h"`.
  4. Precompute physical timing constants at compile-time (`constexpr uint64_t speaker_inactivity_cycles = static_cast<uint64_t>(CLOCK_6502 / 5.0);` and `constexpr double speaker_cycles_per_sample = CLOCK_6502 / static_cast<double>(speaker_sample_rate);`) using `CLOCK_6502` from [`apple2/Apple2Types.h`](file:///home/maxolasersquad/code/linapple/src/apple2/Apple2Types.h). Provide `#ifndef VERSIONSTRING / #define VERSIONSTRING "3.1.0" / #endif` fallback and remove `#include "core/LinAppleCore.h"`.
  5. Delete `cpu_calc_cycles(remaining_cycles);` from `speaker_toggle()`. Remove fallback to `cpu_get_cumulative_cycles()` in `get_cycles()`, returning `0` if `host` or `host->GetCycles` is null. Remove `#include "apple2/CPU.h"`.
  6. Forward-declare `auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;` in `Speaker.cpp` and remove `#include "apple2/Memory.h"`.
  7. Architecture note: Address `$C020` (Cassette Port output toggle) is documented as a deferred Slot 0 item to maintain clean focus on speaker synthesis stabilization.

### Work Package 3: Public API Sanitization & Identity Header Sealing
- **Target:** [`src/apple2/peripherals/speaker/Speaker.h`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.h) & `src/apple2/peripherals/speaker/SpeakerCommands.h`
- **Actions:**
  1. Create `src/apple2/peripherals/speaker/SpeakerCommands.h` matching the project C99 standard (e.g., [`MouseCommands.h`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/mouse/MouseCommands.h)):
     ```c
     // SPDX-License-Identifier: GPL-2.0-only
     #pragma once
     #include <stdint.h>

     #ifdef __cplusplus
     extern "C" {
     #endif

     typedef enum {
       SPEAKER_QUERY_IS_ACTIVE = 0x0100,
       speaker_query_is_active = SPEAKER_QUERY_IS_ACTIVE  // out: uint8_t (0=inactive, 1=active)
     } SpeakerQuery_e;

     #ifdef __cplusplus
     }
     #endif
     ```
  2. Remove `speaker_generate_samples`, `speaker_get_events`, and `speaker_get_last_cycle` from `Speaker.h`. Make `speaker_generate_samples` an internal static helper called strictly via `speaker_think()`.
  3. Move `max_speaker_events`, `speaker_buffer_size`, `SpeakerEvent_t`, `sound_none`, `sound_wave`, and `speaker_sample_volume` into the anonymous namespace of `Speaker.cpp`.
  4. Retain only the C-ABI descriptor getter `speaker_get_descriptor()`, `SsIoSpeaker_t` (needed for snapshot structures), and include `SpeakerCommands.h` in `Speaker.h`.
  5. Standardize `SPEAKER_QUERY_IS_ACTIVE` to use fixed-width `uint8_t` across the C-ABI boundary.
  6. Execution Note: WP1, WP2, WP3, and WP4 will be applied in lockstep so the codebase and test suite remain atomic and never enter an uncompilable intermediate state.

### Work Package 4: Test Suite Modernization (`tests/test_speaker.cpp`)
- **Target:** [`tests/test_speaker.cpp`](file:///home/maxolasersquad/code/linapple/tests/test_speaker.cpp)
- **Actions:**
  1. Refactor `SpeakerHarness` to test purely across Seam 3:
     - Remove harness hijacking of `g_cumulative_cycles`, `g_current_clk_6502`, `g_full_speed`, and `mem`.
     - Drive I/O toggling via registered direct I/O handlers.
     - Step cycle time via `descriptor->think(instance, cycles)`.
     - Capture audio output via `host.AudioPushSamples` mock callback.
     - Query activity via `descriptor->query(instance, speaker_query_is_active, &active_u8, &size)`.
  2. Eliminate all loops containing conditional branches and replace with deterministic point checks.
  3. Eliminate all fuzzy comparisons (`> 0`, `<= 10`) in favor of exact golden samples.
  4. Add regression tests for:
     - Cycle underflow safety (`get_cycles() < elapsed`).
     - Output clipping & clamping under high-frequency toggling.
     - `reset()` invariant clearing.
     - Exact inactivity boundary threshold ($N$ active, $N+1$ inactive).
     - Floating bus return value verification on direct I/O.
     - Null `AudioPushSamples` host callback safety.

### Work Package 5: Multi-Agent Audit & Verification
- **Actions:**
  1. Run `peripheral_architect` subagent to certify full compliance of revised `Speaker.h` and `Speaker.cpp`.
  2. Run `test_architect` subagent to certify `tests/test_speaker.cpp` satisfies all Black-Box and Seam 3 standards.
  3. Build `test-speaker` with zero warnings and execute all test cases via `ctest`.
