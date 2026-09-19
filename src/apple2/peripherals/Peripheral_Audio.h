// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-use-using, cppcoreguidelines-use-enum-class)

enum {
  PERIPHERAL_AUDIO_NAME_MAX = 16,
  PERIPHERAL_AUDIO_MAX_CHANNELS = 16,
  PERIPHERAL_QUERY_AUDIO_INFO = 0x00000010
};

// Enumerators start at 1 so that a zeroed PeripheralAudioInfo_t is invalid
// rather than silently meaning one of the two forms.
typedef enum {
  peripheral_audio_cpu_clocked = 1, /**< One sample per cycle_divisor 6502
                                       cycles; sample_rate is unused */
  peripheral_audio_absolute = 2     /**< sample_rate is the rate in Hz; the card
                                       carries its own oscillator */
} PeripheralAudioTimeBase_t;

typedef struct PeripheralAudioChannelInfo_t {
  char name[PERIPHERAL_AUDIO_NAME_MAX];
  float default_pan_left;  /**< Default gain to Left output (0.0 to 1.0) */
  float default_pan_right; /**< Default gain to Right output (0.0 to 1.0) */
} PeripheralAudioChannelInfo_t;

// Describes peripheral audio characteristics, channel geometry, and sample
// timing.
typedef struct PeripheralAudioInfo_t {
  PeripheralAudioTimeBase_t time_base;
  uint32_t
      cycle_divisor;     /**< 6502 clock cycles per sample (CPU-clocked mode) */
  uint32_t sample_rate;  /**< Sample rate in Hz (absolute oscillator mode) */
  uint32_t num_channels; /**< Number of active planar audio channels */
  float peak_magnitude;  /**< Maximum expected absolute sample value */
  PeripheralAudioChannelInfo_t channels[PERIPHERAL_AUDIO_MAX_CHANNELS];
} PeripheralAudioInfo_t;

// NOLINTEND(modernize-use-using, cppcoreguidelines-use-enum-class)

#ifdef __cplusplus
}
#endif
