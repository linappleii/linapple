// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "core/LinAppleCore.h"
#include "test_fixtures.h"

namespace TestFixtures {

// RAII guard that isolates the active CPU context between tests.
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

// RAII fixture for emulator core initialization and callback teardown.
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

  // Writes bytes to physical memory and active write pages for opcode fetching.
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
