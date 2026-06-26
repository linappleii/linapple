// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Manages audio mixing and sample buffering for the system.
 */

enum { FADE_OUT = 0, FADE_IN = 1 };

void SoundCore_Initialize();
void SoundCore_Destroy();
void SoundCore_ClearBuffers();

/**
 * @brief Uploads samples for the built-in Speaker channel.
 */
void SoundCore_UploadSpeakerSamples(const int16_t* buffer,
                                    uint32_t num_samples);

/**
 * @brief Uploads samples for the expansion Mockingboard channel.
 */
void SoundCore_UploadMockingboardSamples(const int16_t* buffer,
                                         uint32_t num_samples);

/**
 * @brief Retrieves mixed samples for the frontend to play.
 */
void SoundCore_GetSamples(int16_t* out, size_t num_samples);

/**
 * @brief Sets the audio fading state.
 */
void SoundCore_SetFade(int fade_type);
