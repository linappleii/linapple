/*
linapple : An Apple //e emulator for Linux

Based on Apple in PC's mousecard.cpp
- Permission given by Kyle Kim to reuse in AppleWin
Adaptation for SDL and POSIX (l) by beom beotiger, Nov-Dec 2007
*/

#include "core/Common.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "apple2/MouseInterface.h"
#include "apple2/6821.h"
#include "apple2/Memory.h"
#include "apple2/CPU.h"
#include "core/Log.h"
#include "apple2/Video.h"
#include "apple2/Structs.h"
#include "core/Common_Globals.h"

// Sets mouse mode
enum {
MOUSE_SET =    0x00,
// Reads mouse position
MOUSE_READ =    0x10,
// Services mouse interrupt
MOUSE_SERV =    0x20,
// Clears mouse positions to 0 (for delta mode)
MOUSE_CLEAR =    0x30,
// Sets mouse position to a user-defined pos
MOUSE_POS =    0x40,
// Resets mouse clamps to default values
// Sets mouse position to 0,0
MOUSE_INIT =    0x50,
// Sets mouse bounds in a window
MOUSE_CLAMP =    0x60,
// Sets mouse to upper-left corner of clamp win
MOUSE_HOME =    0x70
};

// Set VBL Timing : 0x90 is 60Hz, 0x91 is 50Hz
enum {
MOUSE_TIME =    0x90
};

enum {
BIT0 =    0x01,
BIT1 =    0x02,
BIT2 =    0x04,
BIT3 =    0x08,
BIT4 =    0x10,
BIT5 =    0x20,
BIT6 =    0x40,
BIT7 =    0x80
};

struct MouseInterface sg_Mouse;

static char MouseInterface_rom[] = "\x2C\x58\xFF\x70\x1B\x38\x90\x18\xB8\x50\x15\x01\x20\xF4\xF4\xF4"
                            "\xF4\x00\xB3\xC4\x9B\xA4\xC0\x8A\xDD\xBC\x48\xF0\x53\xE1\xE6\xEC"
                            "\x08\x78\x8D\xF8\x07\x48\x98\x48\x8A\x48\x20\x58\xFF\xBA\xBD\x00"
                            "\x01\xAA\x08\x0A\x0A\x0A\x0A\x28\xA8\xAD\xF8\x07\x8E\xF8\x07\x48"
                            "\xA9\x08\x70\x67\x90\x4D\xB0\x55\x29\x01\x09\xF0\x9D\x38\x06\xA9"
                            "\x02\xD0\x40\x29\x0F\x09\x90\xD0\x35\xFF\xFF\xB9\x83\xC0\x29\xFB"
                            "\x99\x83\xC0\xA9\x3E\x99\x82\xC0\xB9\x83\xC0\x09\x04\x99\x83\xC0"
                            "\xB9\x82\xC0\x29\xC1\x1D\xB8\x05\x99\x82\xC0\x68\xF0\x0A\x6A\x90"
                            "\x75\x68\xAA\x68\xA8\x68\x28\x60\x18\x60\x29\x01\x09\x60\x9D\x38"
                            "\x06\xA9\x0E\x9D\xB8\x05\xA9\x01\x48\xD0\xC0\xA9\x0C\x9D\xB8\x05"
                            "\xA9\x02\xD0\xF4\xA9\x30\x9D\x38\x06\xA9\x06\x9D\xB8\x05\xA9\x00"
                            "\x48\xF0\xA8\xC9\x10\xB0\xD2\x9D\x38\x07\x90\xEA\xA9\x04\xD0\xEB"
                            "\xA9\x40\xD0\xCA\xA4\x06\xA9\x60\x85\x06\x20\x06\x00\x84\x06\xBA"
                            "\xBD\x00\x01\xAA\x0A\x0A\x0A\x0A\xA8\xA9\x20\xD0\xC9\xA9\x70\xD0"
                            "\xC5\x48\xA9\xA0\xD0\xA8\x29\x0F\x09\xB0\xD0\xBA\xA9\xC0\xD0\xB6"
                            "\xA9\x02\xD0\xB7\xA2\x03\x38\x60\xFF\xFF\xFF\xD6\xFF\xFF\xFF\x01"
                            "\x98\x48\xA5\x06\x48\xA5\x07\x48\x86\x07\xA9\x27\x85\x06\x20\x58"
                            "\xFC\xA0\x00\xB1\x06\xF0\x06\x20\xED\xFD\xC8\xD0\xF6\x68\x85\x07"
                            "\x68\x85\x06\x68\xA8\xD0\x5B\xC1\xF0\xF0\xEC\xE5\xCD\xEF\xF5\xF3"
                            "\xE5\x8D\xC3\xEF\xF0\xF9\xF2\xE9\xE7\xE8\xF4\xA0\xB1\xB9\xB8\xB3"
                            "\xA0\xE2\xF9\xA0\xC1\xF0\xF0\xEC\xE5\xA0\xC3\xEF\xED\xF0\xF5\xF4"
                            "\xE5\xF2\xAC\xA0\xC9\xEE\xE3\xAE\x8D\x8D\xC2\xE1\xE3\xE8\xED\xE1"
                            "\xEE\xAF\xCD\xE1\xF2\xEB\xF3\xAF\xCD\xE1\xE3\xCB\xE1\xF9\x8D\x00"
                            "\xB9\x82\xC0\x29\xF1\x1D\xB8\x05\x99\x82\xC0\x68\x30\x0C\xF0\x80"
                            "\xD0\x09\xA9\x00\x9D\xB8\x05\x48\xF0\xE6\x60\xBD\x38\x07\x29\x0F"
                            "\x09\x20\x9D\x38\x07\x8A\x48\x48\x48\x48\xA9\xAA\x48\xBD\x38\x06"
                            "\x48\xA9\x0C\x9D\xB8\x05\xA9\x00\x48\xF0\xC5\xA9\xB3\x48\xAD\x78"
                            "\x04\x18\x90\xEC\xA9\xBC\x48\xAD\xF8\x04\x18\x90\xE3\xA9\x81\x48"
                            "\x7E\x38\x06\x90\x05\xAD\x78\x05\xB0\xD6\x8A\x48\xA9\xD8\x48\xA9"
                            "\x0C\x9D\xB8\x05\xA9\x01\x48\xD0\x97\xBD\x38\x06\x8D\x78\x05\x60"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xC2"
                            "\xBD\x38\x07\x29\x0F\x09\x40\x9D\x38\x07\x8A\x48\x48\x48\xA9\x11"
                            "\xD0\x27\xA9\x1E\x48\xA9\x0C\x9D\xB8\x05\xA9\x01\x48\xD0\x51\xAD"
                            "\xB3\xFB\xC9\x06\xD0\x21\xAD\x19\xC0\x30\xFB\xAD\x19\xC0\x10\xFB"
                            "\xAD\x19\xC0\x30\xFB\xA9\x7F\xD0\x00\x48\xA9\x50\x48\xA9\x0C\x9D"
                            "\xB8\x05\xA9\x00\x48\xF0\x29\xA5\x06\x48\xA5\x07\x48\x98\x48\xA9"
                            "\x20\x85\x07\xA0\x00\x84\x06\xA9\x00\x91\x06\xC8\xD0\xFB\xE6\x07"
                            "\xA5\x07\xC9\x40\xD0\xF1\x68\xA8\xA5\x08\x48\xA9\x00\xF0\x1C\xFF"
                            "\xB9\x82\xC0\x29\xF1\x1D\xB8\x05\x99\x82\xC0\x68\x30\x0A\xF0\x80"
                            "\xA9\x00\x9D\xB8\x05\x48\xF0\xE8\x60\xD0\xAE\xA9\x01\x8D\xD0\x3F"
                            "\x8D\xE0\x3F\xAD\x57\xC0\xAD\x54\xC0\xAD\x52\xC0\xAD\x50\xC0\xEA"
                            "\x85\x06\x85\x07\x85\x08\xE6\x06\xD0\x0E\xE6\x07\xD0\x0C\xE6\x08"
                            "\xA5\x08\xC9\x01\x90\x0A\xB0\x1F\x08\x28\x08\x28\xA9\x00\xA5\x00"
                            "\xAD\xFF\xCF\xB9\x82\xC0\x4A\xEA\xEA\xB0\xDB\xAD\xFF\xCF\xB9\x82"
                            "\xC0\x4A\xA5\x00\xEA\xB0\xCF\x68\x85\x08\x68\x85\x07\x68\x85\x06"
                            "\xA9\xE3\xD0\xA5\xAD\x51\xC0\xAD\x56\xC0\x18\x90\x93\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xC1"
                            "\xBD\x38\x06\xC9\x20\xD0\x06\xA9\x7F\x69\x01\x70\x01\xB8\xB9\x82"
                            "\xC0\x30\xFB\xB9\x81\xC0\x29\xFB\x99\x81\xC0\xA9\xFF\x99\x80\xC0"
                            "\xB9\x81\xC0\x09\x04\x99\x81\xC0\xBD\x38\x06\x99\x80\xC0\xB9\x82"
                            "\xC0\x09\x20\x99\x82\xC0\xB9\x82\xC0\x10\xFB\x29\xDF\x99\x82\xC0"
                            "\x70\x44\xBD\x38\x06\xC9\x30\xD0\x35\xA9\x00\x9D\xB8\x04\x9D\xB8"
                            "\x03\x9D\x38\x05\x9D\x38\x04\xF0\x25\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xB9\x82\xC0\x29\xF1\x1D\xB8\x05\x99\x82\xC0\x68\xF0\x82\xA9\x00"
                            "\x9D\xB8\x05\x48\xF0\xEA\xB9\x81\xC0\x29\xFB\x99\x81\xC0\xA9\x00"
                            "\x99\x80\xC0\xB9\x81\xC0\x09\x04\x99\x81\xC0\xB9\x82\xC0\x0A\x10"
                            "\xFA\xB9\x80\xC0\x9D\x38\x06\xB9\x82\xC0\x09\x10\x99\x82\xC0\xB9"
                            "\x82\xC0\x0A\x30\xFA\xB9\x82\xC0\x29\xEF\x99\x82\xC0\xBD\xB8\x06"
                            "\x29\xF1\x1D\x38\x06\x9D\xB8\x06\x29\x0E\xD0\xB2\xA9\x00\x9D\xB8"
                            "\x05\xA9\x02\x48\xD0\x9A\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xC3"
                            "\xE4\x37\xD0\x2D\xA9\x07\xC5\x36\xF0\x27\x85\x36\x68\xC9\x8D\xF0"
                            "\x74\x29\x01\x09\x80\x9D\x38\x07\x8A\x48\xA9\x84\x48\xBD\x38\x07"
                            "\x4A\xA9\x80\xB0\x01\x0A\x48\xA9\x0C\x9D\xB8\x05\xA9\x00\x48\xF0"
                            "\x3F\xE4\x39\xD0\xD7\xA9\x05\x85\x38\xBD\x38\x07\x29\x01\xD0\x14"
                            "\x68\x68\x68\x68\xA9\x00\x9D\xB8\x03\x9D\xB8\x04\x9D\x38\x04\x9D"
                            "\x38\x05\xF0\x3C\xBD\x38\x07\x29\x01\x09\x80\x9D\x38\x07\x8A\x48"
                            "\xA9\xA1\x48\xA9\x10\x48\xA9\x0C\xD0\x30\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xB9\x82\xC0\x29\xF1\x1D\xB8\x05\x99\x82\xC0\x68\x30\x11\xF0\x80"
                            "\x6A\xB0\x89\x90\xB4\xA9\x00\x9D\xB8\x05\xA9\x01\x48\xD0\xE1\x60"
                            "\xA9\xC0\x9D\xB8\x06\x8C\x22\x02\xA9\x0A\x9D\xB8\x05\xA9\x00\x48"
                            "\xF0\xCE\x68\x68\x68\x68\xA9\x05\x9D\x38\x06\xB9\x81\xC0\x29\xFB"
                            "\x99\x81\xC0\xA9\x00\x99\x80\xC0\xB9\x81\xC0\x09\x04\x99\x81\xC0"
                            "\xB9\x82\xC0\x0A\x10\xFA\xB9\x80\xC0\x48\xB9\x82\xC0\x09\x10\x99"
                            "\x82\xC0\xB9\x82\xC0\x0A\x30\xFA\xB9\x82\xC0\x29\xEF\x99\x82\xC0"
                            "\xDE\x38\x06\xD0\xDB\x68\x9D\xB8\x06\x68\x9D\x38\x05\x68\x9D\x38"
                            "\x04\x68\x9D\xB8\x04\x68\x9D\xB8\x03\x18\x90\x99\xFF\xFF\xFF\xC8"
                            "\x8A\x48\x48\x48\xA9\x12\x48\xBC\xB8\x03\xBD\xB8\x04\xAA\x98\xA0"
                            "\x05\xD0\x6D\xAE\xF8\x07\xA9\x24\x48\xBC\x38\x04\xBD\x38\x05\xAA"
                            "\x98\xA0\x0C\xD0\x5B\xAE\xF8\x07\xA9\x43\x48\xAD\x00\xC0\x0A\x08"
                            "\xBD\xB8\x06\x2A\x2A\x2A\x29\x03\x49\x03\x38\x69\x00\x28\xA2\x00"
                            "\xA0\x10\xD0\x4D\xA9\x8D\x8D\x11\x02\x48\xA9\x11\x48\x48\xA9\x00"
                            "\xF0\x12\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xAE\xF8\x07\xAC\x22\x02\x9D\xB8\x05\xA9\x01\x48"
                            "\xB9\x82\xC0\x29\xF1\x1D\xB8\x05\x99\x82\xC0\x68\x30\x4E\xF0\x80"
                            "\xE0\x80\x90\x0D\x49\xFF\x69\x00\x48\x8A\x49\xFF\x69\x00\xAA\x68"
                            "\x38\x8D\x21\x02\x8E\x20\x02\xA9\xAB\x90\x02\xA9\xAD\x48\xA9\xAC"
                            "\x99\x01\x02\xA2\x11\xA9\x00\x18\x2A\xC9\x0A\x90\x02\xE9\x0A\x2E"
                            "\x21\x02\x2E\x20\x02\xCA\xD0\xF0\x09\xB0\x99\x00\x02\x88\xF0\x08"
                            "\xC0\x07\xF0\x04\xC0\x0E\xD0\xDB\x68\x99\x00\x02\x60\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xCD"
                            "\xB8\x50\x13\xBD\x38\x07\x29\x01\xF0\x47\xA9\x10\x48\xA9\x05\x9D"
                            "\x38\x06\xA9\x7F\x69\x01\xB9\x82\xC0\x30\xFB\xB9\x81\xC0\x29\xFB"
                            "\x99\x81\xC0\xA9\xFF\x99\x80\xC0\xB9\x81\xC0\x09\x04\x99\x81\xC0"
                            "\x68\x99\x80\xC0\xB9\x82\xC0\x09\x20\x99\x82\xC0\xB9\x82\xC0\x10"
                            "\xFB\x29\xDF\x99\x82\xC0\x70\x3F\x70\x07\xBD\x38\x07\x4A\x4A\x4A"
                            "\x4A\xB8\x9D\xB8\x05\xF0\x02\xA9\x80\x48\x50\x14\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xB9\x82\xC0\x29\xF1\x1D\xB8\x05\x99\x82\xC0\x68\x30\x11\xF0\x80"
                            "\xC9\x02\xF0\x81\xD0\x02\xF0\xC2\xB8\xB9\x81\xC0\x29\xFB\x99\x81"
                            "\xC0\xA9\x00\x99\x80\xC0\xB9\x81\xC0\x09\x04\x99\x81\xC0\xB9\x82"
                            "\xC0\x0A\x10\xFA\xB9\x80\xC0\x70\x05\x9D\x38\x06\x50\x01\x48\xB9"
                            "\x82\xC0\x09\x10\x99\x82\xC0\xB9\x82\xC0\x0A\x30\xFA\xB9\x82\xC0"
                            "\x29\xEF\x99\x82\xC0\x50\x19\xDE\x38\x06\xD0\xD2\x68\x9D\xB8\x06"
                            "\x68\x9D\x38\x05\x68\x9D\x38\x04\x68\x9D\xB8\x04\x68\x9D\xB8\x03"
                            "\xA9\x00\xF0\xA2\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xC1\xBD\x38\x06\xC9\x40\xF0\x22\xC9\x60\xF0\x0D\xC9"
                            "\x61\xF0\x09\xC9\xA0\xD0\x2E\x48\xA9\x02\xD0\x45\xAD\xF8\x05\x48"
                            "\xAD\x78\x05\x48\xAD\xF8\x04\x48\xAD\x78\x04\xB0\x0F\xBD\x38\x05"
                            "\x48\xBD\x38\x04\x48\xBD\xB8\x04\x48\xBD\xB8\x03\x48\xBD\x38\x06"
                            "\x48\xA9\x05\xD0\x1C\x29\x0C\x4A\x4A\x4A\xB0\x3E\x4A\x90\x0C\xAD"
                            "\x78\x05\x48\xBD\x38\x06\x48\xA9\x02\xD0\x06\xBD\x38\x06\x48\xA9"
                            "\x01\x9D\x38\x06\xD0\x4F\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xFF\xB9\x82\xC0\x29\xF1\x1D\xB8\x05\x99\x82\xC0\x68"
                            "\xD0\x82\xA9\x00\x9D\xB8\x05\x48\xF0\xEA\x4A\xB0\x13\xAD\xF8\x04"
                            "\x48\xAD\x78\x04\x48\xBD\x38\x06\x48\xA9\x03\x9D\x38\x06\xD0\x15"
                            "\xAD\x78\x05\x48\xAD\xF8\x04\x48\xAD\x78\x04\x48\xBD\x38\x06\x48"
                            "\xA9\x04\x9D\x38\x06\xB9\x82\xC0\x30\xFB\xB9\x81\xC0\x29\xFB\x99"
                            "\x81\xC0\xA9\xFF\x99\x80\xC0\xB9\x81\xC0\x09\x04\x99\x81\xC0\x68"
                            "\x99\x80\xC0\xB9\x82\xC0\x09\x20\x99\x82\xC0\xB9\x82\xC0\x10\xFB"
                            "\x29\xDF\x99\x82\xC0\xDE\x38\x06\xF0\x98\xB9\x82\xC0\x30\xFB\x10"
                            "\xD6\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                            "\xFF\xFF\xFF\xCE";


static void Mouse_Reset();
static void Mouse_SetPositionInternal(int xvalue, int yvalue);
static void Mouse_ClampX(int iMinX, int iMaxX);
static void Mouse_ClampY(int iMinY, int iMaxY);
static void Mouse_OnMouseEvent();
static void Mouse_OnCommand();
static void Mouse_OnWrite();

static void M6821_Listener_A(void* objTo, uint8_t byData) {
  (void)objTo;
  sg_Mouse.m_by6821A = byData;
}

static void M6821_Listener_B(void* objTo, uint8_t byData) {
  (void)objTo;
  uint8_t byDiff = (sg_Mouse.m_by6821B ^ byData) & 0x3E;

  if (byDiff) {
    sg_Mouse.m_by6821B &= ~0x3E;
    sg_Mouse.m_by6821B |= byData & 0x3E;
    if (byDiff & BIT5)      // Write to 0285 chip
    {
      if (byData & BIT5) {
        sg_Mouse.m_by6821B |= BIT7;    // OK, I'm ready to read from MC6821
      }
      else {// Clock Activate for read
        sg_Mouse.m_byBuff[sg_Mouse.m_nBuffPos++] = sg_Mouse.m_by6821A;
        if (sg_Mouse.m_nBuffPos == 1) {
          Mouse_OnCommand();
        }
        if (sg_Mouse.m_nBuffPos == sg_Mouse.m_nDataLen || sg_Mouse.m_nBuffPos > 7) {
          Mouse_OnWrite();      // Have written all, Commit the command.
          sg_Mouse.m_nBuffPos = 0;
        }
        sg_Mouse.m_by6821B &= ~BIT7;    // for next reading
        Pia6821_SetPortB(&sg_Mouse.m_6821, sg_Mouse.m_by6821B);
      }
    }
    if (byDiff & BIT4) {// Read from 0285 chip ?
      if (byData & BIT4) {
        sg_Mouse.m_by6821B &= ~BIT6;    // OK, I'll prepare next value
      } else { // Clock Activate for write
        if (sg_Mouse.m_nBuffPos) { // if m_nBuffPos is 0, something goes wrong!
          sg_Mouse.m_nBuffPos++;
        }
        if (sg_Mouse.m_nBuffPos == sg_Mouse.m_nDataLen || sg_Mouse.m_nBuffPos > 7) {
          sg_Mouse.m_nBuffPos = 0; // Have read all, ready for next command.
        } else {
          Pia6821_SetPortA(&sg_Mouse.m_6821, sg_Mouse.m_byBuff[sg_Mouse.m_nBuffPos]);
        }
        sg_Mouse.m_by6821B |= BIT6;    // for next writing
      }
    }
    Pia6821_SetPortB(&sg_Mouse.m_6821, sg_Mouse.m_by6821B);

    Mouse_SetSlotRom();  // Update Cn00 ROM page
  }
}

#include "core/Peripheral.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static HostInterface_t* g_pMouseHost = nullptr;

void Mouse_SetSlotRom() {
  if (!g_pMouseHost) return;

  uint32_t uOffset = (sg_Mouse.m_by6821B << 7) & 0x0700;
  if (sg_Mouse.m_pSlotRom) {
    uint8_t slot_rom[256];
    memcpy(slot_rom, sg_Mouse.m_pSlotRom + uOffset, 256);
    g_pMouseHost->RegisterCxROM(sg_Mouse.m_uSlot, slot_rom);
  }
}

auto Mouse_IORead(void* instance, uint16_t PC, uint16_t uAddr, uint8_t bWrite, uint8_t uValue, uint32_t nCyclesLeft) -> uint8_t {
  (void)instance;
  (void)uValue;
  (void)nCyclesLeft;
  (void)PC;
  (void)bWrite;
  uint8_t byRS = uAddr & 3;
  return Pia6821_Read(&sg_Mouse.m_6821, byRS);
}

auto Mouse_IOWrite(void* instance, uint16_t PC, uint16_t uAddr, uint8_t bWrite, uint8_t uValue, uint32_t nCyclesLeft) -> uint8_t {
  (void)instance;
  (void)nCyclesLeft;
  (void)PC;
  (void)bWrite;
  uint8_t byRS = uAddr & 3;
  Pia6821_Write(&sg_Mouse.m_6821, byRS, uValue);
  return 0;
}

static auto Mouse_ABI_Init(int slot, HostInterface_t* host) -> void* {
  g_pMouseHost = host;
  const uint32_t FW_SIZE = 2 * 1024;
  auto *pData = reinterpret_cast<uint8_t *>(MouseInterface_rom);

  memset(&sg_Mouse, 0, sizeof(sg_Mouse));
  Pia6821_Reset(&sg_Mouse.m_6821);
  Pia6821_SetListenerB(&sg_Mouse.m_6821, nullptr, M6821_Listener_B);
  Pia6821_SetListenerA(&sg_Mouse.m_6821, nullptr, M6821_Listener_A);

  sg_Mouse.m_by6821A = 0;
  sg_Mouse.m_by6821B = 0x40;    // Set PB6
  Pia6821_SetPortB(&sg_Mouse.m_6821, sg_Mouse.m_by6821B);

  sg_Mouse.m_iMinX = 0;
  sg_Mouse.m_iMaxX = 1023;
  sg_Mouse.m_iMinY = 0;
  sg_Mouse.m_iMaxY = 1023;

  Mouse_Reset();

  sg_Mouse.m_uSlot = slot;
  if (sg_Mouse.m_pSlotRom == nullptr) {
    sg_Mouse.m_pSlotRom = new uint8_t[FW_SIZE];
    if (sg_Mouse.m_pSlotRom) {
      memcpy(sg_Mouse.m_pSlotRom, pData, FW_SIZE);
    }
  }

  Mouse_SetSlotRom();
  host->RegisterIO(slot, Mouse_IORead, Mouse_IOWrite, nullptr, nullptr);
  sg_Mouse.m_bActive = true;
  return reinterpret_cast<void*>(1);
}

static void Mouse_ABI_Reset(void* instance) {
  (void)instance;
  Mouse_Reset();
}

static void Mouse_ABI_Shutdown(void* instance) {
  (void)instance;
  sg_Mouse.m_bActive = false;
  if (sg_Mouse.m_pSlotRom) {
    delete[] sg_Mouse.m_pSlotRom;
    sg_Mouse.m_pSlotRom = nullptr;
  }
  g_pMouseHost = nullptr;
}

static void Mouse_ABI_OnVBlank(void* instance, bool vblank) {
  (void)instance;
  Mouse_SetVBlank(vblank);
}

Peripheral_t g_mouse_peripheral = {
    LINAPPLE_ABI_VERSION,
    "Mouse Interface",
    0xFE, // Slots 1-7
    Mouse_ABI_Init,
    Mouse_ABI_Reset,
    Mouse_ABI_Shutdown,
    nullptr, // think
    Mouse_ABI_OnVBlank,
    nullptr, // save_state
    nullptr, // load_state
    nullptr, // command
    nullptr  // query
};

#ifdef BUILD_SHARED_PERIPHERAL
EXPORT_PERIPHERAL(g_mouse_peripheral)
#endif
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

static void Mouse_OnCommand() {
  switch (sg_Mouse.m_byBuff[0] & 0xF0) {
    case MOUSE_SET:
      sg_Mouse.m_nDataLen = 1;
      sg_Mouse.m_byMode = sg_Mouse.m_byBuff[0] & 0x0F;
      break;
    case MOUSE_READ:
      sg_Mouse.m_nDataLen = 6;
      sg_Mouse.m_byState &= 0x20;
      sg_Mouse.m_nX = sg_Mouse.m_iX;
      sg_Mouse.m_nY = sg_Mouse.m_iY;
      if (sg_Mouse.m_bBtn0) {
        sg_Mouse.m_byState |= 0x40;      // Previous Button 0
      }
      if (sg_Mouse.m_bBtn1) {
        sg_Mouse.m_byState |= 0x01;      // Previous Button 1
      }
      sg_Mouse.m_bBtn0 = sg_Mouse.m_bButtons[0];
      sg_Mouse.m_bBtn1 = sg_Mouse.m_bButtons[1];
      if (sg_Mouse.m_bBtn0) {
        sg_Mouse.m_byState |= 0x80;      // Current Button 0
      }
      if (sg_Mouse.m_bBtn1) {
        sg_Mouse.m_byState |= 0x10;      // Current Button 1
      }
      sg_Mouse.m_byBuff[1] = sg_Mouse.m_nX & 0xFF;
      sg_Mouse.m_byBuff[2] = (sg_Mouse.m_nX >> 8) & 0xFF;
      sg_Mouse.m_byBuff[3] = sg_Mouse.m_nY & 0xFF;
      sg_Mouse.m_byBuff[4] = (sg_Mouse.m_nY >> 8) & 0xFF;
      sg_Mouse.m_byBuff[5] = sg_Mouse.m_byState;      // button 0/1 interrupt status
      sg_Mouse.m_byState &= ~0x20;
      break;
    case MOUSE_SERV:
      sg_Mouse.m_nDataLen = 2;
      sg_Mouse.m_byBuff[1] = sg_Mouse.m_byState & ~0x20;      // reason of interrupt
      CpuIrqDeassert(IS_MOUSE);
      break;
    case MOUSE_CLEAR:
      Mouse_Reset();
      sg_Mouse.m_nDataLen = 1;
      break;
    case MOUSE_POS:
      sg_Mouse.m_nDataLen = 5;
      break;
    case MOUSE_INIT:
      sg_Mouse.m_nDataLen = 3;
      sg_Mouse.m_byBuff[1] = 0xFF;      // I don't know what it is
      break;
    case MOUSE_CLAMP:
      sg_Mouse.m_nDataLen = 5;
      break;
    case MOUSE_HOME:
      sg_Mouse.m_nDataLen = 1;
      Mouse_SetPositionInternal(0, 0);
      break;
    case MOUSE_TIME:    // 0x90
      switch (sg_Mouse.m_byBuff[0] & 0x0C) {
        case 0x00:
          sg_Mouse.m_nDataLen = 1;
          break;  // write cmd ( #$90 is DATATIME 60Hz, #$91 is 50Hz )
        case 0x04:
          sg_Mouse.m_nDataLen = 3;
          break;  // write cmd, $0478, $04F8
        case 0x08:
          sg_Mouse.m_nDataLen = 2;
          break;  // write cmd, $0578
        case 0x0C:
          sg_Mouse.m_nDataLen = 4;
          break;  // write cmd, $0478, $04F8, $0578
      }
      break;
    case 0xA0:
      sg_Mouse.m_nDataLen = 2;
      break;
    case 0xB0:
    case 0xC0:
      sg_Mouse.m_nDataLen = 1;
      break;
    default:
      sg_Mouse.m_nDataLen = 1;
      break;
  }
  Pia6821_SetPortA(&sg_Mouse.m_6821, sg_Mouse.m_byBuff[1]);
}

static void Mouse_OnWrite() {
  int nMin = 0, nMax = 0;
  switch (sg_Mouse.m_byBuff[0] & 0xF0) {
    case MOUSE_CLAMP:
      nMin = (sg_Mouse.m_byBuff[3] << 8) | sg_Mouse.m_byBuff[1];
      nMax = (sg_Mouse.m_byBuff[4] << 8) | sg_Mouse.m_byBuff[2];
      if (sg_Mouse.m_byBuff[0] & 1) {  // Clamp Y
        Mouse_ClampY(nMin, nMax);
      } else {          // Clamp X
        Mouse_ClampX(nMin, nMax);
}
      break;
    case MOUSE_POS:
      sg_Mouse.m_nX = (sg_Mouse.m_byBuff[2] << 8) | sg_Mouse.m_byBuff[1];
      sg_Mouse.m_nY = (sg_Mouse.m_byBuff[4] << 8) | sg_Mouse.m_byBuff[3];
      Mouse_SetPositionInternal(sg_Mouse.m_nX, sg_Mouse.m_nY);
      break;
    case MOUSE_INIT:
      sg_Mouse.m_nX = 0;
      sg_Mouse.m_nY = 0;
      Mouse_ClampX(0, 1023);
      Mouse_ClampY(0, 1023);
      Mouse_SetPositionInternal(0, 0);
      break;
  }
}

static void Mouse_OnMouseEvent() {
  int byState = 0;
  if (!(sg_Mouse.m_byMode & 1)) { // Mouse Off
    return;
  }

  bool bBtn0 = sg_Mouse.m_bButtons[0];
  bool bBtn1 = sg_Mouse.m_bButtons[1];
  if (static_cast<uint32_t>(sg_Mouse.m_nX) != sg_Mouse.m_iX || static_cast<uint32_t>(sg_Mouse.m_nY) != sg_Mouse.m_iY) {
    byState |= 0x22;        // X/Y moved since last READMOUSE | Movement interrupt
  }
  if (sg_Mouse.m_bBtn0 != bBtn0 || sg_Mouse.m_bBtn1 != bBtn1) {
    byState |= 0x04;        // Button 0/1 interrupt
  }
  if (sg_Mouse.m_bVBL) {
    byState |= 0x08;
  }
  byState &= ((sg_Mouse.m_byMode & 0x0E) |
              0x20);  // Keep "X/Y moved since last READMOUSE" for next MOUSE_READ (Contiki v1.3 uses this)
  if (byState & 0x0E) {
    sg_Mouse.m_byState |= byState;
    CpuIrqAssert(IS_MOUSE);
  }
}

void Mouse_SetVBlank(bool bVBL) {
  if (sg_Mouse.m_bVBL != bVBL) {
    sg_Mouse.m_bVBL = bVBL;
    if (sg_Mouse.m_bVBL) { // Rising edge
      Mouse_OnMouseEvent();
    }
  }
}

static void Mouse_Reset() {
  sg_Mouse.m_nBuffPos = 0;
  sg_Mouse.m_nDataLen = 1;

  sg_Mouse.m_byMode = 0;
  sg_Mouse.m_byState = 0;
  sg_Mouse.m_nX = 0;
  sg_Mouse.m_nY = 0;
  sg_Mouse.m_bBtn0 = false;
  sg_Mouse.m_bBtn1 = false;
  Mouse_ClampX(0, 1023);
  Mouse_ClampY(0, 1023);
  Mouse_SetPositionInternal(0, 0);
}

static void Mouse_ClampX(int iMinX, int iMaxX) {
  if (iMinX < 0 || iMinX > iMaxX) {
    return;
  }
  sg_Mouse.m_iMaxX = iMaxX;
  sg_Mouse.m_iMinX = iMinX;
  if (sg_Mouse.m_iX > sg_Mouse.m_iMaxX) {
    sg_Mouse.m_iX = sg_Mouse.m_iMaxX;
  } else if (sg_Mouse.m_iX < sg_Mouse.m_iMinX) {
    sg_Mouse.m_iX = sg_Mouse.m_iMinX;
  }
}

static void Mouse_ClampY(int iMinY, int iMaxY) {
  if (iMinY < 0 || iMinY > iMaxY) {
    return;
  }
  sg_Mouse.m_iMaxY = iMaxY;
  sg_Mouse.m_iMinY = iMinY;
  if (sg_Mouse.m_iY > sg_Mouse.m_iMaxY) {
    sg_Mouse.m_iY = sg_Mouse.m_iMaxY;
  } else if (sg_Mouse.m_iY < sg_Mouse.m_iMinX) {
    sg_Mouse.m_iY = sg_Mouse.m_iMinY;
  }
}

static void Mouse_SetPositionInternal(int xvalue, int yvalue) {
  if ((sg_Mouse.m_iRangeX == 0) || (sg_Mouse.m_iRangeY == 0)) {
    sg_Mouse.m_nX = sg_Mouse.m_iX = sg_Mouse.m_iMinX;
    sg_Mouse.m_nY = sg_Mouse.m_iY = sg_Mouse.m_iMinY;
    return;
  }

  sg_Mouse.m_iX = ((xvalue * sg_Mouse.m_iMaxX) / sg_Mouse.m_iRangeX);
  sg_Mouse.m_iY = ((yvalue * sg_Mouse.m_iMaxY) / sg_Mouse.m_iRangeY);
}

void Mouse_SetPosition(int xvalue, int xrange, int yvalue, int yrange) {
  sg_Mouse.m_iRangeX = static_cast<uint32_t>(xrange);
  sg_Mouse.m_iRangeY = static_cast<uint32_t>(yrange);

  Mouse_SetPositionInternal(xvalue, yvalue);
  Mouse_OnMouseEvent();
}

void Mouse_SetButton(eBUTTON Button, eBUTTONSTATE State) {
  sg_Mouse.m_bButtons[Button] = State == BUTTON_DOWN;
  Mouse_OnMouseEvent();
}

auto Mouse_Active() -> bool {
  return sg_Mouse.m_bActive;
}
