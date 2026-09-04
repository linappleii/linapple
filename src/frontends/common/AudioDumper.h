// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>

#include "core/Util_Path.h"

struct AudioDumper_t {
 public:
  AudioDumper_t() noexcept;
  ~AudioDumper_t();

  AudioDumper_t(const AudioDumper_t&) = delete;
  auto operator=(const AudioDumper_t&) -> AudioDumper_t& = delete;
  AudioDumper_t(AudioDumper_t&& other) noexcept;
  auto operator=(AudioDumper_t&& other) noexcept -> AudioDumper_t&;

  auto initialize(const char* filename, uint32_t sample_rate,
                  uint32_t num_channels) -> bool;
  auto put_samples(const int16_t* buf, uint32_t num_samples) -> bool;
  auto finalize() -> void;
  auto is_active() const -> bool;

 private:
  auto finalize_unlocked() -> void;

  FilePtr_t file_{nullptr, fclose};
  uint32_t total_offset_{0};
  uint32_t data_offset_{0};
  uint32_t total_bytes_written_{0};
  uint32_t num_channels_{2};
  mutable std::mutex mutex_{};
};

auto audio_dumper_initialize(AudioDumper_t* dumper, const char* filename,
                             uint32_t sample_rate, uint32_t num_channels)
    -> int;

auto audio_dumper_put_samples(AudioDumper_t* dumper, const int16_t* buf,
                              uint32_t num_samples) -> int;

auto audio_dumper_finalize(AudioDumper_t* dumper) -> int;
