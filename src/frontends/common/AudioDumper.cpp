// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/AudioDumper.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <utility>

#include "core/Util_Endian.h"

namespace {

constexpr uint32_t fmt_chunk_size = 16;
constexpr uint16_t bits_per_sample = 16;
constexpr uint16_t pcm_format_tag = 1;
constexpr uint32_t riff_size_offset = 4;
constexpr uint32_t data_size_offset = 40;

#pragma pack(push, 1)
struct WavHeader {
  uint8_t riff_id[4] = {'R', 'I', 'F', 'F'};
  uint8_t riff_size[4] = {0, 0, 0, 0};
  uint8_t wave_id[4] = {'W', 'A', 'V', 'E'};
  uint8_t fmt_id[4] = {'f', 'm', 't', ' '};
  uint8_t fmt_size[4] = {16, 0, 0, 0};
  uint8_t audio_format[2] = {1, 0};
  uint8_t num_channels[2] = {2, 0};
  uint8_t sample_rate[4] = {0, 0, 0, 0};
  uint8_t byte_rate[4] = {0, 0, 0, 0};
  uint8_t block_align[2] = {0, 0};
  uint8_t bits_per_sample[2] = {16, 0};
  uint8_t data_id[4] = {'d', 'a', 't', 'a'};
  uint8_t data_size[4] = {0, 0, 0, 0};
};
#pragma pack(pop)
static_assert(sizeof(WavHeader) == 44, "WavHeader must be exactly 44 bytes");

auto write_file_u32_le(FILE* f, uint32_t val) -> bool {
  uint8_t buf[4];
  write_u32_le(buf, val);
  return fwrite(buf, 1, 4, f) == 4;
}

}  // namespace

AudioDumper::AudioDumper() noexcept = default;

AudioDumper::~AudioDumper() { finalize(); }

// NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer) - requires mutex lock before member transfer
AudioDumper::AudioDumper(AudioDumper&& other) noexcept {
  std::lock_guard<std::mutex> lock(other.mutex_);
  file_ = std::move(other.file_);
  total_offset_ = other.total_offset_;
  data_offset_ = other.data_offset_;
  total_bytes_written_ = other.total_bytes_written_;
  num_channels_ = other.num_channels_;
}
// NOLINTEND(cppcoreguidelines-prefer-member-initializer)

auto AudioDumper::operator=(AudioDumper&& other) noexcept -> AudioDumper& {
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

auto AudioDumper::initialize(const char* filename, uint32_t sample_rate,
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
  total_offset_ = riff_size_offset;
  data_offset_ = data_size_offset;

  WavHeader header{};
  write_u32_le(header.fmt_size, fmt_chunk_size);
  write_u16_le(header.audio_format, pcm_format_tag);
  write_u16_le(header.num_channels, static_cast<uint16_t>(num_channels));
  write_u32_le(header.sample_rate, sample_rate);

  const uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
  write_u32_le(header.byte_rate, byte_rate);

  const uint16_t block_align =
      static_cast<uint16_t>(num_channels * (bits_per_sample / 8));
  write_u16_le(header.block_align, block_align);
  write_u16_le(header.bits_per_sample, bits_per_sample);

  if (fwrite(&header, 1, sizeof(header), file_.get()) != sizeof(header)) {
    file_.reset();
    return false;
  }

  total_bytes_written_ = sizeof(header);
  return true;
}

auto AudioDumper::put_samples(const int16_t* buf, uint32_t num_samples)
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

auto AudioDumper::finalize_unlocked() -> void {
  if (file_ == nullptr) {
    return;
  }

  if (total_bytes_written_ >= (total_offset_ + 4)) {
    const uint32_t riff_size = total_bytes_written_ - (total_offset_ + 4);
    if (fseek(file_.get(), static_cast<long>(total_offset_), SEEK_SET) == 0) {
      write_file_u32_le(file_.get(), riff_size);
    }
  }

  if (total_bytes_written_ >= (data_offset_ + 4)) {
    const uint32_t data_size = total_bytes_written_ - (data_offset_ + 4);
    if (fseek(file_.get(), static_cast<long>(data_offset_), SEEK_SET) == 0) {
      write_file_u32_le(file_.get(), data_size);
    }
  }

  fflush(file_.get());
  file_.reset();
}

auto AudioDumper::finalize() -> void {
  std::lock_guard<std::mutex> lock(mutex_);
  finalize_unlocked();
}

auto AudioDumper::is_active() const -> bool {
  std::lock_guard<std::mutex> lock(mutex_);
  return file_ != nullptr;
}

auto audio_dumper_initialize(AudioDumper* dumper, const char* filename,
                             uint32_t sample_rate, uint32_t num_channels)
    -> int {
  if (dumper == nullptr) {
    return 1;
  }
  return dumper->initialize(filename, sample_rate, num_channels) ? 0 : 1;
}

auto audio_dumper_put_samples(AudioDumper* dumper, const int16_t* buf,
                              uint32_t num_samples) -> int {
  if (dumper == nullptr) {
    return 1;
  }
  return dumper->put_samples(buf, num_samples) ? 0 : 1;
}

auto audio_dumper_finalize(AudioDumper* dumper) -> int {
  if (dumper == nullptr) {
    return 1;
  }
  dumper->finalize();
  return 0;
}

auto audio_dumper_is_active(const AudioDumper* dumper) -> bool {
  if (dumper == nullptr) {
    return false;
  }
  return dumper->is_active();
}
