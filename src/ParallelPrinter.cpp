/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski, Nick Westgate

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

/* Description: Parallel Printer Interface Card emulation
 *
 * Author: Nick Westgate
 */

/* Adaptation for SDL and POSIX (l) by beom beotiger, Nov-Dec 2007 */

#include "stdafx.h"

char Parallel_bin[] = "\x18\xB0\x38\x48\x8A\x48\x98\x48\x08\x78\x20\x58\xFF\xBA\x68\x68"
                      "\x68\x68\xA8\xCA\x9A\x68\x28\xAA\x90\x38\xBD\xB8\x05\x10\x19\x98"
                      "\x29\x7F\x49\x30\xC9\x0A\x90\x3B\xC9\x78\xB0\x29\x49\x3D\xF0\x21"
                      "\x98\x29\x9F\x9D\x38\x06\x90\x7E\xBD\xB8\x06\x30\x14\xA5\x24\xDD"
                      "\x38\x07\xB0\x0D\xC9\x11\xB0\x09\x09\xF0\x3D\x38\x07\x65\x24\x85"
                      "\x24\x4A\x38\xB0\x6D\x18\x6A\x3D\xB8\x06\x90\x02\x49\x81\x9D\xB8"
                      "\x06\xD0\x53\xA0\x0A\x7D\x38\x05\x88\xD0\xFA\x9D\xB8\x04\x9D\x38"
                      "\x05\x38\xB0\x43\xC5\x24\x90\x3A\x68\xA8\x68\xAA\x68\x4C\xF0\xFD"
                      "\x90\xFE\xB0\xFE\x99\x80\xC0\x90\x37\x49\x07\xA8\x49\x0A\x0A\xD0"
                      "\x06\xB8\x85\x24\x9D\x38\x07\xBD\xB8\x06\x4A\x70\x02\xB0\x23\x0A"
                      "\x0A\xA9\x27\xB0\xCF\xBD\x38\x07\xFD\xB8\x04\xC9\xF8\x90\x03\x69"
                      "\x27\xAC\xA9\x00\x85\x24\x18\x7E\xB8\x05\x68\xA8\x68\xAA\x68\x60"
                      "\x90\x27\xB0\x00\x10\x11\xA9\x89\x9D\x38\x06\x9D\xB8\x06\xA9\x28"
                      "\x9D\xB8\x04\xA9\x02\x85\x36\x98\x5D\x38\x06\x0A\xF0\x90\x5E\xB8"
                      "\x05\x98\x48\x8A\x0A\x0A\x0A\x0A\xA8\xBD\x38\x07\xC5\x24\x68\xB0"
                      "\x05\x48\x29\x80\x09\x20\x2C\x58\xFF\xF0\x03\xFE\x38\x07\x70\x84";


static unsigned int inactivity = 0;
static unsigned int g_PrinterIdleLimit = 10;
static FILE *file = NULL;
unsigned int const PRINTDRVR_SIZE = 0x100;
bool g_bPrinterAppend = true;

static unsigned char PrintStatus(unsigned short, unsigned short, unsigned char, unsigned char, ULONG);

static unsigned char PrintTransmit(unsigned short, unsigned short, unsigned char, unsigned char value, ULONG);

void PrintLoadRom(LPBYTE pCxRomPeripheral, const unsigned int uSlot) {
  unsigned char *pData = (unsigned char *) Parallel_bin;  // NB. Don't need to unlock resource
  memcpy(pCxRomPeripheral + uSlot * 256, pData, PRINTDRVR_SIZE);
  RegisterIoHandler(uSlot, PrintStatus, PrintTransmit, NULL, NULL, NULL, NULL);
}

static bool CheckPrint()
{
  inactivity = 0;
  if (file == NULL) {
    file = fopen(g_sParallelPrinterFile, (g_bPrinterAppend) ? "ab" : "wb");
  }
  return (file != NULL);
}

static void ClosePrint() {
  if (file != NULL) {
    fclose(file);
    file = NULL;
  }
  inactivity = 0;
}

void PrintDestroy() {
  ClosePrint();
}

void PrintUpdate(unsigned int totalcycles) {
  if (file == NULL) {
    return;
  }
  if ((inactivity += totalcycles) > (Printer_GetIdleLimit() * 1000 * 1000))
  {
    // inactive, so close the file (next print will overwrite it)
    ClosePrint();
  }
}

void PrintReset() {
  ClosePrint();
}

static unsigned char PrintStatus(unsigned short, unsigned short, unsigned char, unsigned char, ULONG) {
  CheckPrint();
  return 0xFF; // status - TODO?
}

static unsigned char PrintTransmit(unsigned short, unsigned short, unsigned char, unsigned char value, ULONG) {
  if (!CheckPrint()) {
    return 0;
  }
  char c = value & 0x7F;
  fwrite(&c, 1, 1, file);
  return 0;
}

unsigned int Printer_GetIdleLimit()
{
  return g_PrinterIdleLimit;
}

void Printer_SetIdleLimit(unsigned int Duration)
{
  g_PrinterIdleLimit = Duration;
}
