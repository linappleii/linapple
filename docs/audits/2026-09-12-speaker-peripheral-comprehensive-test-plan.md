# Apple II Speaker Peripheral: Comprehensive Test Architecture & Verification Plan

**Date:** 2026-09-12  
**Target Peripheral:** `src/apple2/peripherals/speaker/` ([`Speaker.h`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.h), [`Speaker.cpp`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.cpp), [`SpeakerCommands.h`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/SpeakerCommands.h))  
**Target Test Suite:** `test-speaker` ([`tests/test_speaker.cpp`](file:///home/maxolasersquad/code/linapple/tests/test_speaker.cpp))  
**Architectural Seam:** Seam 3 (Core Motherboard ↔ Peripheral C-ABI Contract)  
**Governing Standard:** [`.agents/agents/test_architect.md`](file:///home/maxolasersquad/code/linapple/.agents/agents/test_architect.md)

---

## 1. Architectural Seam & Testing Philosophy

The Apple II Speaker is a Slot 0 internal motherboard peripheral driven by memory-mapped direct I/O address `$C030`. To guarantee **Refactor Invariance** and adhere 100% to the **LinApple Black-Box Contract**:

1. **Seam 3 Isolation**: All interactions with the speaker peripheral must occur strictly through its registered C-ABI descriptor ([`speaker_get_descriptor()`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/Speaker.h#L24)), the [`HostInterface_t`](file:///home/maxolasersquad/code/linapple/src/core/Peripheral_Types.h) bridge, registered Direct I/O callbacks, and the public [`SpeakerCommands.h`](file:///home/maxolasersquad/code/linapple/src/apple2/peripherals/speaker/SpeakerCommands.h) header.
2. **Zero Global Hijacking**: Tests must never access, mock, or mutate emulator core globals (`g_cumulative_cycles`, `g_current_clk_6502`, `g_full_speed`, or direct `mem`).
3. **No Backdoor APIs**: Internal functions (`speaker_generate_samples`, `speaker_get_events`, `speaker_get_last_cycle`) and private structures (`SpeakerPeripheral_t`) remain sealed inside the implementation.
4. **Mental Mutation Rule**: Every test must fail if any production equation, threshold, state transition, or boundary clamp is modified or inverted.
5. **No Tautological or Fuzzy Tests**: Golden constants are used instead of mirrored algorithm formulas. No loose assertions (`> 0`, `!= 0`, `!= nullptr`, or self-satisfying `int16_t` range bounds).
6. **No Branching in Tests**: Test bodies are strictly linear without `if/else`, ternaries, or catch blocks.
7. **Hermetic RAII Fixtures**: All peripheral instances and mock host states are scoped within the `SpeakerHarness_t` fixture, guaranteeing zero resource leaks or cross-test interference.

---

## 2. Complete Functional Inventory of the Speaker Peripheral

The speaker peripheral encompasses 9 distinct operational facets:

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│                             LinApple Speaker Model                               │
├─────────────────────────┬─────────────────────────────┬──────────────────────────┤
│ 1. Registration & ABI   │ 2. Motherboard Bus ($C030)  │ 3. Activity & Inactivity │
│    • Descriptor fields  │    • Read strobe floating   │    • Strobe activation   │
│    • Internal Slot 0    │    • Write strobe ignore    │    • Quiet cycle counter │
│    • Dynamic allocation │    • Flip-flop toggling     │    • Inactivity timeout  │
│    • RAII shutdown      │    • Event queue recording  │    • Boundary deassert   │
├─────────────────────────┼─────────────────────────────┼──────────────────────────┤
│ 4. Sub-Cycle DSP        │ 5. Digital Filtering        │ 6. Host Seam Hygiene     │
│    • 23.19 cyc/sample   │    • First-order DC blocker │    • PCM stereo push     │
│    • Boxcar integration │    • Spindown decay to 0    │    • Exact sample pacing │
│    • Area averaging     │    • Dynamic clamping       │    • Cycle injection     │
├─────────────────────────┼─────────────────────────────┼──────────────────────────┤
│ 7. State Serialization  │ 8. Command & Query ABI      │ 9. Fault Tolerance       │
│    • Two-pass 48-byte   │    • SPEAKER_QUERY_ACTIVE   │    • Cycle underflow     │
│    • Bit-for-bit restore│    • Sizing probe (1 byte)  │    • Queue saturation    │
│    • Pop suppression    │    • Reject undersized      │    • NaN / Inf recovery  │
└─────────────────────────┴─────────────────────────────┴──────────────────────────┘
```

---

## 3. Test Matrix & Detailed Scenario Specifications

The test suite is structured into 9 dedicated `TEST_CASE` blocks comprising 24 distinct verification scenarios:

### Domain 1: Registration, Descriptor & Multi-Card Lifecycle
*Verifies Pillar 1 (Header Isolation), Pillar 2 (Encapsulation), and Seam 3 C-ABI compliance.*

- **TC-01: Identity Descriptor Validation**
  - **Action**: Query `speaker_get_descriptor()`.
  - **Assertions**:
    - `abi_version == LINAPPLE_ABI_VERSION`.
    - `id == "linapple.speaker"`, `name == "Speaker"`.
    - `compatible_slots == PERIPHERAL_MASK_INTERNAL` (`0x01`).
    - `default_slot == 0`.
    - Non-null function pointers: `init`, `shutdown`, `reset`, `think`, `save_state`, `load_state`, `query`.
    - Null function pointers: `command`, `on_vblank`.
- **TC-02: Multi-Instance Isolation & RAII Lifecycle**
  - **Action**: Instantiate two independent speaker instances (`instance1`, `instance2`) via `SpeakerHarness_t`.
  - **Assertions**:
    - Both instance pointers are non-null and distinct (`instance1 != instance2`).
    - Direct I/O registered for both instances cleanly.
    - Strobing `instance1` flips state and asserts activity on `instance1` without mutating `instance2`.
    - Explicit shutdown of `instance1` deallocates cleanly without affecting `instance2`.

---

### Domain 2: Motherboard Direct I/O & Physical Strobe Mechanics
*Verifies Pillar 3 (Bus Seam) and discrete 74LS74 flip-flop hardware fidelity.*

- **TC-03: Direct I/O Read Strobe & Floating Bus Noise**
  - **Action**: Perform memory read to `$C030` with varying simulated video scanner addresses.
  - **Assertions**:
    - Returned byte matches `mem_read_floating_bus(cycles_left)` bit-for-bit.
    - Strobe flag asserts and speaker transitions to active state (`query` returns 1).
- **TC-04: Direct I/O Write Strobe & Data Byte Irrelevance**
  - **Action**: Perform memory writes to `$C030` with distinct data payloads (`0x00`, `0x55`, `0xAA`, `0xFF`).
  - **Assertions**:
    - Returned value matches `mem_read_floating_bus(0)`.
    - Hardware ignores written data; each write triggers cone deflection and maintains active state.
- **TC-05: Alternating Flip-Flop Polarity Verification**
  - **Action**: Execute sequential strobes (`Read`, `Read`, `Write`, `Write`); step clock and generate audio.
  - **Assertions**:
    - First strobe drives positive impulse deflection ($+1.0$).
    - Second strobe drives negative impulse deflection ($-1.0$).
    - Third strobe drives positive impulse deflection ($+1.0$).
    - Fourth strobe drives negative impulse deflection ($-1.0$).

---

### Domain 3: Clock Synchronization & Inactivity Tracking
*Verifies Pillar 6 (Timing & Concurrency) and edge-triggered power management.*

- **TC-06: Inactivity Boundary Threshold (Single-Cycle Precision)**
  - **Hardware Constant**: `speaker_inactivity_cycles = static_cast<uint64_t>(CLOCK_6502 / 5.0)` = `204545` cycles ($\approx 200$ ms).
  - **Action**:
    1. Toggle `$C030`; step `think(0)`. Query active $\rightarrow$ returns 1.
    2. Step clock by exactly $204545$ cycles; step `think(204545)`. Query active $\rightarrow$ returns 1 (boundary holds).
    3. Step clock by exactly 1 additional cycle ($204546$ total); step `think(1)`. Query active $\rightarrow$ returns 0 (deactivated).
  - **Mutation Check**: Off-by-one or condition inversion (`>=` vs `>`) will immediately fail this test.
- **TC-07: Inactivity Extension via Mid-Flight Re-Strobe**
  - **Action**:
    1. Toggle speaker at cycle 0.
    2. Advance $150000$ cycles; re-strobe speaker at cycle $150000$.
    3. Advance by $204545$ cycles to cycle $354545$; step `think()`. Query active $\rightarrow$ returns 1.
    4. Advance by 1 additional cycle to cycle $354546$; step `think()`. Query active $\rightarrow$ returns 0.
  - **Mental Mutation Rule**: Verifies that the re-strobe actually re-armed the quiet timer rather than locking state.

---

### Domain 4: Digital Signal Processing & Filter Mechanics
*Verifies audio waveform fidelity, boxcar area integration, DC filtering, and digital silence.*

- **TC-08: Inactive Digital Silence & Deterministic Sample Pacing**
  - **Action**: Call `think(instance, 1000)` while the speaker is inactive without preceding strobes.
  - **Assertions**:
    - Sample count is deterministically 88 samples (44 stereo pairs for 1000 cycles at 44.1 kHz, $T_{sample} \approx 23.1399$ cycles).
    - Every generated sample in the buffer is strictly `0` (zero power dissipation).
- **TC-09: Single Impulse Step Response & Golden Filter Decay**
  - **Action**: Toggle speaker at $t=1000$ cycles; step $1000$ cycles via `think()`.
  - **Golden Assertions**:
    - Stereo channel matching: Left and Right channels match bit-for-bit across all samples ($L[i] == R[i]$).
    - Peak step amplitude: `samples[0] == 16384` (`SPEAKER_PEAK_AMPLITUDE`), `samples[1] == 16384`.
    - Exact DC filter decay constants ($0.999$ decay coefficient per sample):
      - `samples[2] == 16367`
      - `samples[3] == 16367`
      - `samples[4] == 16351`
      - `samples[5] == 16351`
    - Monotonic decay: `samples[0] > samples[2]` and `samples[2] > samples[4]`.
- **TC-10: Sub-Cycle Boxcar Area Integration (Fractional Duty Cycle)**
  - **Action**: Within the first sample window $[0, 23.139908)$, trigger Strobe 1 at cycle 6 (flips to state=true) and Strobe 2 at cycle 17 (flips to state=false).
  - **Mathematical Golden Assertion**:
    - High duration = $17 - 6 = 11$ cycles. Low duration = $6 + (23.139908 - 17) = 12.139908$ cycles.
    - Area $sum = 11 \times (+1.0) + 12.139908 \times (-1.0) = -1.139908$.
    - Area $average = -1.139908 / 23.139908 = -0.0492615$.
    - Filter state $= -0.0492615$. Scaled output $= -0.0492615 \times 16384 = -807.099 \rightarrow -807$.
    - `samples[0] == -807` and `samples[1] == -807`.
- **TC-11: Continuous 1 kHz Audio Tone Golden Synthesis**
  - **Action**: Simulate a 1 kHz square wave by toggling `$C030` every 511 cycles over 22,000 cycles.
  - **Assertions**:
    - Active waveform generated with periodic zero-crossings.
    - Golden samples match exact expected fixture values at fixed sample indices (e.g. index 0, 10, 50, 100).
    - DC blocker suppresses continuous DC accumulation.
- **TC-12: Spindown Decay to Absolute Silence**
  - **Action**: Generate step response; let speaker enter inactive mode; step $2000000$ cycles.
  - **Assertions**:
    - Trailing samples decay smoothly below `filter_epsilon` ($0.001$).
    - `samples.back() == 0` (clean digital silence without persistent floating point DC offset).

---

### Domain 5: Host Seam Decoupling & Output Pacing
*Verifies Pillar 4 (Host Boundary Hygiene) and Seam 3 timing contracts.*

- **TC-13: Host Seam Null Callback Fault Tolerance**
  - **Action**:
    1. Set `host->AudioPushSamples = nullptr`; step `think(instance, 1000)`: completes safely without crash.
    2. Set `host->GetCycles = nullptr`: `get_cycles()` falls back safely to 0 without crashing.
    3. Set `host->RegisterDirectIO = nullptr`: `init()` succeeds without crash.
- **TC-14: Seam 3 Deterministic Frame Pacing & Buffer Ceiling Guard**
  - **Action**:
    1. Advance cycles by 1 standard frame ($17030$ cycles); step `think(instance, 17030)`.
    2. Advance cycles by a massive 1.0-second burst ($1022727$ cycles); step `think(instance, 1022727)`.
  - **Assertions**:
    - Frame pacing produces exactly 1,472 samples (push count = 1).
    - Massive single-call delta safely clamps to internal buffer capacity `speaker_buffer_size - 2 = 16382` samples (push count = 1), guarding against buffer and memory overrun.

---

### Domain 6: Snapshot Persistence & Save-State Round-Trip
*Verifies Pillar 7 (Deterministic Serialization) and snapshot recovery.*

- **TC-15: Two-Pass Sizing Contract**
  - **Action**: Call `save_state(inst, nullptr, &size)`.
  - **Assertions**:
    - Status is `peripheral_ok`.
    - `size == sizeof(SsIoSpeaker_t)` (exactly 48 bytes).
- **TC-16: Bit-for-Bit State Preservation & Cross-Instance Restoration**
  - **Action**: Toggle `instance1`, advance cycles, save state into buffer; create `instance2` (initially inactive); restore buffer into `instance2`.
  - **Assertions**:
    - `instance2` query confirms `is_active == true`.
    - Save `instance2` state into second buffer; `buffer1 == buffer2` byte-for-byte.
- **TC-17: Anti-DC Pop Observable Output Verification**
  - **Action**: Restore snapshot where `last_sample_state == 1` and `filter_state == 0.0f`; call `think()` without new toggles.
  - **Assertions**:
    - Observable audio output via `AudioPushSamples`: `captured_samples()[0] == 0` (clean DC pop suppression; does not emit a spurious $+16384$ pop).
- **TC-18: Pre-Restore Event Queue Purge**
  - **Action**: Strobe `instance2` 500 times, then immediately `load_state` from a quiet snapshot.
  - **Assertions**:
    - Stale pre-load events are completely flushed; `think()` emits silence rather than processing stale queued strobes.

---

### Domain 7: Numerical Hardening, Stress Boundaries & Error Recovery
*Verifies resilience against memory exhaustion, corrupted state, and hardware race conditions.*

- **TC-19: Event Queue Saturation Under High-Frequency Stress**
  - **Action**: Perform $20000$ consecutive toggles within 1 update period (exceeding `max_speaker_events = 16384`).
  - **Assertions**:
    - Latch state toggles unconditionally ($20000 \pmod 2 == 0 \implies$ returned to initial state).
    - Zero buffer overrun, zero heap corruption.
    - `think(20000)` executes cleanly, generating active bipolar audio.
    - Peak samples do not rail/peg continuously at saturation rails (`min > -32768` and `max < 32767`).
- **TC-20: Backwards Clock & Cycle Underflow Clamping**
  - **Action**: Set host cycles to $10$; call `think(instance, 1000)` ($10 < 1000$).
  - **Assertions**:
    - `start_cycle` clamps safely to 0 without unsigned integer underflow.
    - `next_sample_cycle` clamped safely to 0.0.
    - Synthesis completes without infinite loop or NaN.
- **TC-21: Poisoned Save-State Recovery (NaN / Inf / Negative Clock)**
  - **Action**: Deserialize corrupted `SsIoSpeaker_t` with `filter_state = NAN`, `filter_state = +INF`, and `next_sample_cycle = -500.0`.
  - **Assertions**:
    - Deserialization succeeds (`peripheral_ok`).
    - `filter_state` sanitized to `0.0f`.
    - `next_sample_cycle` clamped to current cycle.
    - Subsequent `think()` calls generate valid audio without crashing or NaN propagation.
- **TC-22: Robustness Across Null Pointers and Undersized Buffers**
  - **Assertions**:
    - `init(0, nullptr) == nullptr`.
    - `save_state(inst, buf, &undersized_size) == peripheral_error`.
    - `save_state(inst, buf, nullptr) == peripheral_error`.
    - `load_state(inst, buf, undersized_size) == peripheral_error`.
    - `load_state(inst, buf, oversized_size) == peripheral_error` (strict size equality).
    - `load_state(inst, nullptr, 48) == peripheral_error`.
    - `query(inst, query_id, &u8, &undersized_size) == peripheral_error`.
    - `query(inst, query_id, &u8, nullptr) == peripheral_error`.
    - `query(inst, 0xFFFF, &u8, &size) == peripheral_incompatible`.
    - Passing `nullptr` as instance to `shutdown`, `reset`, `think`, `save_state`, `load_state`, `query`, and Direct I/O handler returns clean error codes or safe no-op.
    - Direct I/O with null instance returns `mem_read_floating_bus(0)`.
- **TC-23: Multiple Strobes in Identical Cycle ($t_1 == t_2$)**
  - **Action**: Execute two consecutive reads to `$C030` at `cycle = 1000` without cycle advancement.
  - **Assertions**:
    - `(event_time - current_time) == 0.0` handled safely without division by zero, NaN, or crash.
    - Flip-flop returns to original polarity ($2$ toggles).
- **TC-24: Immediate Silence on Active Reset**
  - **Action**: Toggle speaker to active oscillation; call `reset(instance)`; step `think(1000)`.
  - **Assertions**:
    - `is_active` deasserts to false.
    - Subsequent `think()` call emits digital silence (all samples strictly `0`).

---

## 4. Test Harness Fixture Architecture (`SpeakerHarness_t`)

```cpp
struct MockDirectIOHandler_t {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;
};

struct SpeakerHarness_t {
  SpeakerHarness_t();
  ~SpeakerHarness_t();

  // Non-copyable, non-movable RAII
  SpeakerHarness_t(const SpeakerHarness_t&) = delete;
  auto operator=(const SpeakerHarness_t&) -> SpeakerHarness_t& = delete;
  SpeakerHarness_t(SpeakerHarness_t&&) = delete;
  auto operator=(SpeakerHarness_t&&) -> SpeakerHarness_t& = delete;

  // Ingress & Lifecycle
  auto host() -> HostInterface_t*;
  auto create_speaker(int slot = 0) -> void*;
  auto shutdown_instance(void* inst) -> void;
  auto primary_instance() const -> void*;

  // Cycle Clock Controls (Pure Seam 3 injection via host->GetCycles)
  auto cycles() const -> uint64_t;
  auto set_cycles(uint64_t c) -> void;
  auto advance_cycles(uint64_t delta) -> void;

  // Host Simulation Flags
  auto set_drop_audio(bool drop) -> void;
  auto set_null_cycles(bool null_cycles) -> void;

  // Bus Direct I/O Ingress with instance targeting
  auto read_io(uint16_t addr, void* inst = nullptr, uint8_t floating_bus = 0) -> uint8_t;
  auto write_io(uint16_t addr, void* inst = nullptr, uint8_t val = 0) -> uint8_t;
  auto toggle_read(void* inst = nullptr, uint8_t floating_bus = 0) -> uint8_t;
  auto toggle_write(void* inst = nullptr, uint8_t val = 0) -> uint8_t;

  // Stepping & Egress Inspection
  auto think(void* inst, uint32_t elapsed_cycles) -> void;
  auto captured_samples() const -> const std::vector<int16_t>&;
  auto audio_push_count() const -> uint32_t;
  auto clear_captured_samples() -> void;
  auto is_active(void* inst = nullptr) const -> bool;

 private:
  HostInterface_t host_{};
  uint64_t cycles_ = 0;
  std::vector<void*> instances_;
  void* primary_instance_ = nullptr;
  std::map<uint16_t, MockDirectIOHandler_t> handlers_;
  std::vector<int16_t> captured_samples_;
  uint32_t audio_push_count_ = 0;

  static SpeakerHarness_t* s_active_harness;
  static auto mock_log(void*, PeripheralLogLevel_t, const char*, ...) -> void;
  static auto mock_assert_irq(int, bool) -> void;
  static auto mock_register_direct_io(void*, uint16_t, PeripheralIOHandler, PeripheralIOHandler) -> void;
  static auto mock_audio_push_samples(void*, const int16_t*, size_t) -> void;
  static auto mock_get_cycles() -> uint64_t;
};
```

---

## 5. Review & Approval Protocol

1. **Test Architect Certification**: Re-audit by `test_architect` subagent to confirm zero anti-patterns, refactor invariance, and complete scenario coverage.
2. **Implementation in `tests/test_speaker.cpp`**: Implement all 24 scenarios cleanly using doctest.
3. **Compilation & CI Verification**: Build `test-speaker` with zero warnings, execute `ctest`, and verify 100% passing assertions.
