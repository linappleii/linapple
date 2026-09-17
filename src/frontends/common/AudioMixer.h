// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/Peripheral_Audio.h"

constexpr uint32_t SPKR_SAMPLE_RATE = PERIPHERAL_AUDIO_DEFAULT_SAMPLE_RATE;
constexpr uint32_t sample_rate = PERIPHERAL_AUDIO_DEFAULT_SAMPLE_RATE;

enum FadeType_t { fade_out = 0, fade_in = 1 };

auto audio_mixer_initialize() -> void;
auto audio_mixer_destroy() -> void;
auto audio_mixer_clear_buffers() -> void;

auto audio_mixer_register_source(int slot, const char* peripheral_id,
                                 const PeripheralAudioInfo_t* info) -> void;
auto audio_mixer_unregister_source(int slot) -> void;

auto audio_mixer_upload_channels(const char* peripheral_id, int slot,
                                 const int16_t* const* channels,
                                 size_t num_channels, uint32_t num_samples)
    -> void;

auto audio_mixer_set_channel_pan(int slot, size_t channel, float left,
                                 float right) -> void;
auto audio_mixer_get_channel_pan(int slot, size_t channel, float* left,
                                 float* right) -> void;
auto audio_mixer_reset_channel_pan(int slot) -> void;

using AudioChannelTapCallback_t = void (*)(const char* peripheral_id, int slot,
                                           const int16_t* const* channels,
                                           size_t num_channels,
                                           size_t num_samples);
auto audio_mixer_set_channel_tap_callback(AudioChannelTapCallback_t cb) -> void;

auto audio_mixer_get_samples(int16_t* out, size_t num_samples) -> void;

auto audio_mixer_set_fade(FadeType_t fade_type) -> void;
