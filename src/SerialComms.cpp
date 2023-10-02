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

/* Description: Super Serial Card emulation
 *
 * Author: Various
 */

// TO DO:
// . Enable & test Tx IRQ
// . DIP switch read values
//

// Refs:
// (1) "Super Serial Card (SSC) Memory Locations for Programmers" - Aaron Heiss
// (2) SSC recv IRQ example: http://www.wright.edu/~john.matthews/ssc.html#lst - John B. Matthews, 5/13/87
// (3) WaitCommEvent, etc: http://mail.python.org/pipermail/python-list/2002-November/131437.html
// (4) SY6551 info: http://www.axess.com/twilight/sock/rs232pak.html
//

/* Adaptation for SDL and POSIX (l) by beom beotiger, Nov-Dec 2007 */

// for read() and write()
#include <unistd.h>
#include "stdafx.h"
#include <assert.h>
// for terminal structure and funcs
#include <termios.h>
#include <fcntl.h>
// for read() and write()
#include <unistd.h>

#include "dataring.h"   // rxbuf

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


// guards rxbuf. I don't think that the Linux port actually does still use threads,
// but that's how it was in mainline linapple-pie so I'll keep it for now.
static pthread_mutex_t 	m_CriticalSection = PTHREAD_MUTEX_INITIALIZER;

// Default: 19200-8-N-1
// Maybe a better default is: 9600-7-N-1 (for HyperTrm)
SSC_DIPSW CSuperSerialCard::m_DIPSWDefault = {
  // DIPSW1:
  B19200, // CBR_# (win32) -> B# (*nix?)
  FWMODE_CIC,

  // DIPSW2:
  ONESTOPBIT, 8,        // ByteSize
  NOPARITY, true,      // LF
  false,
};

CSuperSerialCard::CSuperSerialCard() {
  m_dwSerialPort = 0;

  GetDIPSW();

  m_hCommHandle = -1;
  m_dwCommInactivity = 0;

  m_bTxIrqEnabled = false;
  m_bRxIrqEnabled = false;

  m_bWrittenTx = false;

  m_vbCommIRQ = false;
  m_hCommThread = NULL;

  for (unsigned int i = 0; i < COMMEVT_MAX; i++)
    m_hCommEvent[i] = NULL;

  memset(&m_o, 0, sizeof(m_o));
	rxbuf = new DataRing(256);
}

CSuperSerialCard::~CSuperSerialCard() {
	delete rxbuf;
	rxbuf = nullptr;
}


// TODO: Serial Comms - UI Property Sheet Page:
// . Ability to config the 2x DIPSWs - only takes affect after next Apple2 reset
// . 'Default' button that resets DIPSWs to DIPSWDefaults

void CSuperSerialCard::GetDIPSW() {
  // TODO: Read settings from Registry
  // In the meantime, use the defaults:
  SetDIPSWDefaults();

  m_uBaudRate = m_DIPSWCurrent.uBaudRate;
  m_uStopBits = m_DIPSWCurrent.uStopBits;
  m_uByteSize = m_DIPSWCurrent.uByteSize;
  m_uParity = m_DIPSWCurrent.uParity;
  m_uControlByte = GenerateControl();
  m_uCommandByte = 0x00;
}

void CSuperSerialCard::SetDIPSWDefaults()
{
  // Default DIPSW settings (comms mode)

  // DIPSW1:
  m_DIPSWCurrent.uBaudRate = m_DIPSWDefault.uBaudRate;
  m_DIPSWCurrent.eFirmwareMode = m_DIPSWDefault.eFirmwareMode;

  // DIPSW2:
  m_DIPSWCurrent.uStopBits = m_DIPSWDefault.uStopBits;
  m_DIPSWCurrent.uByteSize = m_DIPSWDefault.uByteSize;
  m_DIPSWCurrent.uParity = m_DIPSWDefault.uParity;
  m_DIPSWCurrent.bLinefeed = m_DIPSWDefault.bLinefeed;
  m_DIPSWCurrent.bInterrupts = m_DIPSWDefault.bInterrupts;
}

unsigned char CSuperSerialCard::GenerateControl()
{
  const unsigned int CLK = 1;  // Internal

  unsigned int bmByteSize = (8 - m_uByteSize);  // [8,7,6,5] -> [0,1,2,3]
  _ASSERT(bmByteSize <= 3);

  unsigned int StopBit;
  if (((m_uByteSize == 8) && (m_uParity != NOPARITY)) || (m_uStopBits != ONESTOPBIT)) {
    StopBit = 1;
  } else {
    StopBit = 0;
  }

  return (StopBit << 7) | (bmByteSize << 5) | (CLK << 4) | BaudRateToIndex(m_uBaudRate);
}

unsigned int CSuperSerialCard::BaudRateToIndex(unsigned int uBaudRate) {
  switch (uBaudRate) { // changed: CBR_# to B# (for *nix?) --bb
    case B110:
      return 0x05;
    case B300:
      return 0x06;
    case B600:
      return 0x07;
    case B1200:
      return 0x08;
    case B2400:
      return 0x0A;
    case B4800:
      return 0x0C;
    case B9600:
      return 0x0E;
    case B19200:
      return 0x0F;
  }

  _ASSERT(0);
  return BaudRateToIndex(B9600);
}

void CSuperSerialCard::UpdateCommState() {
  if (m_hCommHandle == -1) {
    return;
  }

  struct termios dcb;
  int l_databits = CS8;
  tcgetattr(m_hCommHandle, &dcb); // get current attributes for m_hCommHandle in dcb

  // don't bork ascii CR chars
	cfmakeraw(&dcb);

  // set input/output speed to m_uBaudRate
  cfsetispeed(&dcb, m_uBaudRate);
  cfsetospeed(&dcb, m_uBaudRate);
  // Enable the receiver and set localmode
  dcb.c_cflag |= (CLOCAL | CREAD);

  switch (m_uParity) {
    case NOPARITY: /* NONE */
      dcb.c_cflag &= ~PARENB;
      break;

    case EVENPARITY: /* EVEN */
      dcb.c_cflag |= PARENB;
      dcb.c_cflag &= ~PARODD;
      break;

    case ODDPARITY: /* ODD */
      dcb.c_cflag |= PARENB;
      dcb.c_cflag |= PARODD;
      break;

    case MARKPARITY: /* MARKPARITY */
      dcb.c_cflag |= PARENB | CMSPAR | PARODD;
      break;

    case SPACEPARITY: /* SPACEPARITY */
      dcb.c_cflag |= PARENB | CMSPAR;
      dcb.c_cflag &= ~PARODD;
      break;
  }

  switch (m_uByteSize) { // data bits
    case 5:
      l_databits = CS5;
      break;
    case 6:
      l_databits = CS6;
      break;
    case 7:
      l_databits = CS7;
      break;
    case 8:
      l_databits = CS8;
      break;
  }
  dcb.c_cflag &= ~CSIZE;
  dcb.c_cflag |= l_databits;

  switch (m_uStopBits) {
    case ONE5STOPBITS:
    case ONESTOPBIT:
      dcb.c_cflag &= ~CSTOPB; // 1 stopbit
      break;
    case TWOSTOPBITS:
      dcb.c_cflag |= CSTOPB; // 1.5 and 2 stopbits.
      break;
  }
  dcb.c_cflag &= ~CRTSCTS;               /* Disable hardware flow control */
  /* Enable data to be processed as raw input */
  dcb.c_lflag &= ~(ICANON | ECHO | ISIG);
  /* Set the new options for the port */
  tcsetattr(m_hCommHandle, TCSANOW, &dcb);
}

bool CSuperSerialCard::CheckComm() {
  m_dwCommInactivity = 0;

  if ((m_hCommHandle == -1) && m_dwSerialPort) {
    char portname[32];  // we have /dev/ttyS0..X instead of COM1..COMX+1?
    if (m_dwSerialPort < 0 || m_dwSerialPort > 99) {
      m_dwSerialPort = 1; // buffer overflow check
    }
    sprintf(portname, TEXT("/dev/ttyUSB%u"), (unsigned int) (m_dwSerialPort - 1));
    m_hCommHandle = open(portname, O_RDWR | O_NOCTTY | O_NDELAY);
    if (m_hCommHandle != -1) {
      UpdateCommState();
      CommThInit();
      printf("Opened serial port %s\n", portname);
    }
    else {
      printf("ERROR: failed to open serial port %s: %s\n", portname, strerror(errno));
    }
  }

  return (m_hCommHandle != -1);
}

void CSuperSerialCard::CloseComm()
{
  CommThUninit();    // Kill CommThread before closing COM handle
  if (m_hCommHandle != -1) {
    close(m_hCommHandle); // close device (port?)
  }
  m_hCommHandle = -1;
  m_dwCommInactivity = 0;
}

unsigned char CSuperSerialCard::SSC_IORead(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, ULONG nCyclesLeft) {
  unsigned int uSlot = ((uAddr & 0xff) >> 4) - 8;
  CSuperSerialCard *pSSC = (CSuperSerialCard *) MemGetSlotParameters(uSlot);

  // hack: we need to actually read the buffer sometimes since the receive thread
  // isn't carried over from AppleWin
  pSSC->CheckCommEvent(0);

  switch (uAddr & 0xf) {
    case 0x0:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x1:
      return pSSC->CommDipSw(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x2:
      return pSSC->CommDipSw(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x3:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x4:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x5:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x6:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x7:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x8:
      return pSSC->CommReceive(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x9:
      return pSSC->CommStatus(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xA:
      return pSSC->CommCommand(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xB:
      return pSSC->CommControl(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xC:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xD:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xE:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xF:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
  }

  return 0;
}

unsigned char CSuperSerialCard::SSC_IOWrite(unsigned short PC, unsigned short uAddr, unsigned char bWrite, unsigned char uValue, ULONG nCyclesLeft) {
  unsigned int uSlot = ((uAddr & 0xff) >> 4) - 8;
  CSuperSerialCard *pSSC = (CSuperSerialCard *) MemGetSlotParameters(uSlot);

  switch (uAddr & 0xf) {
    case 0x0:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x1:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x2:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x3:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x4:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x5:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x6:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x7:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x8:
      return pSSC->CommTransmit(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0x9:
      return pSSC->CommStatus(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xA:
      return pSSC->CommCommand(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xB:
      return pSSC->CommControl(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xC:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xD:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xE:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
    case 0xF:
      return IO_Null(PC, uAddr, bWrite, uValue, nCyclesLeft);
  }

  return 0;
}

unsigned char CSuperSerialCard::CommCommand(unsigned short, unsigned short, unsigned char write, unsigned char value, ULONG) {
  if (!CheckComm()) {
    return 0;
  }

  if (write && (value != m_uCommandByte)) {
    m_uCommandByte = value;

    // Update the parity
    if (m_uCommandByte & 0x20) {
      switch (m_uCommandByte & 0xC0) {
        case 0x00 :
          m_uParity = ODDPARITY;
          break;
        case 0x40 :
          m_uParity = EVENPARITY;
          break;
        case 0x80 :
          m_uParity = MARKPARITY;
          break;
        case 0xC0 :
          m_uParity = SPACEPARITY;
          break;
      }
    } else {
      m_uParity = NOPARITY;
    }

    if (m_uCommandByte & 0x10)  // Receiver mode echo [0=no echo, 1=echo]
    {
    }

    switch (m_uCommandByte & 0x0C)  // transmitter interrupt control
    {
      // Note: the RTS signal must be set 'low' in order to receive any
      // incoming data from the serial device
      case 0 << 2: // set RTS high and transmit no interrupts
        m_bTxIrqEnabled = false;
        break;
      case 1 << 2: // set RTS low and transmit interrupts
        m_bTxIrqEnabled = true;
        break;
      case 2 << 2: // set RTS low and transmit no interrupts
        m_bTxIrqEnabled = false;
        break;
      case 3 << 2: // set RTS low and transmit break signals instead of interrupts
        m_bTxIrqEnabled = false;
        break;
    }

    // interrupt request disable [0=enable receiver interrupts]
    m_bRxIrqEnabled = ((m_uCommandByte & 0x02) == 0);

    if (m_uCommandByte & 0x01)  // Data Terminal Ready (DTR) setting [0=set DTR high (indicates 'not ready')]
    {
      // Note that, although the DTR is generally not used in the SSC (it may actually not
      // be connected!), it must be set to 'low' in order for the 6551 to function correctly.
    }

    UpdateCommState();
  }

  return m_uCommandByte;
}

unsigned char CSuperSerialCard::CommControl(unsigned short, unsigned short, unsigned char write, unsigned char value, ULONG) {
  if (!CheckComm()) {
    return 0;
  }

  if (write && (value != m_uControlByte)) {
    m_uControlByte = value;

    // UPDATE THE BAUD RATE
    switch (m_uControlByte & 0x0F) {
      // Note that 1 MHz Apples (everything other than the Apple IIgs and //c
      // Plus running in "fast" mode) cannot handle 19.2 kbps, and even 9600
      // bps on these machines requires either some highly optimised code or
      // a decent buffer in the device being accessed.  The faster Apples
      // have no difficulty with this speed, however.

      case 0x00: // fall through [16x external clock]
      case 0x01: // fall through [50 bps]
      case 0x02: // fall through [75 bps]
      case 0x03: // fall through [109.92 bps]
      case 0x04: // fall through [134.58 bps]
      case 0x05:
        m_uBaudRate = B110;
        break;  // [150 bps]
      case 0x06:
        m_uBaudRate = B300;
        break;
      case 0x07:
        m_uBaudRate = B600;
        break;
      case 0x08:
        m_uBaudRate = B1200;
        break;
      case 0x09: // fall through [1800 bps]
      case 0x0A:
        m_uBaudRate = B2400;
        break;
      case 0x0B: // fall through [3600 bps]
      case 0x0C:
        m_uBaudRate = B4800;
        break;
      case 0x0D: // fall through [7200 bps]
      case 0x0E:
        m_uBaudRate = B9600;
        break;
      case 0x0F:
        m_uBaudRate = B19200;
        break;
    }

    if (m_uControlByte & 0x10) {
      // receiver clock source [0= external, 1= internal]
    }

    // UPDATE THE unsigned char SIZE
    switch (m_uControlByte & 0x60) {
      case 0x00:
        m_uByteSize = 8;
        break;
      case 0x20:
        m_uByteSize = 7;
        break;
      case 0x40:
        m_uByteSize = 6;
        break;
      case 0x60:
        m_uByteSize = 5;
        break;
    }

    // UPDATE THE NUMBER OF STOP BITS
    if (m_uControlByte & 0x80) {
      if ((m_uByteSize == 8) && (m_uParity != NOPARITY))
        m_uStopBits = ONESTOPBIT;
      else if ((m_uByteSize == 5) && (m_uParity == NOPARITY))
        m_uStopBits = ONE5STOPBITS;
      else
        m_uStopBits = TWOSTOPBITS;
    } else {
      m_uStopBits = ONESTOPBIT;
    }

    UpdateCommState();
  }

  return m_uControlByte;
}

static const char *descbyte(uint8_t byte) {
	char tmp[8] = { '\'', (char)byte, '\'', ' ', 0 };
	const char *bstr = tmp;
	if (!isprint(byte)) {
		switch(byte) {
			case 13: bstr = "<CR> "; break;
			case 10: bstr = "<LF> "; break;
			case '\t': bstr = "<\\t> "; break;
			case '\b': bstr = "<\\b> "; break;
			default:
				bstr = "";
		}
	}

	static char retstr[32];
	sprintf(retstr, "%s(%d, 0x%02x)", bstr, byte, byte);
	return retstr;
}

unsigned char CSuperSerialCard::CommReceive(unsigned short, unsigned short, unsigned char, unsigned char, ULONG) {
  if (!CheckComm()) {
    return 0;
  }

	int rdchar = rxbuf->ReadByte();
	if (rdchar >= 0) {
		if (m_vbCommIRQ && rxbuf->IsEmpty()) {
			// read last byte, so get CommThread to call WaitCommEvent() again
			fprintf(stderr, "CommRecv: SetEvent - ACK\n");
			//SetEvent(m_hCommEvent[COMMEVT_ACK]);
		}

		printf("\e[1;94m <-- A2 reads incoming byte %s\e[0m\n", descbyte(rdchar));
		return (BYTE)rdchar;
	}
	else {
		printf("Apple II attempted to read serial w/ empty buffer\n");
		return 0;
	}
}

unsigned char CSuperSerialCard::CommTransmit(unsigned short, unsigned short, unsigned char, unsigned char value, ULONG) {
  if (!CheckComm()) {
    return 0;
  }

  unsigned int uBytesWritten;
  uBytesWritten = write(m_hCommHandle, &value, 1);

  m_bWrittenTx = true;  // Transmit done
  m_uLastBytesWritten = uBytesWritten;

  // TO DO:
  // 1) Use CommThread determine when transmit is complete
  // 2) OR do this:
  //if (m_bTxIrqEnabled)
  //  CpuIrqAssert(IS_SSC);

  return 0;
}

// 6551 ACIA Status Register ($C089+s0)
// ------------------------------------
// Bit   Value   Meaning
// 0    1       Parity error
// 1    1       Framing error
// 2    1       Overrun error
// 3    1       Receive register full
// 4    1       Transmit register empty
// 5    0       Data Carrier Detect (DCD) true [0=DCD low (detected), 1=DCD high (not detected)]
// 6    0       Data Set Ready (DSR) true [0=DSR low (ready), 1=DSR high (not ready)]
// 7    1       Interrupt (IRQ) true (cleared by reading status reg [Ref.4])

enum {
  ST_PARITY_ERR = 1 << 0,
  ST_FRAMING_ERR = 1 << 1,
  ST_OVERRUN_ERR = 1 << 2,
  ST_RX_FULL = 1 << 3,
  ST_TX_EMPTY = 1 << 4,
  ST_DCD = 1 << 5,
  ST_DSR = 1 << 6,
  ST_IRQ = 1 << 7
};

unsigned char CSuperSerialCard::CommStatus(unsigned short, unsigned short, unsigned char, unsigned char, ULONG)
{
  if (!CheckComm())
    return ST_DSR | ST_DCD | ST_TX_EMPTY;

  #ifdef SUPPORT_MODEM
  unsigned int modemstatus = 0;
  GetCommModemStatus(m_hCommHandle,&modemstatus);        // Returns 0x30 = MS_DSR_ON|MS_CTS_ON
  #endif

  // TO DO - ST_TX_EMPTY:
  // . IRQs enabled  : set after WaitCommEvent has signaled that TX has completed
  // . IRQs disabled : always set it [Currently done]
  //

  // So that /m_vRecvBytes/ doesn't change midway (from 0 to 1):
  // . bIRQ=false, but uStatus.ST_RX_FULL=1
  //  EnterCriticalSection(&m_CriticalSection);
  pthread_mutex_lock(&m_CriticalSection);
  bool bIRQ = false;
  if (m_bTxIrqEnabled && m_bWrittenTx) {
    bIRQ = true;
  }
  if (m_bRxIrqEnabled && rxbuf->IsEmpty()) {
    bIRQ = true;
  }

  m_bWrittenTx = false;  // Read status reg always clears IRQ

  unsigned char uStatus = ST_TX_EMPTY | (!rxbuf->IsEmpty() ? ST_RX_FULL : 0x00)
                 #ifdef SUPPORT_MODEM
                 | ((modemstatus & MS_RLSD_ON)  ? 0x00 : ST_DCD)  // Need 0x00 to allow ZLink to start up
        | ((modemstatus & MS_DSR_ON)  ? 0x00 : ST_DSR)
                 #endif
                 | (bIRQ ? ST_IRQ : 0x00);

  pthread_mutex_unlock(&m_CriticalSection);
  CpuIrqDeassert(IS_SSC);

  return uStatus;
}

unsigned char CSuperSerialCard::CommDipSw(unsigned short, unsigned short addr, unsigned char, unsigned char, ULONG) {
  unsigned char sw = 0;
  switch (addr & 0xf) {
    case 1:  // DIPSW1
      sw = (BaudRateToIndex(m_DIPSWCurrent.uBaudRate) << 4) | m_DIPSWCurrent.eFirmwareMode;
      break;

    case 2:  // DIPSW2
      // Comms mode - SSC manual, pg23/24
      unsigned char INT = m_DIPSWCurrent.uStopBits == TWOSTOPBITS ? 1 : 0;  // SW2-1 (Stop bits: 1-ON(0); 2-OFF(1))
      unsigned char DSR = 0;                        // Always zero
      unsigned char DCD = m_DIPSWCurrent.uByteSize == 7 ? 1 : 0;      // SW2-2 (Data bits: 8-ON(0); 7-OFF(1))
      unsigned char TDR = 0;                        // Always zero

      // SW2-3 (Parity: odd-ON(0); even-OFF(1))
      // SW2-4 (Parity: none-ON(0); SW2-3-OFF(1))
      unsigned char RDR, OVR;
      switch (m_DIPSWCurrent.uParity) {
        case ODDPARITY:
          RDR = 0;
          OVR = 1;
          break;
        case EVENPARITY:
          RDR = 1;
          OVR = 1;
          break;
        default:
          _ASSERT(0);
        case NOPARITY:
          RDR = 0;
          OVR = 0;
          break;
      }

      unsigned char FE = m_DIPSWCurrent.bLinefeed ? 1 : 0;          // SW2-5 (LF: yes-ON(0); no-OFF(1))
      unsigned char PE = m_DIPSWCurrent.bInterrupts ? 1 : 0;        // SW2-6 (Interrupts: yes-ON(0); no-OFF(1))

      sw = (INT << 7) | (DSR << 6) | (DCD << 5) | (TDR << 4) | (RDR << 3) | (OVR << 2) | (FE << 1) | (PE << 0);
      break;
  }
  return sw;
}

void CSuperSerialCard::CommInitialize(LPBYTE pCxRomPeripheral, unsigned int uSlot)
{
  const unsigned int SSC_FW_SIZE = 2 * 1024;
  const unsigned int SSC_SLOT_FW_SIZE = 256;
  const unsigned int SSC_SLOT_FW_OFFSET = 7 * 256;

  unsigned char *pData = (unsigned char *) SSC_rom;  // NB. Don't need to unlock resource

  memcpy(pCxRomPeripheral + uSlot * 256, pData + SSC_SLOT_FW_OFFSET, SSC_SLOT_FW_SIZE);

  // Expansion ROM
  if (m_pExpansionRom == NULL) {
    m_pExpansionRom = new unsigned char[SSC_FW_SIZE];
    if (m_pExpansionRom) {
      memcpy(m_pExpansionRom, pData, SSC_FW_SIZE);
    }
  }

  RegisterIoHandler(uSlot, &CSuperSerialCard::SSC_IORead, &CSuperSerialCard::SSC_IOWrite, NULL, NULL, this,
                    m_pExpansionRom);
}

void CSuperSerialCard::CommReset() {
  CloseComm();
  GetDIPSW();
  rxbuf->Clear();
  m_bTxIrqEnabled = false;
  m_bRxIrqEnabled = false;
  m_bWrittenTx = false;
  m_vbCommIRQ = false;
}

void CSuperSerialCard::CommDestroy() {
  CommReset();
  delete[] m_pExpansionRom;
  m_pExpansionRom = NULL;
}

void CSuperSerialCard::CommSetSerialPort(/*HWND window,*/ unsigned int newserialport) {
  if (m_hCommHandle == -1) {
    m_dwSerialPort = newserialport;
  } else {
    fprintf(stderr, "You cannot change the serial port while it is in use!\n");
  }
}

void CSuperSerialCard::CommUpdate(unsigned int totalcycles) {
  if (m_hCommHandle == -1) {
    return;
  }

  if ((m_dwCommInactivity += totalcycles) > 1000000) {
    static unsigned int lastcheck = 0;

    if ((m_dwCommInactivity > 2000000) || (m_dwCommInactivity - lastcheck > 99950)) {
      #ifdef SUPPORT_MODEM
      unsigned int modemstatus = 0;
      GetCommModemStatus(m_hCommHandle,&modemstatus);
      if ((modemstatus & MS_RLSD_ON) || DiskIsSpinning())
        m_dwCommInactivity = 0;
      #else
      if (DiskIsSpinning())
        m_dwCommInactivity = 0;
      #endif
    }
  }
}

void CSuperSerialCard::CheckCommEvent(unsigned int dwEvtMask) {
	int maxBytes = rxbuf->BytesFree();
	if (maxBytes > 0) {
		uint8_t temp[maxBytes];
		int nbytes = read(m_hCommHandle, temp, maxBytes);
		if (nbytes > 0) {
      printf("got SZ! %d\n", nbytes);
      for(int i=0;i<nbytes;i++) {
        printf("byte %d: 0x%02x\n", i, temp[nbytes]);
      }

			pthread_mutex_lock(&m_CriticalSection);
			rxbuf->Append(temp, nbytes);
			printf("serial: received %d bytes; now %d bytes in buffer\n", nbytes, rxbuf->BytesAvail());
			pthread_mutex_unlock(&m_CriticalSection);

			if (m_bRxIrqEnabled) {
				m_vbCommIRQ = true;
				CpuIrqAssert(IS_SSC);
			}
		}
	}
}

unsigned int CSuperSerialCard::CommThread(LPVOID lpParameter) {
  return 0;
}

bool CSuperSerialCard::CommThInit() {
  return true;
}

void CSuperSerialCard::CommThUninit() {
}

unsigned int CSuperSerialCard::CommGetSnapshot(SS_IO_Comms *pSS)
{
  pSS->baudrate = m_uBaudRate;
  pSS->bytesize = m_uByteSize;
  pSS->commandbyte = m_uCommandByte;
  pSS->comminactivity = m_dwCommInactivity;
  pSS->controlbyte = m_uControlByte;
  pSS->parity = m_uParity;
  pSS->stopbits = m_uStopBits;

  int nbytes = rxbuf->BytesAvail();
	uint8_t *peekbytes = rxbuf->Read(nbytes, false);
	memcpy(pSS->recvbuffer, peekbytes, nbytes);
	pSS->recvbytes = nbytes;
  return 0;
}

unsigned int CSuperSerialCard::CommSetSnapshot(SS_IO_Comms *pSS)
{
  m_uBaudRate = pSS->baudrate;
  m_uByteSize = pSS->bytesize;
  m_uCommandByte = pSS->commandbyte;
  m_dwCommInactivity = pSS->comminactivity;
  m_uControlByte = pSS->controlbyte;
  m_uParity = pSS->parity;
  m_uStopBits = pSS->stopbits;

  rxbuf->Clear();
  rxbuf->Append(pSS->recvbuffer, pSS->recvbytes);
  return 0;
}

