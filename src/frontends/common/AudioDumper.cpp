// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/AudioDumper.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

namespace {

constexpr uint32_t fmt_chunk_size = 16;
constexpr uint16_t bits_per_sample = 16;
constexpr uint16_t pcm_format_tag = 1;

auto write_u16_le(FILE* f, uint16_t val) -> bool {
  uint8_t buf[2] = {static_cast<uint8_t>(val & 0xFF),
                    static_cast<uint8_t>((val >> 8) & 0xFF)};
  return fwrite(buf, 1, 2, f) == 2;
}

auto write_u32_le(FILE* f, uint32_t val) -> bool {
  uint8_t buf[4] = {static_cast<uint8_t>(val & 0xFF),
                    static_cast<uint8_t>((val >> 8) & 0xFF),
                    static_cast<uint8_t>((val >> 16) & 0xFF),
                    static_cast<uint8_t>((val >> 24) & 0xFF)};
  return fwrite(buf, 1, 4, f) == 4;
}

}  // namespace

AudioDumper_t::AudioDumper_t() = default;

AudioDumper_t::~AudioDumper_t() { finalize(); }

AudioDumper_t::AudioDumper_t(AudioDumper_t&& other) noexcept {
  std::lock_guard<std::mutex> lock(other.mutex_);
  file_ = std::move(other.file_);
  total_offset_ = other.total_offset_;
  data_offset_ = other.data_offset_;
  total_bytes_written_ = other.total_bytes_written_;
  num_channels_ = other.num_channels_;
}

auto AudioDumper_t::operator=(AudioDumper_t&& other) noexcept
    -> AudioDumper_t& {
  if (this != &other) {
    std::unique_lock<std::mutex> lock_this(mutex_, std::defer_lock);
    std::unique_lock<std::mutex> lock_other(other.mutex_, std::defer_lock);
    std::lock(lock_this, lock_other);

    finalize_unlocked();

    file_ = std::move(other.file_);
    total_offset_ = other.total_offset_;
    data_offset_ = other.data_offset_;
    total_bytes_written_ = other.total_bytes_written_;
    num_channels_ = other.num_channels_;
  }
  return *this;
}

auto AudioDumper_t::initialize(const char* filename, uint32_t sample_rate,
                               uint32_t num_channels) -> bool {
  if (filename == nullptr || sample_rate == 0 || num_channels == 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  finalize_unlocked();

  file_.reset(fopen(filename, "wb"));
  if (file_ == nullptr) {
    return false;
  }

  num_channels_ = num_channels;

  if (fwrite("RIFF", 1, 4, file_.get()) != 4) {
    file_.reset();
    return false;
  }

  const long total_pos = ftell(file_.get());
  if (total_pos < 0) {
    file_.reset();
    return false;
  }
  total_offset_ = static_cast<uint32_t>(total_pos);
  if (!write_u32_le(file_.get(), 0)) {
    file_.reset();
    return false;
  }

  if (fwrite("WAVEfmt ", 1, 8, file_.get()) != 8) {
    file_.reset();
    return false;
  }

  if (!write_u32_le(file_.get(), fmt_chunk_size)) {
    file_.reset();
    return false;
  }

  if (!write_u16_le(file_.get(), pcm_format_tag)) {
    file_.reset();
    return false;
  }

  if (!write_u16_le(file_.get(), static_cast<uint16_t>(num_channels))) {
    file_.reset();
    return false;
  }

  if (!write_u32_le(file_.get(), sample_rate)) {
    file_.reset();
    return false;
  }

  const uint32_t byte_rate = sample_rate * 2 * num_channels;
  if (!write_u32_le(file_.get(), byte_rate)) {
    file_.reset();
    return false;
  }

  const uint16_t block_align = static_cast<uint16_t>(2 * num_channels);
  if (!write_u16_le(file_.get(), block_align)) {
    file_.reset();
    return false;
  }

  if (!write_u16_le(file_.get(), bits_per_sample)) {
    file_.reset();
    return false;
  }

  if (fwrite("data", 1, 4, file_.get()) != 4) {
    file_.reset();
    return false;
  }

  const long data_pos = ftell(file_.get());
  if (data_pos < 0) {
    file_.reset();
    return false;
  }
  data_offset_ = static_cast<uint32_t>(data_pos);
  if (!write_u32_le(file_.get(), 0)) {
    file_.reset();
    return false;
  }

  const long current_pos = ftell(file_.get());
  total_bytes_written_ =
      (current_pos >= 0) ? static_cast<uint32_t>(current_pos) : 0;

  return true;
}

auto AudioDumper_t::put_samples(const int16_t* buf, uint32_t num_samples)
    -> bool {
  if (buf == nullptr || num_samples == 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (file_ == nullptr) {
    return false;
  }

  const size_t bytes_to_write =
      static_cast<size_t>(num_samples) * sizeof(int16_t);
  const size_t written = fwrite(buf, 1, bytes_to_write, file_.get());
  total_bytes_written_ += static_cast<uint32_t>(written);

  return written == bytes_to_write;
}

auto AudioDumper_t::finalize_unlocked() -> void {
  if (file_ == nullptr) {
    return;
  }

  // Update total size in RIFF chunk header
  if (total_bytes_written_ >= (total_offset_ + 4)) {
    const uint32_t riff_size = total_bytes_written_ - (total_offset_ + 4);
    if (fseek(file_.get(), static_cast<long>(total_offset_), SEEK_SET) == 0) {
      write_u32_le(file_.get(), riff_size);
    }
  }

  // Update data chunk size in data subchunk header
  if (total_bytes_written_ >= (data_offset_ + 4)) {
    const uint32_t data_size = total_bytes_written_ - (data_offset_ + 4);
    if (fseek(file_.get(), static_cast<long>(data_offset_), SEEK_SET) == 0) {
      write_u32_le(file_.get(), data_size);
    }
  }

  fflush(file_.get());
  file_.reset();
}

auto AudioDumper_t::finalize() -> void {
  std::lock_guard<std::mutex> lock(mutex_);
  finalize_unlocked();
}

auto AudioDumper_t::is_active() const -> bool {
  std::lock_guard<std::mutex> lock(mutex_);
  return file_ != nullptr;
}

auto audio_dumper_initialize(AudioDumper_t* dumper, const char* filename,
                             uint32_t sample_rate, uint32_t num_channels)
    -> int {
  if (dumper == nullptr) return 1;
  return dumper->initialize(filename, sample_rate, num_channels) ? 0 : 1;
}

auto audio_dumper_put_samples(AudioDumper_t* dumper, const int16_t* buf,
                              uint32_t num_samples) -> int {
  if (dumper == nullptr) return 1;
  return dumper->put_samples(buf, num_samples) ? 0 : 1;
}

auto audio_dumper_finalize(AudioDumper_t* dumper) -> int {
  if (dumper == nullptr) return 1;
  dumper->finalize();
  return 0;
}
