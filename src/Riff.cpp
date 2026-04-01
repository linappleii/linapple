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

/* Description: RIFF funcs  --- my GOD, what is RIFF?
 *
 * Author: Various
 */

/* Adaptation for SDL and POSIX (l) by beom beotiger, Nov-Dec 2007 */

#include <inttypes.h>
#include <stdint.h>
#include "wincompat.h"
#include "Riff.h"

static FILE* g_hRiffFile = NULL;
static unsigned int dwTotalOffset;
static unsigned int dwDataOffset;
static unsigned int g_dwTotalNumberOfBytesWritten = 0;
static unsigned int g_NumChannels = 2;

int RiffInitWriteFile(char *pszFile, unsigned int sample_rate, unsigned int NumChannels)
{
  g_hRiffFile = fopen(pszFile, "wb");

  if (g_hRiffFile == NULL) {
    return 1;
  }

  g_NumChannels = NumChannels;

  unsigned int temp32;
  unsigned short temp16;

  fwrite("RIFF", 1, 4, g_hRiffFile);

  temp32 = 0;        // total size
  dwTotalOffset = ftell(g_hRiffFile);
  fwrite(&temp32, 1, 4, g_hRiffFile);

  fwrite("WAVE", 1, 4, g_hRiffFile);

  fwrite("fmt ", 1, 4, g_hRiffFile);

  temp32 = 16;      // format length
  fwrite(&temp32, 1, 4, g_hRiffFile);

  temp16 = 1;        // PCM format
  fwrite(&temp16, 1, 2, g_hRiffFile);

  temp16 = NumChannels;    // channels
  fwrite(&temp16, 1, 2, g_hRiffFile);

  temp32 = sample_rate;  // sample rate
  fwrite(&temp32, 1, 4, g_hRiffFile);

  temp32 = sample_rate * 2 * NumChannels;  // bytes/second
  fwrite(&temp32, 1, 4, g_hRiffFile);

  temp16 = 2 * NumChannels;  // block align
  fwrite(&temp16, 1, 2, g_hRiffFile);

  temp16 = 16;      // bits/sample
  fwrite(&temp16, 1, 2, g_hRiffFile);

  fwrite("data", 1, 4, g_hRiffFile);

  temp32 = 0;        // data length
  dwDataOffset = ftell(g_hRiffFile);
  fwrite(&temp32, 1, 4, g_hRiffFile);

  g_dwTotalNumberOfBytesWritten = ftell(g_hRiffFile);

  return 0;
}

int RiffFinishWriteFile() {
  if (g_hRiffFile == NULL) {
    return 1;
  }

  unsigned int temp32;

  temp32 = g_dwTotalNumberOfBytesWritten - (dwTotalOffset + 4);
  fseek(g_hRiffFile, dwTotalOffset, SEEK_SET);
  fwrite(&temp32, 1, 4, g_hRiffFile);

  temp32 = g_dwTotalNumberOfBytesWritten - (dwDataOffset + 4);
  fseek(g_hRiffFile, dwDataOffset, SEEK_SET);
  fwrite(&temp32, 1, 4, g_hRiffFile);

  int res = fclose(g_hRiffFile);
  g_hRiffFile = NULL;
  return (res == 0) ? 0 : 1;
}

int RiffPutSamples(short *buf, unsigned int uSamples) {
  if (g_hRiffFile == NULL) {
    return 1;
  }

  size_t bytesToWrite = uSamples * sizeof(short) * g_NumChannels;
  size_t bytesWritten = fwrite(buf, 1, bytesToWrite, g_hRiffFile);
  g_dwTotalNumberOfBytesWritten += (unsigned int)bytesWritten;

  return 0;
}
