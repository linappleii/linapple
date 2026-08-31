// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/AudioDumper.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

constexpr uint32_t fmt_chunk_size = 16;
constexpr uint16_t bits_per_sample = 16;

auto audio_dumper_initialize(AudioDumper_t* dumper, const char* filename,
                             uint32_t sample_rate, uint32_t num_channels)
    -> int {
  if (!dumper || !filename) return 1;

  dumper->file = fopen(filename, "wb");
  if (!dumper->file) {
    return 1;
  }

  dumper->num_channels = num_channels;

  uint32_t temp32 = 0;
  uint16_t temp16 = 0;

  fwrite("RIFF", 1, 4, dumper->file);

  temp32 = 0;  // total size placeholder
  long total_pos = ftell(dumper->file);
  if (total_pos < 0) {
    fclose(dumper->file);
    dumper->file = nullptr;
    return 1;
  }
  dumper->total_offset = static_cast<uint32_t>(total_pos);
  fwrite(&temp32, 1, 4, dumper->file);

  fwrite("WAVE", 1, 4, dumper->file);
  fwrite("fmt ", 1, 4, dumper->file);

  temp32 = fmt_chunk_size;  // format chunk size
  fwrite(&temp32, 1, 4, dumper->file);

  temp16 = 1;  // PCM format category
  fwrite(&temp16, 1, 2, dumper->file);

  temp16 = static_cast<uint16_t>(num_channels);
  fwrite(&temp16, 1, 2, dumper->file);

  temp32 = sample_rate;
  fwrite(&temp32, 1, 4, dumper->file);

  temp32 =
      sample_rate * 2 * num_channels;  // byte rate (16-bit = 2 bytes/sample)
  fwrite(&temp32, 1, 4, dumper->file);

  temp16 = static_cast<uint16_t>(2 * num_channels);  // block align
  fwrite(&temp16, 1, 2, dumper->file);

  temp16 = bits_per_sample;  // bits per sample
  fwrite(&temp16, 1, 2, dumper->file);

  fwrite("data", 1, 4, dumper->file);

  temp32 = 0;  // data size placeholder
  long data_pos = ftell(dumper->file);
  if (data_pos < 0) {
    fclose(dumper->file);
    dumper->file = nullptr;
    return 1;
  }
  dumper->data_offset = static_cast<uint32_t>(data_pos);
  fwrite(&temp32, 1, 4, dumper->file);

  long total_written_pos = ftell(dumper->file);
  dumper->total_bytes_written =
      (total_written_pos >= 0) ? static_cast<uint32_t>(total_written_pos) : 0;

  return 0;
}

auto audio_dumper_put_samples(AudioDumper_t* dumper, const int16_t* buf,
                              uint32_t num_samples) -> int {
  if (!dumper || !dumper->file || !buf) {
    return 1;
  }

  size_t bytes_to_write =
      static_cast<size_t>(num_samples) * sizeof(int16_t) * dumper->num_channels;
  size_t bytes_written = fwrite(buf, 1, bytes_to_write, dumper->file);
  dumper->total_bytes_written += static_cast<uint32_t>(bytes_written);

  return 0;
}

auto audio_dumper_finalize(AudioDumper_t* dumper) -> int {
  if (!dumper || !dumper->file) {
    return 1;
  }

  uint32_t temp32 = 0;

  // Update total size field in RIFF header
  temp32 = dumper->total_bytes_written - (dumper->total_offset + 4);
  fseek(dumper->file, static_cast<long>(dumper->total_offset), SEEK_SET);
  fwrite(&temp32, 1, 4, dumper->file);

  // Update data size field in data subchunk
  temp32 = dumper->total_bytes_written - (dumper->data_offset + 4);
  fseek(dumper->file, static_cast<long>(dumper->data_offset), SEEK_SET);
  fwrite(&temp32, 1, 4, dumper->file);

  fclose(dumper->file);
  dumper->file = nullptr;

  return 0;
}
