/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Core sound related functionality
 *
 * Author: Tom Charlesworth
 */

/*  Adaption for Linux+SDL done by beom beotiger. Peace! LLL */

#include <atomic>
#include "stdafx.h"
// for Assertion
#include <assert.h>
#include <iostream>
#include <functional>

bool g_bDSAvailable = false;
SDL_AudioStream *g_audioStream = NULL;

// forward decls    ------------------------
void SDLSoundDriverUninit();    // for DSInit()
bool SDLSoundDriverInit(unsigned wantedFreq, unsigned wantedSamples);  // for DSUninit?
static void SDLCALL sdl3AudioCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount);

bool DSInit() {
  if (g_bDSAvailable) {
    return true;  // do not need to repeat all process?? --bb
  }
  g_bDSAvailable = SDLSoundDriverInit(SPKR_SAMPLE_RATE, 1024);// I just do not know what number of samples use.
  return g_bDSAvailable;  //
}

void DSUninit() {
  if (!g_bDSAvailable) {
    return;
  }
  SDLSoundDriverUninit();  // using code from OpenMSX
}


void SoundCore_SetFade(int how) {
  if (how == FADE_OUT) {
    SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(g_audioStream));  //stop playing sound
  } else {
    SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(g_audioStream));  //start playing
  }
}

//  Code from OpenMSX  (http://openmsx.sourceforge.net)

void mute();

void unmute();

unsigned getFrequency();

double uploadBuffer(short *buffer, unsigned len);

void reInit();

unsigned getBufferFilled();

unsigned getBufferFree();

unsigned frequency;

struct sample_buffer {
  typedef int16_t sample_t;
  typedef std::function<sample_t(sample_t,sample_t)> mix_func_t;

  std::vector<sample_t> buffer;
  std::atomic<size_t> read_index;
  std::atomic<size_t> write_index;
  sample_t last_value;

  sample_buffer(size_t size) : buffer(size), read_index(0), write_index(0), last_value(0) {}

  void reinit() {
    std::fill(buffer.begin(), buffer.end(), 0);
    read_index = 0;
    write_index = 0;
  }

  size_t get_filled() const {
    size_t r = read_index.load(std::memory_order_relaxed);
    size_t w = write_index.load(std::memory_order_relaxed);
    size_t result;
    if (r <= w) {
      result = w - r;
    } else {
      result = buffer.size() + w - r;
    }
    if (result >= buffer.size()) result = buffer.size() - 1; // Prevent crash if slightly out of sync
    return result;
  }

  size_t get_free() const {
    auto result = buffer.size() - 2 - get_filled();
    if (result >= buffer.size()) result = 0;
    return result;
  }

  void upload(sample_t *src_buffer, size_t len);

  void drain_to(sample_t *stream, size_t len);
  void mix_into(sample_t *stream, size_t len,
                mix_func_t func = std::plus<sample_t>());
};

sample_buffer *mix_buffer;
sample_buffer *mock_buffer;

bool muted;


//  Main part
bool SDLSoundDriverInit(unsigned wantedFreq, unsigned wantedSamples) {
  SDL_AudioSpec desired;
  desired.freq = wantedFreq;
  desired.channels = 2; // stereo(2)  or mono(1)
  desired.format = SDL_AUDIO_S16;

  g_audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, sdl3AudioCallback, NULL);
  if (g_audioStream == NULL) {
    printf("Unable to open SDL audio: %s", SDL_GetError());
    return false;
  }
  frequency = wantedFreq;

  unsigned bufferSize;
  unsigned bufferIdxMask;
  bufferSize = 16 * wantedSamples;
  // GPH NOTE: bufferSize needs to be power of 2 for quick
  // modulus division (e.g. &)... Other, expensive division (div instruction) is required,
  // and because of the high volume of work against mixBuffer and mockBuffer, that is
  // undesirable.
  bufferIdxMask = bufferSize - 1;
  printf("bufferSize=%08x bufferIdxMask=%08x\n", bufferSize, bufferIdxMask);

  mix_buffer = new sample_buffer(bufferSize);  // buffer for Apple2 speakers
  mock_buffer = new sample_buffer(bufferSize);  // buffer for Mockingboard

  reInit();

  SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(g_audioStream));

  printf("Freq=%d,format=%d,channels=%d\n", desired.freq, desired.format, desired.channels);
  printf("samples=%d,bufferSize=%d\n", wantedSamples, bufferSize);

  return true;
}

void SDLSoundDriverUninit() {
  delete mix_buffer;
  delete mock_buffer;
  SDL_DestroyAudioStream(g_audioStream);
  g_audioStream = NULL;
}


void reInit() {
  mix_buffer->reinit();
  mock_buffer->reinit();
}

void mute() {
  if (!muted) {
    muted = true;
    SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(g_audioStream));
  }
}

void unmute() {
  if (muted) {
    muted = false;
    reInit();
    SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(g_audioStream));
  }
}

unsigned getFrequency() {
  return frequency;
}


// GPH this is called on IRQ to refresh the audio at regular intervals.
static void SDLCALL sdl3AudioCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
  (void)total_amount;
  (void)userdata;
  if (additional_amount <= 0) return;

  int16_t *temp_buf = (int16_t *)SDL_malloc(additional_amount);
  if (!temp_buf) return;

  int num_samples = additional_amount / sizeof(int16_t);

  mix_buffer->drain_to(temp_buf, num_samples);
#ifdef MOCKINGBOARD
  mock_buffer->mix_into(temp_buf, num_samples);
#endif

  SDL_PutAudioStreamData(stream, temp_buf, additional_amount);
  SDL_free(temp_buf);
}


void sample_buffer::drain_to(sample_t *stream, size_t len) {
  const auto available = get_filled();
  assert((len & 1) == 0); // stereo

  const auto num = std::min(len, available);
  if (num > 0) {
    mix_into(stream, num, [](const sample_t& ignored, const sample_t& new_value) {
  (void)ignored;
      return new_value;
    });
  }

  // Fill the remainer of the buffer with last value to prevent potential
  // clicks and pops.
  if (available > 0) { // update last_value
    last_value = read_index > 0 ? buffer[read_index-1] : buffer.back();
  }
  std::fill_n(stream+num, len-num, last_value);
}


void sample_buffer::mix_into(sample_t *stream, size_t len,
                             mix_func_t mix_func) {
  // And add Mockingboard sound data to the stream
  // GPH: We are going to add the Mockingboard and speaker samples.
  // Their independent maximum amplitudes have been selected to eliminate
  // any possibility of wave peak clipping.  This speeds up the timing-sensitive
  // operation here (since we're in an IRQ handler) and eliminates the
  // need for a potentially expensive divide.
  const auto available = get_filled();
  const auto num = std::min(len, available);
  if (num < 1) {
    return;
  }

  if ((read_index + num) < buffer.size()) { // No wrap around: straight copy
    std::transform(stream, stream+num, buffer.begin()+read_index,
                   stream, mix_func);
    read_index += num;
  } else { // handle wrap around
    const auto len1 = buffer.size() - read_index;
    if (len1) {
      std::transform(stream, stream+len1, buffer.begin()+read_index,
                     stream, mix_func);
    }

    const auto len2 = num - len1;
    read_index = len2;
    if (len2) {
      std::transform(stream+len1, stream+len1+len2, buffer.begin(),
                     stream+len1, mix_func);
    }
  }
}


void DSUploadBuffer(short *buffer, unsigned len) {
  mix_buffer->upload(buffer, len);
}

void sample_buffer::upload(sample_t *src_buffer, size_t len) {
  const auto num = std::min(len, get_free()); // ignore overrun (drop samples)
  if ((write_index + num) < buffer.size()) {
    std::copy_n(src_buffer, num, buffer.begin()+write_index);
    write_index += num;
  } else {
    const auto len1 = buffer.size() - write_index;
    std::copy_n(src_buffer, len1, buffer.begin()+write_index);
    const auto len2 = num - len1;
    std::copy_n(src_buffer+len1, len2, buffer.begin());
    write_index = len2;
  }
}

// Uploading sound data for Mockingboard buffer
// GPH 01042015: buffer contains interleaved stereo data: left sample, right sample, left sample, etc...
void DSUploadMockBuffer(short *buffer, unsigned len) {
  mock_buffer->upload(buffer, len);
}
