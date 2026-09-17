// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/CPU.h"
#include "apple2/peripherals/Peripheral.h"
#include "core/LinAppleCore.h"
#include "test_fixtures.h"

namespace TestFixtures {

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
    peripheral_manager_init();
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

 private:
  ScopedCpuContext_t cpu_;
};

}  // namespace TestFixtures
