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

/* Description: Super Serial Card emulation
 *
 * Author: Various
 */

#include "core/Common.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "apple2/SerialComms.h"
#include "SerialCommsFrontend.h"
#include "apple2/CPU.h"
#include "core/Log.h"
#include "apple2/Memory.h"

char SSC_rom[] = "\x20\x9B\xC9\xA9\x16\x48\xA9\x00\x9D\xB8\x04\x9D\xB8\x03\x9D\x38"
                 "\x04\x9D\xB8\x05\x9D\x38\x06\x9D\xB8\x06\xB9\x82\xC0\x85\x2B\x4A"
                 "\x4A\x90\x04\x68\x29\xFE\x48\xB8\xB9\x81\xC0\x4A\xB0\x07\x4A\xB0"
                 "\x0E\xA9\x01\xD0\x3D\x4A\xA9\x03\xB0\x02\xA9\x80\x9D\xB8\x04\x2C"
                 "\x58\xFF\xA5\x2B\x29\x20\x49\x20\x9D\xB8\x03\x70\x0A\x20\x9B\xC8"
                 "\xAE\xF8\x07\x9D\xB8\x05\x60\xA5\x2B\x4A\x4A\x29\x03\xA8\xF0\x04"
                 "\x68\x29\x7F\x48\xB9\xA6\xC9\x9D\x38\x06\xA4\x26\x68\x29\x95\x48"
                 "\xA9\x09\x9D\x38\x05\x68\x9D\x38\x07\xA5\x2B\x48\x29\xA0\x50\x02"
                 "\x29\x80\x20\xA1\xCD\x20\x81\xCD\x68\x29\x0C\x50\x02\xA9\x00\x0A"
                 "\x0A\x0A\x09\x0B\x99\x8A\xC0\xB9\x88\xC0\x60\x20\x9B\xC9\x20\xAA"
                 "\xC8\x29\x7F\xAC\xF8\x07\xBE\xB8\x05\x60\x20\xFF\xCA\xB0\x05\x20"
                 "\x2C\xCC\x90\xF6\x60\x20\x1E\xCA\x68\xA8\x68\xAA\xA5\x27\x60\xF0"
                 "\x29\xBD\xB8\x06\x10\x05\x5E\xB8\x06\xD0\x24\x20\x3E\xCC\x90\x1A"
                 "\xBD\xB8\x03\x29\xC0\xF0\x0E\xA5\x27\xC9\xE0\x90\x08\xBD\xB8\x04"
                 "\x09\x40\x9D\xB8\x04\x28\xF0\xD0\xD0\xCB\x20\xFF\xCA\x90\xDC\x20"
                 "\x11\xCC\x28\x08\xF0\xDA\x20\xD1\xC9\x4C\xD0\xC8\x20\x1A\xCB\xB0"
                 "\xB7\xA5\x27\x48\xBD\x38\x07\x29\xC0\xD0\x16\xA5\x24\xF0\x42\xC9"
                 "\x08\xF0\x04\xC9\x10\xD0\x0A\x09\xF0\x3D\xB8\x06\x18\x65\x24\x85"
                 "\x24\xBD\xB8\x06\xC5\x24\xF0\x29\xA9\xA0\x90\x08\xBD\x38\x07\x0A"
                 "\x10\x1F\xA9\x88\x85\x27\x2C\x58\xFF\x08\x70\x0C\xEA\x2C\x58\xFF"
                 "\x50\xB8\xAE\xF8\x07\x4C\xEF\xC9\x20\xB5\xC9\x20\x6B\xCB\x4C\x68"
                 "\xC9\x68\xB8\x08\x85\x27\x48\x20\x68\xCB\x20\xB5\xC9\x68\x49\x8D"
                 "\x0A\xD0\x05\x9D\xB8\x06\x85\x24\xBD\xB8\x04\x10\x0D\xBD\x38\x06"
                 "\xF0\x08\x18\xFD\xB8\x06\xA9\x8D\x90\xDA\x28\x70\xA4\xBD\x38\x07"
                 "\x30\x16\xBC\xB8\x06\x0A\x30\x0E\x98\xA0\x00\x38\xFD\x38\x06\xC9"
                 "\xF8\x90\x03\x69\x27\xA8\x84\x24\x4C\xB8\xC8\x8E\xF8\x07\x84\x26"
                 "\xA9\x00\x9D\xB8\x05\x60\x29\x48\x50\x84\x85\x27\x20\x9B\xC9\x20"
                 "\x63\xCB\x4C\xA3\xC8\xA5\x27\x49\x08\x0A\xF0\x04\x49\xEE\xD0\x09"
                 "\xDE\xB8\x06\x10\x03\x9D\xB8\x06\x60\xC9\xC0\xB0\xFB\xFE\xB8\x06"
                 "\x60\xBD\x38\x07\x29\x08\xF0\x16\xBD\xB8\x04\xA4\x27\xC0\x94\xD0"
                 "\x04\x09\x80\xD0\x06\xC0\x92\xD0\x05\x29\x7F\x9D\xB8\x04\x60\x8A"
                 "\x0A\x0A\x0A\x0A\x85\x26\xA9\x00\x9D\xB8\x05\x70\x0F\xA0\x00\xB1"
                 "\x3C\x85\x27\x20\x02\xCC\x20\xBA\xFC\x90\xF2\x60\x20\xD2\xCA\x90"
                 "\xFB\xB9\x88\xC0\xA0\x00\x91\x3C\x20\xBA\xFC\x90\xEF\x60\xBD\xB8"
                 "\x04\x10\x31\xA9\x02\x48\xA9\x7F\x20\xE2\xCD\xA4\x24\xB1\x28\x85"
                 "\x27\xA9\x07\x25\x4F\xD0\x10\xA4\x24\xA9\xDF\xD1\x28\xD0\x02\xA5"
                 "\x27\x91\x28\xE6\x4F\xE6\x4F\xBD\xB8\x04\x30\x09\x20\x11\xCC\x68"
                 "\xA9\x8D\x85\x27\x60\x20\xFF\xCA\x90\x0C\x20\x11\xCC\x20\xD1\xC9"
                 "\x20\xA3\xCC\x4C\x2B\xCA\x20\x3E\xCC\x90\xC6\x70\xBE\xBD\x38\x07"
                 "\x0A\x10\x22\x68\xA8\xA5\x27\xC0\x01\xF0\x20\xB0\x34\xC9\x9B\xD0"
                 "\x06\xC8\x98\x48\x4C\x2B\xCA\xC9\xC1\x90\x08\xC9\xDB\xB0\x04\x09"
                 "\x20\x85\x27\x98\x48\x20\x68\xCB\x4C\x2B\xCA\xC9\x9B\xF0\xE2\xC9"
                 "\xB0\x90\x0A\xC9\xBB\xB0\x06\xA8\xB9\x09\xCA\x85\x27\xA0\x00\xF0"
                 "\xE2\xC9\x9B\xD0\xDE\xA0\x00\xF0\xC9\x9B\x9C\x9F\xDB\xDC\xDF\xFB"
                 "\xFC\xFD\xFE\xFF\xA2\xCA\xCA\xD0\xFD\x38\xE9\x01\xD0\xF6\xAE\xF8"
                 "\x07\x60\xA4\x26\xB9\x89\xC0\x48\x29\x20\x4A\x4A\x85\x35\x68\x29"
                 "\x0F\xC9\x08\x90\x04\x29\x07\xB0\x02\xA5\x35\x05\x35\xF0\x05\x09"
                 "\x20\x9D\xB8\x05\x60\xA4\x26\xB9\x89\xC0\x29\x70\xC9\x10\x60\x20"
                 "\xD2\xCA\x90\x15\xB9\x88\xC0\x09\x80\xC9\x8A\xD0\x09\xA8\xBD\x38"
                 "\x07\x29\x20\xD0\x03\x98\x38\x60\x18\x60\xA4\x26\xB9\x81\xC0\x4A"
                 "\xB0\x36\xBD\xB8\x04\x29\x07\xF0\x05\x20\xFC\xCD\x38\x60\xA5\x27"
                 "\x29\x7F\xDD\x38\x05\xD0\x05\xFE\xB8\x04\x38\x60\xBD\x38\x07\x29"
                 "\x08\xF0\x15\x20\xFF\xCA\x90\x10\xC9\x93\xF0\x0E\x48\xBD\x38\x07"
                 "\x4A\x4A\x68\x90\x04\x9D\xB8\x06\x18\x60\x20\xAA\xC8\xC9\x91\xD0"
                 "\xF9\x18\x60\x20\x1A\xCB\xB0\xF1\x20\x9E\xCC\xA4\x26\xB9\x81\xC0"
                 "\x4A\x90\x4E\x4A\x90\x4B\xA5\x27\x48\xBD\x38\x04\xC9\x67\x90\x10"
                 "\xC9\x6C\xB0\x22\xC9\x6B\x68\x48\x49\x9B\x29\x7F\xD0\x18\xB0\x19"
                 "\xBD\xB8\x04\x29\x1F\x09\x80\x85\x27\x20\x02\xCC\x20\xAA\xC8\x49"
                 "\x86\xD0\xED\x9D\x38\x04\xDE\x38\x04\x68\x85\x27\x49\x8D\x0A\xD0"
                 "\x0A\xBD\xB8\x03\x29\x30\xF0\x03\x9D\x38\x04\x20\x02\xCC\x4C\xEA"
                 "\xCB\x20\x02\xCC\x0A\xA8\xBD\xB8\x03\xC0\x18\xF0\x0C\x4A\x4A\xC0"
                 "\x14\xF0\x06\x4A\x4A\xC0\x1A\xD0\x25\x29\x03\xF0\x0D\xA8\xB9\xFE"
                 "\xCB\xA8\xA9\x20\x20\xC4\xCA\x88\xD0\xF8\xA5\x27\x0A\xC9\x1A\xD0"
                 "\x0D\xBD\x38\x07\x6A\x90\x07\xA9\x8A\x85\x27\x4C\x6B\xCB\x60\x01"
                 "\x08\x40\x20\xF5\xCA\xD0\xFB\x98\x09\x89\xA8\xA5\x27\x99\xFF\xBF"
                 "\x60\x48\xA4\x24\xA5\x27\x91\x28\x68\xC9\x95\xD0\x0C\xA5\x27\xC9"
                 "\x20\xB0\x06\x20\xDF\xCC\x59\xDB\xCC\x85\x27\x60\x18\xBD\x38\x07"
                 "\x29\x04\xF0\x09\xAD\x00\xC0\x10\x04\x8D\x10\xC0\x38\x60\xE6\x4E"
                 "\xD0\x02\xE6\x4F\x20\x2C\xCC\xB8\x90\xF3\x20\x11\xCC\x29\x7F\xDD"
                 "\x38\x05\xD0\x3D\xA4\x26\xB9\x81\xC0\x4A\xB0\x35\xA0\x0A\xB9\x93"
                 "\xCC\x85\x27\x98\x48\x20\xA3\xCC\x68\xA8\x88\x10\xF1\xA9\x01\x20"
                 "\x7B\xCE\x20\x34\xCC\x10\xFB\xC9\x88\xF0\xE1\x85\x27\x20\xA3\xCC"
                 "\x20\x1A\xCB\xBD\xB8\x04\x29\x07\xD0\xE8\xA9\x8D\x85\x27\x2C\x58"
                 "\xFF\x38\x60\xBA\xC3\xD3\xD3\xA0\xC5\xCC\xD0\xD0\xC1\x8D\xBD\x38"
                 "\x07\x10\x13\xBD\x38\x07\x29\x02\xF0\x0D\xBD\xB8\x04\x29\x38\xF0"
                 "\x06\x8A\x48\xA9\xAF\x48\x60\x20\xDF\xCC\x09\x80\xC9\xE0\x90\x06"
                 "\x59\xD3\xCC\x4C\xF6\xFD\xC9\xC1\x90\xF9\xC9\xDB\xB0\xF5\x59\xD7"
                 "\xCC\x90\xF0\x20\x00\xE0\x20\x00\x00\x00\xC0\x00\x00\xE0\xC0\xBD"
                 "\xB8\x03\x2A\x2A\x2A\x29\x03\xA8\xA5\x27\x60\x42\x67\xC0\x54\x47"
                 "\xA6\x43\x87\xA6\x51\x47\xB8\x52\xC7\xAC\x5A\xE7\xF3\x49\x90\xD3"
                 "\x4B\x90\xDF\x45\x43\x80\x46\xE3\x04\x4C\xE3\x01\x58\xE3\x08\x54"
                 "\x83\x40\x53\x43\x40\x4D\xE3\x20\x00\x42\xF6\x7C\x50\xF6\x9A\x44"
                 "\xF6\x9B\x46\xF6\x46\x4C\xF6\x40\x43\xF6\x3A\x54\xD6\x34\x4E\x90"
                 "\xE8\x53\x56\x60\x00\xA9\x3F\xA0\x07\xD0\x10\xA9\xCF\xA0\x05\xD0"
                 "\x0A\xA9\xF3\xA0\x03\xD0\x04\xA9\xFC\xA0\x01\x3D\xB8\x03\x85\x2A"
                 "\xBD\x38\x04\x29\x03\x18\x6A\x2A\x88\xD0\xFC\x05\x2A\x9D\xB8\x03"
                 "\x60\x29\x07\x0A\x0A\x0A\x85\x2A\x0A\xC5\x26\xF0\x0F\xBD\xB8\x04"
                 "\x29\xC7\x05\x2A\x9D\xB8\x04\xA9\x00\x9D\x38\x06\x60\x29\x0F\xD0"
                 "\x07\xB9\x81\xC0\x4A\x4A\x4A\x4A\x09\x10\x85\x2A\xA9\xE0\x85\x2B"
                 "\xB9\x8B\xC0\x25\x2B\x05\x2A\x99\x8B\xC0\x60\x88\x0A\x0A\x0A\x0A"
                 "\x0A\x85\x2A\xA9\x1F\xD0\xE7\x1E\xB8\x04\x38\xB0\x10\x99\x89\xC0"
                 "\x20\x93\xFE\x20\x89\xFE\xAE\xF8\x07\x1E\xB8\x04\x18\x7E\xB8\x04"
                 "\x60\xB9\x8A\xC0\x48\x09\x0C\x99\x8A\xC0\xA9\xE9\x20\xC4\xCA\x68"
                 "\x99\x8A\xC0\x60\xA9\x28\x9D\x38\x06\xA9\x80\x1D\x38\x07\xD0\x05"
                 "\xA9\xFE\x3D\x38\x07\x9D\x38\x07\x60\xC9\x28\x90\x0E\x9D\x38\x06"
                 "\xA9\x3F\xD0\xEE\x1E\x38\x05\x38\x7E\x38\x05\x60\xA8\xA5\x27\x29"
                 "\x7F\xC9\x20\xD0\x09\xC0\x03\xF0\x01\x60\xA9\x04\xD0\x6D\xC9\x0D"
                 "\xD0\x12\x20\x79\xCE\xC0\x07\xF0\x01\x60\xA9\xCD\x48\xBD\x38\x04"
                 "\x48\xA4\x26\x60\x85\x35\xA9\xCE\x48\xB9\x30\xCE\x48\xA5\x35\x60"
                 "\xA7\x37\x61\x89\x8A\xA7\x89\x89\xDD\x38\x05\xD0\x06\xDE\xB8\x04"
                 "\x4C\x02\xCC\xC9\x30\x90\x0D\xC9\x3A\xB0\x09\x29\x0F\x9D\x38\x04"
                 "\xA9\x02\xD0\x27\xC9\x20\xB0\x06\x9D\x38\x05\x4C\x79\xCE\xA0\x00"
                 "\xF0\x4D\x49\x30\xC9\x0A\xB0\x0D\xA0\x0A\x7D\x38\x04\x88\xD0\xFA"
                 "\x9D\x38\x04\xF0\x15\xA0\x2E\xD0\x36\xA9\x00\x85\x2A\xAE\xF8\x07"
                 "\xBD\xB8\x04\x29\xF8\x05\x2A\x9D\xB8\x04\x60\xA8\xBD\x38\x04\xC0"
                 "\x44\xF0\x09\xC0\x45\xD0\x11\x1D\x38\x07\xD0\x05\x49\xFF\x3D\x38"
                 "\x07\x9D\x38\x07\xA9\x06\xD0\xD3\xA9\x20\x9D\xB8\x05\xD0\xF5\xB9"
                 "\xEB\xCC\xF0\xF4\xC5\x35\xF0\x05\xC8\xC8\xC8\xD0\xF2\xC8\xB9\xEB"
                 "\xCC\x85\x2A\x29\x20\xD0\x07\xBD\x38\x07\x29\x10\xD0\xEB\xBD\x38"
                 "\x07\x4A\x4A\x24\x2A\xB0\x04\x10\xE0\x30\x02\x50\xDC\xA5\x2A\x48"
                 "\x29\x07\x20\x7B\xCE\xC8\x68\x29\x10\xD0\x07\xB9\xEB\xCC\x9D\x38"
                 "\x04\x60\xA9\xCD\x48\xB9\xEB\xCC\x48\xA4\x26\xBD\x38\x04\x60\xC2"
                 "\x2C\x58\xFF\x70\x0C\x38\x90\x18\xB8\x50\x06\x01\x31\x8E\x94\x97"
                 "\x9A\x85\x27\x86\x35\x8A\x48\x98\x48\x08\x78\x8D\xFF\xCF\x20\x58"
                 "\xFF\xBA\xBD\x00\x01\x8D\xF8\x07\xAA\x0A\x0A\x0A\x0A\x85\x26\xA8"
                 "\x28\x50\x29\x1E\x38\x05\x5E\x38\x05\xB9\x8A\xC0\x29\x1F\xD0\x05"
                 "\xA9\xEF\x20\x05\xC8\xE4\x37\xD0\x0B\xA9\x07\xC5\x36\xF0\x05\x85"
                 "\x36\x18\x90\x08\xE4\x39\xD0\xF9\xA9\x05\x85\x38\xBD\x38\x07\x29"
                 "\x02\x08\x90\x03\x4C\xBF\xC8\xBD\xB8\x04\x48\x0A\x10\x0E\xA6\x35"
                 "\xA5\x27\x09\x20\x9D\x00\x02\x85\x27\xAE\xF8\x07\x68\x29\xBF\x9D"
                 "\xB8\x04\x28\xF0\x06\x20\x63\xCB\x4C\xB5\xC8\x4C\xFC\xC8\x20\x00"
                 "\xC8\xA2\x00\x60\x4C\x9B\xC8\x4C\xAA\xC9\x4A\x20\x9B\xC9\xB0\x08"
                 "\x20\xF5\xCA\xF0\x06\x18\x90\x03\x20\xD2\xCA\xBD\xB8\x05\xAA\x60"
                 "\xA2\x03\xB5\x36\x48\xCA\x10\xFA\xAE\xF8\x07\xBD\x38\x06\x85\x36"
                 "\xBD\xB8\x04\x29\x38\x4A\x4A\x4A\x09\xC0\x85\x37\x8A\x48\xA5\x27"
                 "\x48\x09\x80\x20\xED\xFD\x68\x85\x27\x68\x8D\xF8\x07\xAA\x0A\x0A"
                 "\x0A\x0A\x85\x26\x8D\xFF\xCF\xA5\x36\x9D\x38\x06\xA2\x00\x68\x95"
                 "\x36\xE8\xE0\x04\x90\xF8\xAE\xF8\x07\x60\xC1\xD0\xD0\xCC\xC5\x08";

// Default: 19200-8-N-1
SSC_DIPSW g_DIPSWDefault = {
  // DIPSW1:
  SSC_B19200, // Baud rate constant
  FIRMWARE_CIC,

  // DIPSW2:
  SSC_STOP_BITS_1, 8,        // ByteSize
  SSC_PARITY_NONE, true,      // LF
  false,
};

static void GetDIPSW(SuperSerialCard* pSSC);
static void SetDIPSWDefaults(SuperSerialCard* pSSC);
static unsigned char GenerateControl(SuperSerialCard* pSSC);
static unsigned int BaudRateToIndex(unsigned int uBaudRate);
static void UpdateCommState(SuperSerialCard* pSSC);

static unsigned char CommCommand(SuperSerialCard* pSSC, unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);
static unsigned char CommControl(SuperSerialCard* pSSC, unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);
static unsigned char CommDipSw(SuperSerialCard* pSSC, unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);
static unsigned char CommReceive(SuperSerialCard* pSSC, unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);
static unsigned char CommStatus(SuperSerialCard* pSSC, unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);
static unsigned char CommTransmit(SuperSerialCard* pSSC, unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);

void SSC_Reset(SuperSerialCard* pSSC) {
  GetDIPSW(pSSC);
  pSSC->m_vRecvBytes = 0;
  pSSC->m_bTxIrqEnabled = false;
  pSSC->m_bRxIrqEnabled = false;
  pSSC->m_bWrittenTx = false;
  pSSC->m_vbCommIRQ = false;
  pSSC->m_uCommandByte = 0xFF; // Ensure first write always triggers UpdateCommState
}

void SSC_Initialize(SuperSerialCard* pSSC, uint8_t* pCxRomPeripheral, unsigned int uSlot) {
  const unsigned int SSC_FW_SIZE = 2 * 1024;
  const unsigned int SSC_SLOT_FW_SIZE = 256;
  const unsigned int SSC_SLOT_FW_OFFSET = 7 * 256;

  unsigned char *pData = (unsigned char *) SSC_rom;

  memcpy(pCxRomPeripheral + uSlot * 256, pData + SSC_SLOT_FW_OFFSET, SSC_SLOT_FW_SIZE);

  // Expansion ROM
  if (pSSC->m_pExpansionRom == NULL) {
    pSSC->m_pExpansionRom = new unsigned char[SSC_FW_SIZE];
    if (pSSC->m_pExpansionRom) {
      memcpy(pSSC->m_pExpansionRom, pData, SSC_FW_SIZE);
    }
  }

  RegisterIoHandler(uSlot, &SSC_IORead, &SSC_IOWrite, NULL, NULL, pSSC,
                    pSSC->m_pExpansionRom);
}

void SSC_Destroy(SuperSerialCard* pSSC) {
  SSC_Reset(pSSC);
  delete[] pSSC->m_pExpansionRom;
  pSSC->m_pExpansionRom = NULL;
}

static void GetDIPSW(SuperSerialCard* pSSC) {
  SetDIPSWDefaults(pSSC);

  pSSC->m_uBaudRate = pSSC->m_DIPSWCurrent.uBaudRate;
  pSSC->m_eStopBits = pSSC->m_DIPSWCurrent.eStopBits;
  pSSC->m_uByteSize = pSSC->m_DIPSWCurrent.uByteSize;
  pSSC->m_eParity = pSSC->m_DIPSWCurrent.eParity;
  pSSC->m_uControlByte = GenerateControl(pSSC);
  pSSC->m_uCommandByte = 0x00;
}

static void SetDIPSWDefaults(SuperSerialCard* pSSC) {
  pSSC->m_DIPSWCurrent = g_DIPSWDefault;
}

static unsigned char GenerateControl(SuperSerialCard* pSSC) {
  const unsigned int CLK = 1;  // Internal
  unsigned int bmByteSize = (8 - pSSC->m_uByteSize);
  assert(bmByteSize <= 3);

  unsigned int StopBit;
  if (((pSSC->m_uByteSize == 8) && (pSSC->m_eParity != SSC_PARITY_NONE)) || (pSSC->m_eStopBits != SSC_STOP_BITS_1)) {
    StopBit = 1;
  } else {
    StopBit = 0;
  }

  return (StopBit << 7) | (bmByteSize << 5) | (CLK << 4) | BaudRateToIndex(pSSC->m_uBaudRate);
}

static unsigned int BaudRateToIndex(unsigned int uBaudRate) {
  switch (uBaudRate) {
    case SSC_B110:   return 0x05;
    case SSC_B300:   return 0x06;
    case SSC_B600:   return 0x07;
    case SSC_B1200:  return 0x08;
    case SSC_B2400:  return 0x0A;
    case SSC_B4800:  return 0x0C;
    case SSC_B9600:  return 0x0E;
    case SSC_B19200: return 0x0F;
  }
  return 0x0E; // Default 9600
}

static void UpdateCommState(SuperSerialCard* pSSC) {
  if (SSCFrontend_IsActive()) {
      SSCFrontend_UpdateState(pSSC->m_uBaudRate, pSSC->m_uByteSize, pSSC->m_eParity, pSSC->m_eStopBits);
  }
}

unsigned char SSC_IORead(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, uint32_t nCyclesLeft) {
  unsigned int uSlot = ((uAddr & 0xff) >> 4) - 8;
  SuperSerialCard *pSSC = (SuperSerialCard *) MemGetSlotParameters(uSlot);

  switch (uAddr & 0xf) {
    case 0x1: return CommDipSw(pSSC, PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x2: return CommDipSw(pSSC, PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x8: return CommReceive(pSSC, PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x9: return CommStatus(pSSC, PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xA: return CommCommand(pSSC, PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xB: return CommControl(pSSC, PC, uAddr, bWrite, uValue, nCyclesLeft);
    default:  return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
  }
}

unsigned char SSC_IOWrite(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, uint32_t nCyclesLeft) {
  unsigned int uSlot = ((uAddr & 0xff) >> 4) - 8;
  SuperSerialCard *pSSC = (SuperSerialCard *) MemGetSlotParameters(uSlot);

  switch (uAddr & 0xf) {
    case 0x8: return CommTransmit(pSSC, PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x9: return CommStatus(pSSC, PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xA: return CommCommand(pSSC, PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xB: return CommControl(pSSC, PC, uAddr, bWrite, uValue, nCyclesLeft);
    default:  return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
  }
}

static unsigned char CommCommand(SuperSerialCard* pSSC, unsigned short, unsigned short, unsigned char write, unsigned char value, uint32_t) {
  if (write && (value != pSSC->m_uCommandByte)) {
    pSSC->m_uCommandByte = value;
    if (pSSC->m_uCommandByte & 0x20) {
      switch (pSSC->m_uCommandByte & 0xC0) {
        case 0x00 : pSSC->m_eParity = SSC_PARITY_ODD; break;
        case 0x40 : pSSC->m_eParity = SSC_PARITY_EVEN; break;
        case 0x80 : pSSC->m_eParity = SSC_PARITY_MARK; break;
        case 0xC0 : pSSC->m_eParity = SSC_PARITY_SPACE; break;
      }
    } else {
      pSSC->m_eParity = SSC_PARITY_NONE;
    }

    switch (pSSC->m_uCommandByte & 0x0C) {
      case 0 << 2: pSSC->m_bTxIrqEnabled = false; break;
      case 1 << 2: pSSC->m_bTxIrqEnabled = true; break;
      case 2 << 2: pSSC->m_bTxIrqEnabled = false; break;
      case 3 << 2: pSSC->m_bTxIrqEnabled = false; break;
    }
    pSSC->m_bRxIrqEnabled = ((pSSC->m_uCommandByte & 0x02) == 0);
    UpdateCommState(pSSC);
  }
  return pSSC->m_uCommandByte;
}

static unsigned char CommControl(SuperSerialCard* pSSC, unsigned short, unsigned short, unsigned char write, unsigned char value, uint32_t) {
  if (write && (value != pSSC->m_uControlByte)) {
    pSSC->m_uControlByte = value;
    switch (pSSC->m_uControlByte & 0x0F) {
      case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
        pSSC->m_uBaudRate = SSC_B110; break;
      case 0x06: pSSC->m_uBaudRate = SSC_B300; break;
      case 0x07: pSSC->m_uBaudRate = SSC_B600; break;
      case 0x08: pSSC->m_uBaudRate = SSC_B1200; break;
      case 0x09: case 0x0A: pSSC->m_uBaudRate = SSC_B2400; break;
      case 0x0B: case 0x0C: pSSC->m_uBaudRate = SSC_B4800; break;
      case 0x0D: case 0x0E: pSSC->m_uBaudRate = SSC_B9600; break;
      case 0x0F: pSSC->m_uBaudRate = SSC_B19200; break;
    }

    switch (pSSC->m_uControlByte & 0x60) {
      case 0x00: pSSC->m_uByteSize = 8; break;
      case 0x20: pSSC->m_uByteSize = 7; break;
      case 0x40: pSSC->m_uByteSize = 6; break;
      case 0x60: pSSC->m_uByteSize = 5; break;
    }

    if (pSSC->m_uControlByte & 0x80) {
      if ((pSSC->m_uByteSize == 8) && (pSSC->m_eParity != SSC_PARITY_NONE)) pSSC->m_eStopBits = SSC_STOP_BITS_1;
      else if ((pSSC->m_uByteSize == 5) && (pSSC->m_eParity == SSC_PARITY_NONE)) pSSC->m_eStopBits = SSC_STOP_BITS_1_5;
      else pSSC->m_eStopBits = SSC_STOP_BITS_2;
    } else {
      pSSC->m_eStopBits = SSC_STOP_BITS_1;
    }
    UpdateCommState(pSSC);
  }
  return pSSC->m_uControlByte;
}

static unsigned char CommReceive(SuperSerialCard* pSSC, unsigned short, unsigned short, unsigned char, unsigned char, uint32_t) {
  unsigned char result = 0;
  if (pSSC->m_vRecvBytes) {
    result = pSSC->m_RecvBuffer[0];
    pSSC->m_vRecvBytes--;
    for (unsigned int i = 0; i < pSSC->m_vRecvBytes; ++i) {
        pSSC->m_RecvBuffer[i] = pSSC->m_RecvBuffer[i+1];
    }
  }
  return result;
}

static unsigned char CommTransmit(SuperSerialCard* pSSC, unsigned short, unsigned short, unsigned char, unsigned char value, uint32_t) {
  SSCFrontend_SendByte(value);
  pSSC->m_bWrittenTx = true; // Transmit interrupt pending
  if (pSSC->m_bTxIrqEnabled) {
      pSSC->m_vbCommIRQ = true;
      CpuIrqAssert(IS_SSC);
  }
  return 0;
}

enum {
  ST_PARITY_ERR = 1 << 0, ST_FRAMING_ERR = 1 << 1, ST_OVERRUN_ERR = 1 << 2,
  ST_RX_FULL = 1 << 3, ST_TX_EMPTY = 1 << 4, ST_DCD = 1 << 5,
  ST_DSR = 1 << 6, ST_IRQ = 1 << 7
};

static unsigned char CommStatus(SuperSerialCard* pSSC, unsigned short, unsigned short, unsigned char write, unsigned char, uint32_t) {
  if (write) {
    // Programmed Reset: Datasheet pg 6-8
    // Clears bits 0-4 of Command Register
    pSSC->m_uCommandByte &= 0xE0;
    pSSC->m_bTxIrqEnabled = false;
    pSSC->m_bRxIrqEnabled = true; // Bit 1 = 0 means enabled for 6551

    // Status bits 0-3 cleared, bit 4 (TDRE) set, IRQ cleared
    pSSC->m_bWrittenTx = false;
    pSSC->m_vRecvBytes = 0;
    pSSC->m_vbCommIRQ = false;
    CpuIrqDeassert(IS_SSC);
    return 0;
  }

  bool bTxIRQ = pSSC->m_bTxIrqEnabled && pSSC->m_bWrittenTx;
  bool bRxIRQ = pSSC->m_bRxIrqEnabled && pSSC->m_vRecvBytes;
  bool bIRQ = bTxIRQ || bRxIRQ;

  // Reading the Status Register clears the TDRE interrupt bit (m_bWrittenTx)
  pSSC->m_bWrittenTx = false;

  unsigned char uStatus = ST_TX_EMPTY | (pSSC->m_vRecvBytes ? ST_RX_FULL : 0x00) | (bIRQ ? ST_IRQ : 0x00);
  if (!SSCFrontend_IsActive()) uStatus |= ST_DSR | ST_DCD;

  // Deassert IRQ only if no other interrupt sources are active
  if (!(pSSC->m_bRxIrqEnabled && pSSC->m_vRecvBytes)) {
      CpuIrqDeassert(IS_SSC);
      pSSC->m_vbCommIRQ = false;
  }

  return uStatus;
}

static unsigned char CommDipSw(SuperSerialCard* pSSC, unsigned short, unsigned short addr, unsigned char, unsigned char, uint32_t) {
  unsigned char sw = 0;
  if ((addr & 0xf) == 1) {
    sw = (BaudRateToIndex(pSSC->m_DIPSWCurrent.uBaudRate) << 4) | pSSC->m_DIPSWCurrent.eFirmwareMode;
  } else if ((addr & 0xf) == 2) {
    unsigned char INT = pSSC->m_DIPSWCurrent.eStopBits == SSC_STOP_BITS_2 ? 1 : 0;
    unsigned char DCD = pSSC->m_DIPSWCurrent.uByteSize == 7 ? 1 : 0;
    unsigned char RDR, OVR;
    switch (pSSC->m_DIPSWCurrent.eParity) {
      case SSC_PARITY_ODD:  RDR = 0; OVR = 1; break;
      case SSC_PARITY_EVEN: RDR = 1; OVR = 1; break;
      default:              RDR = 0; OVR = 0; break;
    }
    unsigned char FE = pSSC->m_DIPSWCurrent.bLinefeed ? 1 : 0;
    unsigned char PE = pSSC->m_DIPSWCurrent.bInterrupts ? 1 : 0;
    sw = (INT << 7) | (DCD << 5) | (RDR << 3) | (OVR << 2) | (FE << 1) | (PE << 0);
  }
  return sw;
}

void SSC_PushRxByte(SuperSerialCard* pSSC, uint8_t byte) {
    if (pSSC->m_vRecvBytes < uRecvBufferSize) {
        pSSC->m_RecvBuffer[pSSC->m_vRecvBytes++] = byte;
        if (pSSC->m_bRxIrqEnabled) {
            pSSC->m_vbCommIRQ = true;
            CpuIrqAssert(IS_SSC);
        }
    }
}

unsigned int SSC_GetSnapshot(SuperSerialCard* pSSC, SS_IO_Comms *pSS) {
  pSS->baudrate = pSSC->m_uBaudRate;
  pSS->bytesize = pSSC->m_uByteSize;
  pSS->commandbyte = pSSC->m_uCommandByte;
  pSS->comminactivity = 0;
  pSS->controlbyte = pSSC->m_uControlByte;
  pSS->parity = pSSC->m_eParity;
  memcpy(pSS->recvbuffer, pSSC->m_RecvBuffer, uRecvBufferSize);
  pSS->recvbytes = pSSC->m_vRecvBytes;
  pSS->stopbits = pSSC->m_eStopBits;
  return 0;
}

unsigned int SSC_SetSnapshot(SuperSerialCard* pSSC, SS_IO_Comms *pSS) {
  pSSC->m_uBaudRate = pSS->baudrate;
  pSSC->m_uByteSize = pSS->bytesize;
  pSSC->m_uCommandByte = pSS->commandbyte;
  pSSC->m_uControlByte = pSS->controlbyte;
  pSSC->m_eParity = (SscParity)pSS->parity;
  memcpy(pSSC->m_RecvBuffer, pSS->recvbuffer, uRecvBufferSize);
  pSSC->m_vRecvBytes = pSS->recvbytes;
  pSSC->m_eStopBits = (SscStopBits)pSS->stopbits;
  return 0;
}
