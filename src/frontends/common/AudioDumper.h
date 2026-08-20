// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstdio>

struct AudioDumper_t {
  FILE* file = nullptr;
  uint32_t total_offset = 0;
  uint32_t data_offset = 0;
  uint32_t total_bytes_written = 0;
  uint32_t num_channels = 2;
};

auto audio_dumper_initialize(AudioDumper_t* dumper, const char* filename,
                             uint32_t sample_rate, uint32_t num_channels)
    -> int;

auto audio_dumper_put_samples(AudioDumper_t* dumper, const int16_t* buf,
                              uint32_t num_samples) -> int;

auto audio_dumper_finalize(AudioDumper_t* dumper) -> int;
