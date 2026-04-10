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

/* Description: Core sound mixing functionality
 *
 * Author: Tom Charlesworth, modified for decoupling.
 */

#include "Common.h"
#include <vector>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdint>

#include "SoundCore.h"
#include "Mockingboard.h"
#include "Speaker.h"
#include "Log.h"
#include "Common_Globals.h"

// Core buffers for mixing
struct sample_buffer {
  std::vector<int16_t> buffer;
  std::atomic<size_t> read_index;
  std::atomic<size_t> write_index;
  int16_t last_value;

  sample_buffer(size_t size) : buffer(size), read_index(0), write_index(0), last_value(0) {}

  void reinit() {
    std::fill(buffer.begin(), buffer.end(), 0);
    read_index = 0;
    write_index = 0;
    last_value = 0;
  }

  size_t get_filled() const {
    size_t r = read_index.load(std::memory_order_relaxed);
    size_t w = write_index.load(std::memory_order_relaxed);
    if (r <= w) return w - r;
    return buffer.size() + w - r;
  }

  size_t get_free() const {
    size_t filled = get_filled();
    if (filled >= buffer.size() - 1) return 0;
    return buffer.size() - 1 - filled;
  }

  void upload(const int16_t *src, size_t len) {
    size_t free = get_free();
    size_t num = (len < free) ? len : free;
    if (num == 0) return;

    size_t w = write_index.load(std::memory_order_relaxed);
    if (w + num < buffer.size()) {
      memcpy(&buffer[w], src, num * sizeof(int16_t));
      write_index.store(w + num, std::memory_order_release);
    } else {
      size_t len1 = buffer.size() - w;
      memcpy(&buffer[w], src, len1 * sizeof(int16_t));
      size_t len2 = num - len1;
      memcpy(&buffer[0], src + len1, len2 * sizeof(int16_t));
      write_index.store(len2, std::memory_order_release);
    }
  }

  void drain_to(int16_t *dest, size_t len, bool mix) {
    size_t available = get_filled();
    size_t num = (len < available) ? len : available;
    
    size_t r = read_index.load(std::memory_order_relaxed);
    auto process = [&](const int16_t* src, size_t count, size_t offset) {
      if (mix) {
        for (size_t i = 0; i < count; ++i) {
          int32_t val = (int32_t)dest[offset + i] + (int32_t)src[i];
          if (val > 32767) val = 32767;
          else if (val < -32768) val = -32768;
          dest[offset + i] = (int16_t)val;
        }
      } else {
        memcpy(dest + offset, src, count * sizeof(int16_t));
      }
    };

    if (num > 0) {
      if (r + num < buffer.size()) {
        process(&buffer[r], num, 0);
        r += num;
      } else {
        size_t len1 = buffer.size() - r;
        process(&buffer[r], len1, 0);
        size_t len2 = num - len1;
        process(&buffer[0], len2, len1);
        r = len2;
      }
      read_index.store(r, std::memory_order_release);
      last_value = (r > 0) ? buffer[r - 1] : buffer.back();
    }

    // Fill remaining with last_value
    if (num < len) {
        if (mix) {
            for (size_t i = num; i < len; ++i) {
                int32_t val = (int32_t)dest[i] + (int32_t)last_value;
                if (val > 32767) val = 32767;
                else if (val < -32768) val = -32768;
                dest[i] = (int16_t)val;
            }
        } else {
            for (size_t i = num; i < len; ++i) dest[i] = last_value;
        }
    }
  }
};

static sample_buffer *g_spkrMixBuffer = nullptr;
static sample_buffer *g_mockMixBuffer = nullptr;

void SoundCore_Initialize() {
  if (!g_spkrMixBuffer) g_spkrMixBuffer = new sample_buffer(16384);
  if (!g_mockMixBuffer) g_mockMixBuffer = new sample_buffer(16384);
  g_spkrMixBuffer->reinit();
  g_mockMixBuffer->reinit();
}

void SoundCore_Destroy() {
  delete g_spkrMixBuffer;
  delete g_mockMixBuffer;
  g_spkrMixBuffer = nullptr;
  g_mockMixBuffer = nullptr;
}

void DSUploadBuffer(short *buffer, unsigned len) {
  if (g_spkrMixBuffer) g_spkrMixBuffer->upload(buffer, len);
}

void DSUploadMockBuffer(short *buffer, unsigned len) {
  if (g_mockMixBuffer) g_mockMixBuffer->upload(buffer, len);
}

void SoundCore_GetSamples(int16_t *out, size_t num_samples) {
  if (!g_spkrMixBuffer || !g_mockMixBuffer) {
    memset(out, 0, num_samples * sizeof(int16_t));
    return;
  }
  
  // Speaker is primary (not mixed yet)
  g_spkrMixBuffer->drain_to(out, num_samples, false);
  // Mockingboard is mixed into Speaker
  g_mockMixBuffer->drain_to(out, num_samples, true);
}
