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

// Enumerators start at 1 so that a zeroed PeripheralAudioInfo_t is invalid
// rather than silently meaning one of the two forms.
typedef enum {
  peripheral_audio_cpu_clocked = 1, /**< One sample per cycle_divisor 6502
                                       cycles; sample_rate is unused */
  peripheral_audio_absolute = 2     /**< sample_rate is the rate in Hz; the card
                                       carries its own oscillator */
} PeripheralAudioTimeBase_t;

typedef struct PeripheralAudioChannelInfo_t {
  char name[PERIPHERAL_AUDIO_NAME_MAX]; /**< Channel name, e.g. "Speaker",
                                           "Voice A" */
  float default_pan_left;  /**< Default gain to Left output (0.0 to 1.0) */
  float default_pan_right; /**< Default gain to Right output (0.0 to 1.0) */
} PeripheralAudioChannelInfo_t;

/**
 * @brief How a peripheral describes the audio it emits.
 *
 * A source declares its time base in one of two forms because both exist in
 * real Apple II sound hardware: the speaker and a Mockingboard's AY-3-8910s
 * run from the slot's phase-0 line, while speech synthesizers and similar
 * cards carry their own oscillator. Consumers resolve the CPU-clocked form to
 * Hz by dividing the current 6502 clock by cycle_divisor.
 */
typedef struct PeripheralAudioInfo_t {
  PeripheralAudioTimeBase_t time_base; /**< Which of the two forms below is
                                          meaningful */
  uint32_t cycle_divisor; /**< CPU-clocked form: 6502 cycles per sample */
  uint32_t sample_rate;   /**< Absolute form: native synthesis rate in Hz */
  uint32_t num_channels;  /**< Number of planar audio channels */
  float peak_magnitude;   /**< Largest absolute channel value this source will
                             ever emit; a fact about the signal, not a
                             recommendation */
  PeripheralAudioChannelInfo_t channels[PERIPHERAL_AUDIO_MAX_CHANNELS];
} PeripheralAudioInfo_t;

// NOLINTEND(modernize-use-using, cppcoreguidelines-use-enum-class)

#ifdef __cplusplus
}
#endif
