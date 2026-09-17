// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/Peripheral_Audio.h"

enum FadeType_t { fade_out = 0, fade_in = 1 };

// Only the frontend knows the device's rate, because it opened the device.
// Every source is resampled to it, so no rate constant lives in the mixer or
// in the peripheral ABI.
auto audio_mixer_initialize(uint32_t output_rate_hz) -> void;
auto audio_mixer_destroy() -> void;
auto audio_mixer_clear_buffers() -> void;

auto audio_mixer_register_source(int slot, const char* peripheral_id,
                                 const PeripheralAudioInfo_t* info) -> void;
auto audio_mixer_unregister_source(int slot) -> void;

auto audio_mixer_upload_channels(const char* peripheral_id, int slot,
                                 const float* const* channels,
                                 size_t num_channels, uint32_t num_samples)
    -> void;

auto audio_mixer_set_channel_pan(int slot, size_t channel, float left,
                                 float right) -> void;
auto audio_mixer_get_channel_pan(int slot, size_t channel, float* left,
                                 float* right) -> void;
auto audio_mixer_reset_channel_pan(int slot) -> void;

// Defaults to 1 / peak_magnitude from the source's audio info, which is what
// gives summing headroom an owner and a frontend volume control somewhere to
// attach.
auto audio_mixer_set_source_gain(int slot, float gain) -> void;

using AudioChannelTapCallback_t = void (*)(const char* peripheral_id, int slot,
                                           const float* const* channels,
                                           size_t num_channels,
                                           size_t num_samples);
auto audio_mixer_set_channel_tap_callback(AudioChannelTapCallback_t cb) -> void;

auto audio_mixer_get_samples(int16_t* out, size_t num_samples) -> void;

auto audio_mixer_set_fade(FadeType_t fade_type) -> void;
