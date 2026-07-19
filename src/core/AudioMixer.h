// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

constexpr uint32_t SPKR_SAMPLE_RATE = 44100;
constexpr uint32_t sample_rate = 44100;

enum FadeType_t { fade_out = 0, fade_in = 1 };

auto audio_mixer_initialize() -> void;
auto audio_mixer_destroy() -> void;
auto audio_mixer_clear_buffers() -> void;

auto audio_mixer_upload_speaker_samples(const int16_t* buffer,
                                        uint32_t num_samples) -> void;
auto audio_mixer_upload_mockingboard_samples(const int16_t* buffer,
                                             uint32_t num_samples) -> void;

auto audio_mixer_get_samples(int16_t* out, size_t num_samples) -> void;

auto audio_mixer_set_fade(FadeType_t fade_type) -> void;
