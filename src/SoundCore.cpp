/*
AppleWin : An Apple //e emulator for Windows

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

#include "stdafx.h"
// for Assertion
#include <assert.h>
#include <iostream>
#include <functional>

bool g_bDSAvailable = false;

// forward decls    ------------------------
void SDLSoundDriverUninit();    // for DSInit()
bool SDLSoundDriverInit(unsigned wantedFreq, unsigned wantedSamples);  // for DSUninit?

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
    SDL_PauseAudio(1);  //stop playing sound
  } else {
    SDL_PauseAudio(0);  //start playing
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

static void audioCallback(void *userdata, BYTE *strm, int len);

unsigned frequency;

struct sample_buffer {
  typedef int16_t sample_t;
  typedef std::function<sample_t(sample_t,sample_t)> mix_func_t;

  std::vector<sample_t> buffer;
  size_t read_index, write_index;
  sample_t last_value;

  sample_buffer(size_t size) : buffer(size), last_value(0) {}

  void reinit() {
    std::fill(buffer.begin(), buffer.end(), 0);
    read_index = write_index = 0;
  }

  size_t get_filled() const {
    size_t result;
    if (read_index <= write_index) {
      result = write_index - read_index;
    } else {
      result = buffer.size() + write_index - read_index;
    }
    assert((0 <= result) && (result < buffer.size()));
    return result;
  }

  size_t get_free() const {
    // we can't distinguish completely filled from completely empty
    // (in both cases readIx would be equal to writeIdx), so instead
    // we define full as '(writeIdx + 2) == readIdx' (note that index
    // increases in steps of 2 (stereo)).
    auto result = buffer.size() - 2 - get_filled();
    assert((0 <= result) && (result < buffer.size()));
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
  desired.samples = wantedSamples;

  desired.channels = 2; // stereo(2)  or mono(1)
  // be courteous with BIG_Endian systems, please! --bb
  #if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
  desired.format = AUDIO_S16MSB;
  #else
  desired.format = AUDIO_S16LSB;
  #endif

  desired.callback = audioCallback; // must be a static method
  desired.userdata = NULL;
  SDL_AudioSpec audioSpec;
  if (SDL_OpenAudio(&desired, &audioSpec) != 0) {
    printf("Unable to open SDL audio: %s", SDL_GetError());
    return false;
  }
  frequency = audioSpec.freq;

  unsigned bufferSize;
  unsigned bufferIdxMask;
  bufferSize = 8 * (audioSpec.size / sizeof(short));
  // GPH NOTE: bufferSize needs to be power of 2 for quick
  // modulus division (e.g. &)... Other, expensive division (div instruction) is required,
  // and because of the high volume of work against mixBuffer and mockBuffer, that is
  // undesirable.
  bufferIdxMask = bufferSize - 1;
  printf("bufferSize=%08x bufferIdxMask=%08x\n", bufferSize, bufferIdxMask);

  mix_buffer = new sample_buffer(bufferSize);  // buffer for Apple2 speakers
  mock_buffer = new sample_buffer(bufferSize);  // buffer for Mockingboard

  reInit();
  printf("SDL_MIX_MAXVOLUME=%d\n", SDL_MIX_MAXVOLUME);
  printf("Freq=%d,format=%d,channels=%d,silence=%d\n", audioSpec.freq, audioSpec.format, audioSpec.channels,
         audioSpec.silence);
  printf("samples=%d,size=%d,bufferSize=%d\n", audioSpec.samples, audioSpec.size, bufferSize);

  return true;
}

void SDLSoundDriverUninit() {
  delete mix_buffer;
  delete mock_buffer;
  SDL_CloseAudio();
}


void reInit() {
  mix_buffer->reinit();
  mock_buffer->reinit();
}

void mute() {
  if (!muted) {
    muted = true;
    SDL_PauseAudio(1);
  }
}

void unmute() {
  if (muted) {
    muted = false;
    reInit();
    SDL_PauseAudio(0);
  }
}

unsigned getFrequency() {
  return frequency;
}


// GPH this is called on IRQ to refresh the audio at regular intervals.
void audioCallback(void *userdata, BYTE *strm, int len) {
  assert((len & 3) == 0); // stereo, 16-bit
  SDL_LockAudio();
  {
    // We'll mix the buffers here keeping in mind the need for speed.
    auto stream = reinterpret_cast<sample_buffer::sample_t*>(strm);
    const auto str_len = len / sizeof(sample_buffer::sample_t);
    mix_buffer->drain_to(stream, str_len);
#ifdef MOCKINGBOARD
    mock_buffer->mix_into(stream, str_len);
#endif
  }
  SDL_UnlockAudio();
}


void sample_buffer::drain_to(sample_t *stream, size_t len) {
  const auto available = get_filled();
  assert((len & 1) == 0); // stereo

  const auto num = std::min(len, available);
  if (num > 0) {
    mix_into(stream, num, [](const sample_t& ignored, const sample_t& new_value) {
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
