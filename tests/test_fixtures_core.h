// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "core/LinAppleCore.h"
#include "test_fixtures.h"

namespace TestFixtures {

/** Retrieve snapshot fixture path. */
class ScopedLocalTimeProvider_t {
 public:
  explicit ScopedLocalTimeProvider_t(const HostLocalTime_t& frozen)
      : frozen_(frozen) {
    linapple_set_local_time_provider(answer, this);
  }

  ~ScopedLocalTimeProvider_t() {
    linapple_set_local_time_provider(nullptr, nullptr);
  }

  ScopedLocalTimeProvider_t(const ScopedLocalTimeProvider_t&) = delete;
  auto operator=(const ScopedLocalTimeProvider_t&)
      -> ScopedLocalTimeProvider_t& = delete;
  ScopedLocalTimeProvider_t(ScopedLocalTimeProvider_t&&) = delete;
  auto operator=(ScopedLocalTimeProvider_t&&)
      -> ScopedLocalTimeProvider_t& = delete;

  auto set(const HostLocalTime_t& frozen) -> void { frozen_ = frozen; }
  auto value() const -> const HostLocalTime_t& { return frozen_; }
  auto calls() const -> unsigned { return calls_; }

 private:
  static auto answer(void* ctx, HostLocalTime_t* out) -> bool {
    auto* self = static_cast<ScopedLocalTimeProvider_t*>(ctx);
    if (self == nullptr || out == nullptr) {
      return false;
    }
    ++self->calls_;
    *out = self->frozen_;
    return true;
  }

  HostLocalTime_t frozen_;
  unsigned calls_ = 0;
};

/**
 * @brief RAII swap of the active CPU context.
 *
 * The context is a process global, so a case that leaves a half-run one
 * behind changes what the next case measures.
 */
struct ScopedCpuContext_t {
  CpuInstance_t* previous = nullptr;
  CpuInstance_t fresh{};

  ScopedCpuContext_t() : previous(cpu_get_active_context()) {
    cpu_set_active_context(&fresh);
  }

  ~ScopedCpuContext_t() {
    if (previous != nullptr) {
      cpu_set_active_context(previous);
    }
  }

  ScopedCpuContext_t(const ScopedCpuContext_t&) = delete;
  auto operator=(const ScopedCpuContext_t&) -> ScopedCpuContext_t& = delete;
  ScopedCpuContext_t(ScopedCpuContext_t&&) = delete;
  auto operator=(ScopedCpuContext_t&&) -> ScopedCpuContext_t& = delete;
};

/**
 * @brief RAII emulator core built from a declared machine.
 *
 * The CPU context is a member so that it is installed before linapple_init
 * runs and restored after linapple_shutdown, and every frontend callback the
 * core holds is cleared on the way out: they are process globals, and a
 * dangling one would fire during the next case's shutdown.
 */
class ScopedCore_t {
 public:
  explicit ScopedCore_t(const ScopedTestConfig_t& config) {
    config.load();
    linapple_init();
  }

  ~ScopedCore_t() {
    linapple_set_audio_channel_callback(nullptr);
    linapple_set_audio_source_register_callback(nullptr);
    linapple_set_audio_source_unregister_callback(nullptr);
    linapple_shutdown();
  }

  ScopedCore_t(const ScopedCore_t&) = delete;
  auto operator=(const ScopedCore_t&) -> ScopedCore_t& = delete;
  ScopedCore_t(ScopedCore_t&&) = delete;
  auto operator=(ScopedCore_t&&) -> ScopedCore_t& = delete;

  /**
   * @brief Load bytes where the 6502 will fetch them.
   *
   * The 6502 fetches every opcode and every vector out of the memory image,
   * so that is where the byte has to end up whatever is banked in -- which is
   * the only way a test can point the interrupt vector at its own handler
   * without a language card. Where a write page also exists it gets the byte
   * too, so a later bank switch restores what was written rather than
   * whatever the page held before.
   */
  static auto poke(uint16_t addr, const uint8_t* bytes, size_t count) -> void {
    for (size_t i = 0; i < count; ++i) {
      const auto target = static_cast<uint16_t>(addr + i);
      memdirty[target >> 8] = 0xFF;
      mem[target] = bytes[i];
      uint8_t* page = memwrite[target >> 8];
      if (page != nullptr) {
        page[target & 0xFF] = bytes[i];
      }
    }
  }

  template <size_t N>
  static auto poke(uint16_t addr, const std::array<uint8_t, N>& bytes) -> void {
    poke(addr, bytes.data(), N);
  }

 private:
  ScopedCpuContext_t cpu_;
};

}  // namespace TestFixtures
