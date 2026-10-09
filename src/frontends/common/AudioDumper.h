// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>

#include "core/Util_Path.h"

struct AudioDumper {
 public:
  AudioDumper() noexcept;
  ~AudioDumper();

  AudioDumper(const AudioDumper&) = delete;
  auto operator=(const AudioDumper&) -> AudioDumper& = delete;
  AudioDumper(AudioDumper&& other) noexcept;
  auto operator=(AudioDumper&& other) noexcept -> AudioDumper&;

  auto initialize(const char* filename, uint32_t sample_rate,
                  uint32_t num_channels) -> bool;
  auto put_samples(const int16_t* buf, uint32_t num_samples) -> bool;
  auto finalize() -> void;
  auto is_active() const -> bool;

 private:
  auto finalize_unlocked() -> void;

  FilePtr file_{nullptr, fclose};
  uint32_t total_offset_{0};
  uint32_t data_offset_{0};
  uint32_t total_bytes_written_{0};
  uint32_t num_channels_{2};
  mutable std::mutex mutex_;
};

auto audio_dumper_initialize(AudioDumper* dumper, const char* filename,
                             uint32_t sample_rate, uint32_t num_channels)
    -> int;

auto audio_dumper_put_samples(AudioDumper* dumper, const int16_t* buf,
                              uint32_t num_samples) -> int;

auto audio_dumper_finalize(AudioDumper* dumper) -> int;

auto audio_dumper_is_active(const AudioDumper* dumper) -> bool;
