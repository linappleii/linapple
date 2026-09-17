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
  PERIPHERAL_QUERY_AUDIO_INFO = 0x00000010,
  PERIPHERAL_AUDIO_DEFAULT_SAMPLE_RATE = 44100
};

typedef struct PeripheralAudioChannelInfo_t {
  char name[PERIPHERAL_AUDIO_NAME_MAX]; /**< Channel name, e.g. "Speaker",
                                           "Voice A" */
  float default_pan_left;  /**< Default gain to Left output (0.0 to 1.0) */
  float default_pan_right; /**< Default gain to Right output (0.0 to 1.0) */
} PeripheralAudioChannelInfo_t;

typedef struct PeripheralAudioInfo_t {
  uint32_t sample_rate;  /**< Native synthesis sample rate in Hz */
  uint32_t num_channels; /**< Number of planar audio channels */
  PeripheralAudioChannelInfo_t channels[PERIPHERAL_AUDIO_MAX_CHANNELS];
} PeripheralAudioInfo_t;

// NOLINTEND(modernize-use-using, cppcoreguidelines-use-enum-class)

#ifdef __cplusplus
}
#endif
