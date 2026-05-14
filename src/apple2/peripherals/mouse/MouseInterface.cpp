// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables) Justification: This file
// implements the C11-compatible Peripheral ABI. It requires void* pointers for
// instance state, raw memory management, and instance state to bridge with
// the core C interface

#include "apple2/peripherals/mouse/MouseInterface.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Structs.h"
#include "apple2/Video.h"
#include "apple2/chips/6821.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/Log.h"
#include "core/Peripheral.h"

// Sets mouse mode
enum {
  MOUSE_SET = 0x00,
  MOUSE_READ = 0x10,
  MOUSE_SERV = 0x20,
  MOUSE_CLEAR = 0x30,
  MOUSE_POS = 0x40,
  MOUSE_INIT = 0x50,
  MOUSE_CLAMP = 0x60,
  MOUSE_HOME = 0x70
};

// Set VBL Timing : 0x90 is 60Hz, 0x91 is 50Hz
enum { MOUSE_TIME = 0x90 };

enum {
  BIT0 = 0x01,
  BIT1 = 0x02,
  BIT2 = 0x04,
  BIT3 = 0x08,
  BIT4 = 0x10,
  BIT5 = 0x20,
  BIT6 = 0x40,
  BIT7 = 0x80
};

static constexpr uint8_t MOUSE_CMD_MASK = 0xF0;
static constexpr uint8_t MOUSE_MODE_MASK = 0x0F;
static constexpr uint8_t MOUSE_PB_DATA_MASK = 0x3E;

enum MouseStatus_e {
  MOUSE_STAT_PREV_BTN1 = BIT0,
  MOUSE_STAT_MOVE_INT = BIT1,
  MOUSE_STAT_BTN_INT = BIT2,
  MOUSE_STAT_VBL_INT = BIT3,
  MOUSE_STAT_CURR_BTN1 = BIT4,
  MOUSE_STAT_MOVEMENT = BIT5,
  MOUSE_STAT_PREV_BTN0 = BIT6,
  MOUSE_STAT_CURR_BTN0 = BIT7
};

static constexpr size_t MOUSE_ROM_PAGE_SIZE = 256;
static constexpr uint32_t MOUSE_DEFAULT_MAX_COORD = 1023;

struct MousePeripheral_t {
  MouseInterface logic{};
  HostInterface_t* host = nullptr;
  int slot = 0;
};

// Legacy global (will be removed)

static const uint8_t MouseInterface_rom[] =
    "\x2C\x58\xFF\x70\x1B\x38\x90\x18\xB8\x50\x15\x01\x20\xF4\xF4\xF4"
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

static void Mouse_Reset_Internal(MousePeripheral_t* mp);
static void Mouse_SetPositionInternal(MousePeripheral_t* mp, int xvalue,
                                      int yvalue);
static void Mouse_ClampX(MousePeripheral_t* mp, int iMinX, int iMaxX);
static void Mouse_ClampY(MousePeripheral_t* mp, int iMinY, int iMaxY);
static void Mouse_OnMouseEvent(MousePeripheral_t* mp);
static void Mouse_OnCommand(MousePeripheral_t* mp);
static void Mouse_OnWrite(MousePeripheral_t* mp);

static void Mouse_SetSlotRom_Instance(MousePeripheral_t* mp) {
  if (!mp || !mp->host) return;

  uint32_t uOffset = (static_cast<uint32_t>(mp->logic.m_by6821B) << 7) & 0x0700;
  if (mp->logic.m_pSlotRom) {
    uint8_t slot_rom[MOUSE_ROM_PAGE_SIZE];
    memcpy(slot_rom, mp->logic.m_pSlotRom + uOffset, MOUSE_ROM_PAGE_SIZE);
    mp->host->RegisterCxROM(mp->slot, slot_rom);
  }
}

static void M6821_Listener_A(void* objTo, uint8_t byData) {
  auto* mp = static_cast<MousePeripheral_t*>(objTo);
  mp->logic.m_by6821A = byData;
}

static void M6821_Listener_B(void* objTo, uint8_t byData) {
  auto* mp = static_cast<MousePeripheral_t*>(objTo);
  uint8_t byDiff = (mp->logic.m_by6821B ^ byData) & MOUSE_PB_DATA_MASK;

  if (byDiff != 0) {
    mp->logic.m_by6821B &= ~MOUSE_PB_DATA_MASK;
    mp->logic.m_by6821B |= byData & MOUSE_PB_DATA_MASK;
    if ((byDiff & BIT5) != 0)  // Write to 0285 chip
    {
      if ((byData & BIT5) != 0) {
        mp->logic.m_by6821B |= BIT7;  // OK, I'm ready to read from MC6821
      } else {                        // Clock Activate for read
        mp->logic.m_byBuff[mp->logic.m_nBuffPos++] = mp->logic.m_by6821A;
        if (mp->logic.m_nBuffPos == 1) {
          Mouse_OnCommand(mp);
        }
        if (mp->logic.m_nBuffPos == mp->logic.m_nDataLen ||
            mp->logic.m_nBuffPos > 7) {
          Mouse_OnWrite(mp);  // Have written all, Commit the command.
          mp->logic.m_nBuffPos = 0;
        }
        mp->logic.m_by6821B &= ~BIT7;  // for next reading
        Pia6821_SetPortB(&mp->logic.m_6821, mp->logic.m_by6821B);
      }
    }
    if ((byDiff & BIT4) != 0) {  // Read from 0285 chip ?
      if ((byData & BIT4) != 0) {
        mp->logic.m_by6821B &= ~BIT6;  // OK, I'll prepare next value
      } else {                         // Clock Activate for write
        if (mp->logic
                .m_nBuffPos != 0) {  // if m_nBuffPos is 0, something goes wrong!
          mp->logic.m_nBuffPos++;
        }
        if (mp->logic.m_nBuffPos == mp->logic.m_nDataLen ||
            mp->logic.m_nBuffPos > 7) {
          mp->logic.m_nBuffPos = 0;  // Have read all, ready for next command.
        } else {
          Pia6821_SetPortA(&mp->logic.m_6821,
                           mp->logic.m_byBuff[mp->logic.m_nBuffPos]);
        }
        mp->logic.m_by6821B |= BIT6;  // for next writing
      }
    }
    Pia6821_SetPortB(&mp->logic.m_6821, mp->logic.m_by6821B);

    Mouse_SetSlotRom_Instance(mp);  // Update Cn00 ROM page
  }
}

static auto Mouse_IORead(void* instance, uint16_t PC, uint16_t uAddr,
                         uint8_t bWrite, uint8_t uValue, uint32_t nCyclesLeft)
    -> uint8_t {
  (void)PC;
  (void)bWrite;
  (void)uValue;
  (void)nCyclesLeft;
  if (!instance) return MemReadFloatingBus(nCyclesLeft);
  auto* mp = static_cast<MousePeripheral_t*>(instance);
  uint8_t byRS = static_cast<uint8_t>(uAddr & 3);
  return Pia6821_Read(&mp->logic.m_6821, byRS);
}

static auto Mouse_IOWrite(void* instance, uint16_t PC, uint16_t uAddr,
                          uint8_t bWrite, uint8_t uValue, uint32_t nCyclesLeft)
    -> uint8_t {
  (void)PC;
  (void)bWrite;
  (void)nCyclesLeft;
  if (!instance) return 0;
  auto* mp = static_cast<MousePeripheral_t*>(instance);
  uint8_t byRS = static_cast<uint8_t>(uAddr & 3);
  Pia6821_Write(&mp->logic.m_6821, byRS, uValue);
  return 0;
}

static auto Mouse_ABI_Init(int slot, HostInterface_t* host) -> void* {
  auto* mp = new MousePeripheral_t{};
  mp->host = host;
  mp->slot = slot;

  static constexpr uint32_t FW_SIZE = 2048;
  const uint8_t* pData = MouseInterface_rom;

  Pia6821_Reset(&mp->logic.m_6821);
  Pia6821_SetListenerB(&mp->logic.m_6821, mp, M6821_Listener_B);
  Pia6821_SetListenerA(&mp->logic.m_6821, mp, M6821_Listener_A);

  mp->logic.m_by6821A = 0;
  mp->logic.m_by6821B = BIT6;  // Set PB6
  Pia6821_SetPortB(&mp->logic.m_6821, mp->logic.m_by6821B);

  mp->logic.m_iMinX = 0;
  mp->logic.m_iMaxX = MOUSE_DEFAULT_MAX_COORD;
  mp->logic.m_iMinY = 0;
  mp->logic.m_iMaxY = MOUSE_DEFAULT_MAX_COORD;

  Mouse_Reset_Internal(mp);

  mp->logic.m_uSlot = static_cast<uint32_t>(slot);
  mp->logic.m_pSlotRom = new uint8_t[FW_SIZE];
  memcpy(mp->logic.m_pSlotRom, pData, FW_SIZE);

  Mouse_SetSlotRom_Instance(mp);
  host->RegisterIO(slot, Mouse_IORead, Mouse_IOWrite, nullptr, nullptr);
  mp->logic.m_bActive = true;
  return mp;
}

static void Mouse_ABI_Reset(void* instance) {
  if (!instance) return;
  auto* mp = static_cast<MousePeripheral_t*>(instance);
  Mouse_Reset_Internal(mp);
}

static void Mouse_ABI_Shutdown(void* instance) {
  if (!instance) return;
  auto* mp = static_cast<MousePeripheral_t*>(instance);
  mp->logic.m_bActive = false;
  if (mp->logic.m_pSlotRom) {
    delete[] mp->logic.m_pSlotRom;
    mp->logic.m_pSlotRom = nullptr;
  }
  delete mp;
}

static void Mouse_ABI_OnVBlank(void* instance, bool vblank) {
  if (!instance) return;
  auto* mp = static_cast<MousePeripheral_t*>(instance);
  if (mp->logic.m_bVBL != vblank) {
    mp->logic.m_bVBL = vblank;
    if (mp->logic.m_bVBL) {  // Rising edge
      Mouse_OnMouseEvent(mp);
    }
  }
}

static auto Mouse_ABI_Command(void* instance, uint32_t cmd_id, const void* data,
                              size_t size) -> PeripheralStatus {
  if (!instance || !data) return PERIPHERAL_ERROR;
  auto* mp = static_cast<MousePeripheral_t*>(instance);

  switch (static_cast<MouseCmd_e>(cmd_id)) {
    case MOUSE_CMD_SET_POS: {
      if (size < sizeof(MousePosPayload_t)) return PERIPHERAL_ERROR;
      const auto* p = static_cast<const MousePosPayload_t*>(data);
      mp->logic.m_iRangeX = static_cast<uint32_t>(p->x_range);
      mp->logic.m_iRangeY = static_cast<uint32_t>(p->y_range);
      Mouse_SetPositionInternal(mp, p->x, p->y);
      Mouse_OnMouseEvent(mp);
      return PERIPHERAL_OK;
    }
    case MOUSE_CMD_SET_BUTTON: {
      if (size < sizeof(MouseButtonPayload_t)) return PERIPHERAL_ERROR;
      const auto* p = static_cast<const MouseButtonPayload_t*>(data);
      if (p->button < 2) {
        mp->logic.m_bButtons[p->button] = p->down;
        Mouse_OnMouseEvent(mp);
      }
      return PERIPHERAL_OK;
    }
  }
  return PERIPHERAL_ERROR;
}

static auto Mouse_ABI_Query(void* instance, uint32_t query_id, void* out,
                            size_t* out_size) -> PeripheralStatus {
  if (!instance || !out || !out_size) return PERIPHERAL_ERROR;
  auto* mp = static_cast<MousePeripheral_t*>(instance);

  switch (static_cast<MouseQuery_e>(query_id)) {
    case MOUSE_QUERY_IS_ACTIVE: {
      if (*out_size < 1) return PERIPHERAL_ERROR;
      *static_cast<uint8_t*>(out) = mp->logic.m_bActive ? 1 : 0;
      *out_size = 1;
      return PERIPHERAL_OK;
    }
  }
  return PERIPHERAL_ERROR;
}

Peripheral_t g_mouse_peripheral = {LINAPPLE_ABI_VERSION,
                                   "linapple.mouse",
                                   "Mouse Interface",
                                   "Apple II Mouse Card emulation",
                                   "LinApple Contributors",
                                   VERSIONSTRING,
                                   0xFE,  // Slots 1-7
                                   4,     // Default Slot 4
                                   Mouse_ABI_Init,
                                   Mouse_ABI_Reset,
                                   Mouse_ABI_Shutdown,
                                   nullptr,  // think
                                   Mouse_ABI_OnVBlank,
                                   nullptr,  // save_state
                                   nullptr,  // load_state
                                   Mouse_ABI_Command,
                                   Mouse_ABI_Query};

static void Mouse_OnCommand(MousePeripheral_t* mp) {
  switch (mp->logic.m_byBuff[0] & MOUSE_CMD_MASK) {
    case MOUSE_SET:
      mp->logic.m_nDataLen = 1;
      mp->logic.m_byMode = mp->logic.m_byBuff[0] & MOUSE_MODE_MASK;
      break;
    case MOUSE_READ:
      mp->logic.m_nDataLen = 6;
      mp->logic.m_byState &= MOUSE_STAT_MOVEMENT;
      mp->logic.m_nX = static_cast<int>(mp->logic.m_iX);
      mp->logic.m_nY = static_cast<int>(mp->logic.m_iY);
      if (mp->logic.m_bBtn0) {
        mp->logic.m_byState |= MOUSE_STAT_PREV_BTN0;
      }
      if (mp->logic.m_bBtn1) {
        mp->logic.m_byState |= MOUSE_STAT_PREV_BTN1;
      }
      mp->logic.m_bBtn0 = mp->logic.m_bButtons[0];
      mp->logic.m_bBtn1 = mp->logic.m_bButtons[1];
      if (mp->logic.m_bBtn0) {
        mp->logic.m_byState |= MOUSE_STAT_CURR_BTN0;
      }
      if (mp->logic.m_bBtn1) {
        mp->logic.m_byState |= MOUSE_STAT_CURR_BTN1;
      }
      mp->logic.m_byBuff[1] = static_cast<uint8_t>(mp->logic.m_nX & 0xFF);
      mp->logic.m_byBuff[2] = static_cast<uint8_t>((mp->logic.m_nX >> 8) & 0xFF);
      mp->logic.m_byBuff[3] = static_cast<uint8_t>(mp->logic.m_nY & 0xFF);
      mp->logic.m_byBuff[4] = static_cast<uint8_t>((mp->logic.m_nY >> 8) & 0xFF);
      mp->logic.m_byBuff[5] =
          mp->logic.m_byState;  // button 0/1 interrupt status
      mp->logic.m_byState &= ~MOUSE_STAT_MOVEMENT;
      break;
    case MOUSE_SERV:
      mp->logic.m_nDataLen = 2;
      mp->logic.m_byBuff[1] =
          mp->logic.m_byState & ~MOUSE_STAT_MOVEMENT;  // reason of interrupt
      if (mp->host && mp->host->AssertIrq)
        mp->host->AssertIrq(mp->slot, false);
      else
        CpuIrqDeassert(IS_MOUSE);
      break;
    case MOUSE_CLEAR:
      Mouse_Reset_Internal(mp);
      mp->logic.m_nDataLen = 1;
      break;
    case MOUSE_POS:
      mp->logic.m_nDataLen = 5;
      break;
    case MOUSE_INIT:
      mp->logic.m_nDataLen = 3;
      mp->logic.m_byBuff[1] = 0xFF;
      break;
    case MOUSE_CLAMP:
      mp->logic.m_nDataLen = 5;
      break;
    case MOUSE_HOME:
      mp->logic.m_nDataLen = 1;
      Mouse_SetPositionInternal(mp, 0, 0);
      break;
    case MOUSE_TIME:  // 0x90
      switch (mp->logic.m_byBuff[0] & 0x0C) {
        case 0x00:
          mp->logic.m_nDataLen = 1;
          break;
        case 0x04:
          mp->logic.m_nDataLen = 3;
          break;
        case 0x08:
          mp->logic.m_nDataLen = 2;
          break;
        case 0x0C:
          mp->logic.m_nDataLen = 4;
          break;
        default:
          break;
      }
      break;
    default:
      mp->logic.m_nDataLen = 1;
      break;
  }
  Pia6821_SetPortA(&mp->logic.m_6821, mp->logic.m_byBuff[1]);
}

static void Mouse_OnWrite(MousePeripheral_t* mp) {
  int nMin = 0;
  int nMax = 0;
  switch (mp->logic.m_byBuff[0] & MOUSE_CMD_MASK) {
    case MOUSE_CLAMP:
      nMin = (mp->logic.m_byBuff[3] << 8) | mp->logic.m_byBuff[1];
      nMax = (mp->logic.m_byBuff[4] << 8) | mp->logic.m_byBuff[2];
      if ((mp->logic.m_byBuff[0] & 1) != 0) {  // Clamp Y
        Mouse_ClampY(mp, nMin, nMax);
      } else {  // Clamp X
        Mouse_ClampX(mp, nMin, nMax);
      }
      break;
    case MOUSE_POS:
      mp->logic.m_nX = (mp->logic.m_byBuff[2] << 8) | mp->logic.m_byBuff[1];
      mp->logic.m_nY = (mp->logic.m_byBuff[4] << 8) | mp->logic.m_byBuff[3];
      Mouse_SetPositionInternal(mp, mp->logic.m_nX, mp->logic.m_nY);
      break;
    case MOUSE_INIT:
      mp->logic.m_nX = 0;
      mp->logic.m_nY = 0;
      Mouse_ClampX(mp, 0, static_cast<int>(MOUSE_DEFAULT_MAX_COORD));
      Mouse_ClampY(mp, 0, static_cast<int>(MOUSE_DEFAULT_MAX_COORD));
      Mouse_SetPositionInternal(mp, 0, 0);
      break;
    default:
      break;
  }
}

static void Mouse_OnMouseEvent(MousePeripheral_t* mp) {
  uint8_t byState = 0;
  if ((mp->logic.m_byMode & 1) == 0) {  // Mouse Off
    return;
  }

  bool bBtn0 = mp->logic.m_bButtons[0];
  bool bBtn1 = mp->logic.m_bButtons[1];
  if (static_cast<uint32_t>(mp->logic.m_nX) != mp->logic.m_iX ||
      static_cast<uint32_t>(mp->logic.m_nY) != mp->logic.m_iY) {
    byState |= (MOUSE_STAT_MOVEMENT | MOUSE_STAT_MOVE_INT);
  }
  if (mp->logic.m_bBtn0 != bBtn0 || mp->logic.m_bBtn1 != bBtn1) {
    byState |= MOUSE_STAT_BTN_INT;
  }
  if (mp->logic.m_bVBL) {
    byState |= MOUSE_STAT_VBL_INT;
  }
  byState &= ((mp->logic.m_byMode & 0x0E) | MOUSE_STAT_MOVEMENT);

  if ((byState & 0x0E) != 0) {
    mp->logic.m_byState |= byState;
    if (mp->host && mp->host->AssertIrq)
      mp->host->AssertIrq(mp->slot, true);
    else
      CpuIrqAssert(IS_MOUSE);
  }
}

static void Mouse_Reset_Internal(MousePeripheral_t* mp) {
  mp->logic.m_nBuffPos = 0;
  mp->logic.m_nDataLen = 1;

  mp->logic.m_byMode = 0;
  mp->logic.m_byState = 0;
  mp->logic.m_nX = 0;
  mp->logic.m_nY = 0;
  mp->logic.m_bBtn0 = false;
  mp->logic.m_bBtn1 = false;
  Mouse_ClampX(mp, 0, static_cast<int>(MOUSE_DEFAULT_MAX_COORD));
  Mouse_ClampY(mp, 0, static_cast<int>(MOUSE_DEFAULT_MAX_COORD));
  Mouse_SetPositionInternal(mp, 0, 0);
}

static void Mouse_ClampX(MousePeripheral_t* mp, int iMinX, int iMaxX) {
  if (iMinX < 0 || iMinX > iMaxX) return;
  mp->logic.m_iMaxX = static_cast<uint32_t>(iMaxX);
  mp->logic.m_iMinX = static_cast<uint32_t>(iMinX);
  if (mp->logic.m_iX > mp->logic.m_iMaxX)
    mp->logic.m_iX = mp->logic.m_iMaxX;
  else if (mp->logic.m_iX < mp->logic.m_iMinX)
    mp->logic.m_iX = mp->logic.m_iMinX;
}

static void Mouse_ClampY(MousePeripheral_t* mp, int iMinY, int iMaxY) {
  if (iMinY < 0 || iMinY > iMaxY) return;
  mp->logic.m_iMaxY = static_cast<uint32_t>(iMaxY);
  mp->logic.m_iMinY = static_cast<uint32_t>(iMinY);
  if (mp->logic.m_iY > mp->logic.m_iMaxY)
    mp->logic.m_iY = mp->logic.m_iMaxY;
  else if (mp->logic.m_iY < mp->logic.m_iMinY)
    mp->logic.m_iY = mp->logic.m_iMinY;
}

static void Mouse_SetPositionInternal(MousePeripheral_t* mp, int xvalue,
                                      int yvalue) {
  if ((mp->logic.m_iRangeX == 0) || (mp->logic.m_iRangeY == 0)) {
    mp->logic.m_nX = static_cast<int>(mp->logic.m_iX = mp->logic.m_iMinX);
    mp->logic.m_nY = static_cast<int>(mp->logic.m_iY = mp->logic.m_iMinY);
    return;
  }

  mp->logic.m_iX =
      (static_cast<uint32_t>(xvalue) * mp->logic.m_iMaxX) / mp->logic.m_iRangeX;
  mp->logic.m_iY =
      (static_cast<uint32_t>(yvalue) * mp->logic.m_iMaxY) / mp->logic.m_iRangeY;
}

PERIPHERAL_REGISTER(g_mouse_peripheral)

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables)
