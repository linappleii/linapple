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

#include "apple2/Disk.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/CPU.h"
#include "apple2/DiskCommands.h"
#include "apple2/DiskFTP.h"
#include "apple2/DiskGCR.h"
#include "apple2/DiskLoader.h"
#include "apple2/Memory.h"
#include "apple2/Structs.h"
#include "apple2/Video.h"
#include "apple2/formats/DoDriver.h"
#include "apple2/formats/IieDriver.h"
#include "apple2/formats/Nb2Driver.h"
#include "apple2/formats/NibDriver.h"
#include "apple2/formats/PoDriver.h"
#include "apple2/formats/Woz2Driver.h"
#include "apple2/ftpparse.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

static HostInterface_t* g_pDiskHost = nullptr;
static int g_nDiskSlot = 0;

static void CheckSpinning();
static auto IsDriveValid(const int iDrive) -> bool;
static void ReadTrack(int drive);
static void RemoveDisk(int drive);
static void WriteTrack(int drive);

static DiskError_e DiskInsert_Internal(int drive, const char* imageFileName,
                                       bool writeProtected,
                                       bool createIfNecessary);

static auto Disk_ABI_Command(void* instance, uint32_t cmd, const void* data,
                             size_t size) -> PeripheralStatus;

static auto Disk_ABI_Query(void* instance, uint32_t cmd, void* data,
                           size_t* size) -> PeripheralStatus;

static auto Disk_ABI_SaveState(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus;

static auto Disk_ABI_LoadState(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus;

constexpr int DISK_STATE_VERSION = 1;

#pragma pack(push, 1)
struct DiskStateHeader_t {
  uint32_t version; /* DISK_STATE_VERSION */
  uint32_t size;    /* sizeof(DiskSavedState_t) */
};

struct DiskDriveState_t {
  char fullname[MAX_DISK_FULL_NAME + 1];
  int32_t track;
  int32_t phase;
  int32_t byte_pos;
  uint8_t user_write_protected;
  uint8_t trackimagedata;
  uint8_t trackimagedirty;
  uint32_t spinning;
  uint32_t writelight;
  int32_t nibbles;
  uint8_t nTrack[NIBBLES_PER_TRACK];
};

struct DiskSavedState_t {
  DiskStateHeader_t header;
  DiskDriveState_t drives[2];
  uint16_t phases;
  uint16_t currdrive;
  uint8_t diskaccessed;
  uint8_t enhancedisk;
  uint8_t floppylatch;
  uint8_t floppymotoron;
  uint8_t floppywritemode;
};
#pragma pack(pop)

static const char Disk2_rom[] =
    "\xA2\x20\xA0\x00\xA2\x03\x86\x3C\x8A\x0A\x24\x3C\xF0\x10\x05\x3C"
    "\x49\xFF\x29\x7E\xB0\x08\x4A\xD0\xFB\x98\x9D\x56\x03\xC8\xE8\x10"
    "\xE5\x20\x58\xFF\xBA\xBD\x00\x01\x0A\x0A\x0A\x0A\x85\x2B\xAA\xBD"
    "\x8E\xC0\xBD\x8C\xC0\xBD\x8A\xC0\xBD\x89\xC0\xA0\x50\xBD\x80\xC0"
    "\x98\x29\x03\x0A\x05\x2B\xAA\xBD\x81\xC0\xA9\x56\x20\xA8\xFC\x88"
    "\x10\xEB\x85\x26\x85\x3D\x85\x41\xA9\x08\x85\x27\x18\x08\xBD\x8C"
    "\xC0\x10\xFB\x49\xD5\xD0\xF7\xBD\x8C\xC0\x10\xFB\xC9\xAA\xD0\xF3"
    "\xEA\xBD\x8C\xC0\x10\xFB\xC9\x96\xF0\x09\x28\x90\xDF\x49\xAD\xF0"
    "\x25\xD0\xD9\xA0\x03\x85\x40\xBD\x8C\xC0\x10\xFB\x2A\x85\x3C\xBD"
    "\x8C\xC0\x10\xFB\x25\x3C\x88\xD0\xEC\x28\xC5\x3D\xD0\xBE\xA5\x40"
    "\xC5\x41\xD0\xB8\xB0\xB7\xA0\x56\x84\x3C\xBC\x8C\xC0\x10\xFB\x59"
    "\xD6\x02\xA4\x3C\x88\x99\x00\x03\xD0\xEE\x84\x3C\xBC\x8C\xC0\x10"
    "\xFB\x59\xD6\x02\xA4\x3C\x91\x26\xC8\xD0\xEF\xBC\x8C\xC0\x10\xFB"
    "\x59\xD6\x02\xD0\x87\xA0\x00\xA2\x56\xCA\x30\xFB\xB1\x26\x5E\x00"
    "\x03\x2A\x5E\x00\x03\x2A\x91\x26\xC8\xD0\xEE\xE6\x27\xE6\x3D\xA5"
    "\x3D\xCD\x00\x08\xA6\x2B\x90\xDB\x4C\x01\x08\x00\x00\x00\x00\x00";

static auto DiskControlMotor(uint16_t pc, uint16_t addr, uint8_t bWrite,
                             uint8_t d, uint32_t nCyclesLeft) -> uint8_t;

static auto DiskControlStepper(uint16_t pc, uint16_t addr, uint8_t bWrite,
                               uint8_t d, uint32_t nCyclesLeft) -> uint8_t;

static auto DiskEnable(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                       uint32_t nCyclesLeft) -> uint8_t;

static auto DiskReadWrite(uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d,
                          uint32_t nCyclesLeft) -> uint8_t;

static auto DiskSetLatchValue(uint16_t pc, uint16_t addr, uint8_t bWrite,
                              uint8_t d, uint32_t nCyclesLeft) -> uint8_t;

static auto DiskSetReadMode(uint16_t pc, uint16_t addr, uint8_t bWrite,
                            uint8_t d, uint32_t nCyclesLeft) -> uint8_t;

static auto DiskSetWriteMode(uint16_t pc, uint16_t addr, uint8_t bWrite,
                             uint8_t d, uint32_t nCyclesLeft) -> uint8_t;

#define LOG_DISK_ENABLED 0

#if (LOG_DISK_ENABLED)
#define LOG_DISK(format, ...) Logger::Info(format, __VA_ARGS__)
#else
#define LOG_DISK(...)
#endif

bool enhancedisk = true;

static uint16_t currdrive = 0;
static bool diskaccessed = false;
static Disk_t g_aFloppyDisk[DRIVES];
static uint8_t floppylatch = 0;
static bool floppymotoron = false;
static bool floppywritemode = false;
static uint16_t phases;  // state bits for stepper magnet phases 0 - 3
static uint32_t s_spin_accumulator = 0;
static uint32_t s_rotation_accumulator = 0;

static auto CheckSpinning() -> void {
  Disk_t* fptr = &g_aFloppyDisk[currdrive];
  bool was_spinning = (fptr->spinning > 0);
  if (floppymotoron) {
    fptr->spinning = 20000;
  }
  bool now_spinning = (fptr->spinning > 0);

  if (was_spinning != now_spinning) {
    if (g_pDiskHost) {
      g_pDiskHost->NotifyActivityChanged(g_nDiskSlot, now_spinning);
      g_pDiskHost->NotifyStatusChanged(g_nDiskSlot);
    }
  }
}

static auto DiskIsEffectivelyWriteProtected(const int iDrive) -> bool {
  if (iDrive < 0 || iDrive >= 2) return false;
  Disk_t* fptr = &g_aFloppyDisk[iDrive];

  bool protected_result = false;

  // Layer 3: User toggle
  if (fptr->user_write_protected) {
    protected_result = true;
  }
  // Layer 2: OS read-only
  else if (fptr->os_readonly) {
    protected_result = true;
  }
  // Layer 1: Driver/Format capability
  else if (fptr->driver != nullptr) {
    bool has_write_cap = (fptr->driver->capabilities & 0x01) != 0;
    if (!has_write_cap) {
      protected_result = true;
    } else {
      protected_result =
          fptr->driver->is_write_protected(fptr->driver_instance);
    }
  }

  return protected_result;
}

static auto GetImageTitle(const char* imageFileName, Disk_t* fptr) -> char* {
  char imagetitle[MAX_DISK_FULL_NAME + 1];
  const char* startpos = imageFileName;

  const char* last_sep = strrchr(startpos, FILE_SEPARATOR);
  if (last_sep) {
    startpos = last_sep + 1;
  }
  Util_SafeStrCpy(imagetitle, startpos, MAX_DISK_FULL_NAME);

  bool found = false;
  int loop = 0;
  while (imagetitle[loop] && !found) {
    if (IsCharLower(imagetitle[loop])) {
      found = true;
    } else {
      loop++;
    }
  }

  if ((!found) && (loop > 2)) {
    for (char* p = imagetitle + 1; *p; ++p)
      *p = static_cast<char>(tolower(static_cast<uint8_t>(*p)));
  }

  Util_SafeStrCpy(fptr->fullname, imageFileName, MAX_DISK_FULL_NAME);

  if (imagetitle[0]) {
    char* dot = strrchr(imagetitle, '.');
    if (dot && dot > imagetitle) {
      *dot = 0;
    }
  }

  Util_SafeStrCpy(fptr->imagename, imagetitle, MAX_DISK_IMAGE_NAME);
  return fptr->imagename;
}

static auto IsDriveValid(const int iDrive) -> bool {
  return (iDrive >= 0 && iDrive < DRIVES);
}

static void AllocTrack(int drive) {
  Disk_t* fptr = &g_aFloppyDisk[drive];
  const int NIBBLES_HARDWARE = 6656;
  fptr->trackimage = static_cast<uint8_t*>(malloc(NIBBLES_HARDWARE));
  if (fptr->trackimage) memset(fptr->trackimage, 0, NIBBLES_HARDWARE);
}

static void ReadTrack(int iDrive) {
  if (!IsDriveValid(iDrive)) {
    return;
  }

  Disk_t* pFloppy = &g_aFloppyDisk[iDrive];

  if (pFloppy->track >= TRACKS) {
    pFloppy->trackimagedata = false;
    return;
  }

  if (!pFloppy->trackimage) {
    AllocTrack(iDrive);
  }

  if (pFloppy->trackimage && pFloppy->driver && pFloppy->driver_instance) {
    LOG_DISK("read track %2X%s\r", pFloppy->track,
             (pFloppy->phase & 1) ? ".5" : "");

    pFloppy->driver->read_track(pFloppy->driver_instance, pFloppy->track,
                                pFloppy->phase, pFloppy->trackimage,
                                &pFloppy->nibbles);

    pFloppy->byte = 0;
    pFloppy->trackimagedata = (pFloppy->nibbles != 0);
  }
}

static void RemoveDisk(int iDrive) {
  Disk_t* pFloppy = &g_aFloppyDisk[iDrive];

  if (pFloppy->driver) {
    if (pFloppy->trackimage && pFloppy->trackimagedirty) {
      WriteTrack(iDrive);
    }

    pFloppy->driver->close(pFloppy->driver_instance);
    pFloppy->driver = nullptr;
    pFloppy->driver_instance = nullptr;

    if (pFloppy->trackimage) {
      free(pFloppy->trackimage);
      pFloppy->trackimage = nullptr;
    }

    pFloppy->trackimagedata = false;

    if (g_pDiskHost) {
      const char* key =
          (iDrive == 0) ? REGVALUE_DISK_IMAGE1 : REGVALUE_DISK_IMAGE2;
      g_pDiskHost->SetConfig("Slots", key, "");
      g_pDiskHost->NotifyStatusChanged(g_nDiskSlot);
    }
  }

  pFloppy->os_readonly = false;
  pFloppy->user_write_protected = false;
  pFloppy->last_error = DISK_ERR_NONE;
  memset(pFloppy->imagename, 0, MAX_DISK_IMAGE_NAME + 1);
  memset(pFloppy->fullname, 0, MAX_DISK_FULL_NAME + 1);
}

static void WriteTrack(int iDrive) {
  Disk_t* pFloppy = &g_aFloppyDisk[iDrive];

  if (pFloppy->track >= TRACKS) {
    return;
  }

  if (DiskIsEffectivelyWriteProtected(iDrive)) return;

  if (pFloppy->trackimage && pFloppy->driver_instance) {
    pFloppy->driver->write_track(pFloppy->driver_instance, pFloppy->track,
                                 pFloppy->phase, pFloppy->trackimage,
                                 pFloppy->nibbles);
  }

  pFloppy->trackimagedirty = false;
}

static void DiskBoot() {
  if (g_aFloppyDisk[0].driver) {
    // Standard hardware boot: point CPU to $Cx00
    // (Actual logic is handled by frontend or CPU start)
    floppymotoron = false;
  }
}

static auto DiskControlMotor(uint16_t, uint16_t address, uint8_t, uint8_t,
                             uint32_t) -> uint8_t {
  floppymotoron = (address & 1) != 0;
  CheckSpinning();
  return MemReturnRandomData(1);
}

static auto DiskControlStepper(uint16_t, uint16_t address, uint8_t, uint8_t,
                               uint32_t) -> uint8_t {
  Disk_t* fptr = &g_aFloppyDisk[currdrive];
  int phase = (address >> 1) & 3;
  int phase_bit = (1 << phase);

  if (address & 1) {
    phases |= phase_bit;
  } else {
    phases &= ~phase_bit;
  }

  int direction = 0;
  if (phases & (1 << ((fptr->phase + 1) & 3))) {
    direction += 1;
  }
  if (phases & (1 << ((fptr->phase + 3) & 3))) {
    direction -= 1;
  }

  if (direction) {
    int oldphase = fptr->phase;
    fptr->phase = MAX(0, MIN(79, fptr->phase + direction));
    fptr->track = MIN(TRACKS - 1, fptr->phase >> 1);

    if (fptr->phase != oldphase) {
      if (fptr->trackimage && fptr->trackimagedirty) {
        WriteTrack(currdrive);
      }
      fptr->trackimagedata = false;
    }
  }
  return (address == 0xE0) ? 0xFF : MemReturnRandomData(1);
}

static void DiskDestroy() {
  DiskLoader_Shutdown();
  RemoveDisk(0);
  RemoveDisk(1);
}

static auto DiskEnable(uint16_t pc, uint16_t address, uint8_t bWrite, uint8_t d,
                       uint32_t nCyclesLeft) -> uint8_t {
  (void)pc;
  (void)bWrite;
  (void)d;
  (void)nCyclesLeft;
  currdrive = address & 1;
  g_aFloppyDisk[!currdrive].spinning = 0;
  g_aFloppyDisk[!currdrive].writelight = 0;
  CheckSpinning();
  return 0;
}

static void DiskEject(const int iDrive) {
  if (IsDriveValid(iDrive)) {
    RemoveDisk(iDrive);
    if (g_pDiskHost) {
      const char* key =
          (iDrive == 0) ? REGVALUE_DISK_IMAGE1 : REGVALUE_DISK_IMAGE2;
      g_pDiskHost->SetConfig("Slots", key, "");
    }
  }
}

auto DiskInitialize() -> void {
  int loop = DRIVES;
  while (loop--) {
    memset(&g_aFloppyDisk[loop], 0, sizeof(Disk_t));
  }
  s_spin_accumulator = 0;
  s_rotation_accumulator = 0;
}

static auto DiskInsert_Internal(int drive, const char* imageFileName,
                                bool writeProtected, bool createIfNecessary)
    -> DiskError_e {
  Disk_t* fptr = &g_aFloppyDisk[drive];

  if (fptr->driver != nullptr) {
    RemoveDisk(drive);
  }
  memset(fptr, 0, sizeof(Disk_t));

  fptr->user_write_protected = writeProtected;
  fptr->os_readonly = false;
  DiskError_e error = DiskLoader_Open(
      imageFileName, createIfNecessary, &fptr->os_readonly,
      const_cast<DiskFormatDriver_t**>(&fptr->driver), &fptr->driver_instance);

  fptr->last_error = error;

  if (error == DISK_ERR_NONE) {
    char* tmp = GetImageTitle(imageFileName, fptr);
    char s_title[MAX_DISK_IMAGE_NAME + 32];
    snprintf(s_title, sizeof(s_title), "%.*s - %.*s",
             static_cast<int>(strlen(g_pAppTitle)), g_pAppTitle,
             static_cast<int>(strlen(tmp)), tmp);
    Linapple_UpdateTitle(s_title);

    if (g_pDiskHost != nullptr) {
      const char* key =
          (drive == 0) ? REGVALUE_DISK_IMAGE1 : REGVALUE_DISK_IMAGE2;
      g_pDiskHost->SetConfig("Slots", key, imageFileName);
      g_pDiskHost->NotifyStatusChanged(g_nDiskSlot);
    }
  } else {
    if (g_pDiskHost != nullptr) {
      g_pDiskHost->NotifyStatusChanged(g_nDiskSlot);
    }
  }
  return error;
}

static void DiskSetProtect(const int iDrive, const bool bWriteProtect) {
  if (IsDriveValid(iDrive)) {
    g_aFloppyDisk[iDrive].user_write_protected = bWriteProtect;
    if (g_pDiskHost) {
      g_pDiskHost->NotifyStatusChanged(g_nDiskSlot);
    }
  }
}

static auto DiskReadWrite(uint16_t, uint16_t, uint8_t, uint8_t, uint32_t)
    -> uint8_t {
  Disk_t* fptr = &g_aFloppyDisk[currdrive];
  diskaccessed = true;
  if ((!fptr->trackimagedata) && fptr->driver) {
    ReadTrack(currdrive);
  }
  if (!fptr->trackimagedata) {
    return 0xFF;
  }
  uint8_t result = 0;

  bool is_protected = DiskIsEffectivelyWriteProtected(currdrive);

  if ((!floppywritemode) || (!is_protected)) {
    if (floppywritemode) {
      if (floppylatch & 0x80) {
        *(fptr->trackimage + fptr->byte) = floppylatch;
        fptr->trackimagedirty = true;
      } else {
        return 0;
      }
    } else {
      result = *(fptr->trackimage + fptr->byte);
    }
  }
  if (++fptr->byte >= static_cast<uint32_t>(fptr->nibbles)) {
    fptr->byte = 0;
  }
  return result;
}

static void DiskReset() {
  floppymotoron = false;
  phases = 0;
}

static auto DiskSetLatchValue(uint16_t, uint16_t, uint8_t write, uint8_t value,
                              uint32_t) -> uint8_t {
  if (write) {
    floppylatch = value;
  }
  return floppylatch;
}

static auto DiskSetReadMode(uint16_t, uint16_t, uint8_t, uint8_t, uint32_t)
    -> uint8_t {
  floppywritemode = false;
  return MemReturnRandomData(DiskIsEffectivelyWriteProtected(currdrive));
}

static auto DiskSetWriteMode(uint16_t, uint16_t, uint8_t, uint8_t, uint32_t)
    -> uint8_t {
  floppywritemode = true;
  bool modechange = !g_aFloppyDisk[currdrive].writelight;
  g_aFloppyDisk[currdrive].writelight = 20000;
  if (modechange) {
    if (g_pDiskHost) g_pDiskHost->NotifyStatusChanged(g_nDiskSlot);
  }
  return MemReturnRandomData(1);
}

auto DiskUpdatePosition(uint32_t cycles) -> void {
  s_spin_accumulator += cycles;
  uint32_t spin_ticks = s_spin_accumulator >> 6;
  s_spin_accumulator &= 63;

  s_rotation_accumulator += cycles;
  uint32_t rotation_ticks = s_rotation_accumulator >> 5;
  s_rotation_accumulator &= 31;

  int loop = 2;
  while (loop--) {
    Disk_t* fptr = &g_aFloppyDisk[loop];
    if (fptr->spinning && !floppymotoron) {
      if (spin_ticks >= fptr->spinning) {
        fptr->spinning = 0;
        if (g_pDiskHost) {
          g_pDiskHost->NotifyActivityChanged(g_nDiskSlot, false);
          g_pDiskHost->NotifyStatusChanged(g_nDiskSlot);
        }
      } else {
        fptr->spinning -= spin_ticks;
      }
    }
    if (floppywritemode && (currdrive == loop) && fptr->spinning) {
      fptr->writelight = 20000;
    } else if (fptr->writelight) {
      if (spin_ticks >= fptr->writelight) {
        fptr->writelight = 0;
        if (g_pDiskHost) {
          g_pDiskHost->NotifyStatusChanged(g_nDiskSlot);
        }
      } else {
        fptr->writelight -= spin_ticks;
      }
    }
    if ((!enhancedisk) && (!diskaccessed) && fptr->spinning) {
      if (g_pDiskHost) g_pDiskHost->RequestPreciseTiming();
      fptr->byte += rotation_ticks;
      if (fptr->byte >= static_cast<uint32_t>(fptr->nibbles)) {
        fptr->byte %=
            (fptr->nibbles ? static_cast<uint32_t>(fptr->nibbles) : 1);
      }
    }
  }
  diskaccessed = false;
}

static auto DiskDriveSwap() -> bool {
  if (g_aFloppyDisk[0].spinning || g_aFloppyDisk[1].spinning) {
    return false;
  }

  Disk_t temp{};
  memcpy(&temp, &g_aFloppyDisk[0], sizeof(Disk_t));
  memcpy(&g_aFloppyDisk[0], &g_aFloppyDisk[1], sizeof(Disk_t));
  memcpy(&g_aFloppyDisk[1], &temp, sizeof(Disk_t));

  char s_title[MAX_DISK_IMAGE_NAME + 32];
  snprintf(s_title, MAX_DISK_IMAGE_NAME + 32, "%.*s - %.*s",
           static_cast<int>(strlen(g_pAppTitle)), g_pAppTitle,
           static_cast<int>(strlen(g_aFloppyDisk[0].imagename)),
           g_aFloppyDisk[0].imagename);
  Linapple_UpdateTitle(s_title);

  if (g_pDiskHost) {
    g_pDiskHost->NotifyStatusChanged(g_nDiskSlot);
  }

  return true;
}

auto Disk_IORead(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite,
                 uint8_t d, uint32_t nCyclesLeft) -> uint8_t;
auto Disk_IOWrite(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite,
                  uint8_t d, uint32_t nCyclesLeft) -> uint8_t;

static auto Disk_ABI_Init(int slot, HostInterface_t* host) -> void* {
  g_pDiskHost = host;
  g_nDiskSlot = slot;

  DiskLoader_Init();
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_woz2_driver));
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_iie_driver));
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_nib_driver));
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_nb2_driver));
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_do_driver));
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_po_driver));

  DiskInitialize();

  char path1[PATH_MAX_LEN] = {0};
  host->GetConfig("Slots", REGVALUE_DISK_IMAGE1, path1, sizeof(path1));

  char path2[PATH_MAX_LEN] = {0};
  host->GetConfig("Slots", REGVALUE_DISK_IMAGE2, path2, sizeof(path2));

  if (path1[0]) {
    DiskInsert_Internal(0, path1, false, false);
  }
  if (path2[0]) {
    DiskInsert_Internal(1, path2, false, false);
  }

  static uint8_t patched_rom[256];
  memcpy(patched_rom, Disk2_rom, 256);
  // TODO/FIXME: HACK! REMOVE A WAIT ROUTINE FROM THE DISK CONTROLLER'S FIRMWARE
  patched_rom[0x4C] = 0xA9;
  patched_rom[0x4D] = 0x00;
  patched_rom[0x4E] = 0xEA;
  host->RegisterCxROM(slot, patched_rom);

  host->RegisterIO(slot, Disk_IORead, Disk_IOWrite, nullptr, nullptr);

  return reinterpret_cast<void*>(1);  // Dummy instance
}

static void Disk_ABI_Reset(void* instance) {
  (void)instance;
  for (int i = 0; i < 2; ++i) {
    RemoveDisk(i);
  }
  DiskReset();
}

static void Disk_ABI_Shutdown(void* instance) {
  (void)instance;
  DiskDestroy();
}

static auto Disk_ABI_Think(void* instance, uint32_t cycles) -> void {
  (void)instance;
  DiskUpdatePosition(cycles);
}

static void PopulateDiskStatus(DiskStatus_t* status) {
  Disk_t* d0 = &g_aFloppyDisk[0];
  Disk_t* d1 = &g_aFloppyDisk[1];

  status->drive0_last_error = static_cast<int32_t>(d0->last_error);
  status->drive0_loaded = (d0->driver != nullptr) ? 1 : 0;
  status->drive0_spinning = (d0->spinning > 0) ? 1 : 0;
  status->drive0_writing = (d0->writelight > 0) ? 1 : 0;
  status->drive0_write_protected = DiskIsEffectivelyWriteProtected(0) ? 1 : 0;
  Util_SafeStrCpy(status->drive0_name, d0->imagename, DISK_STATUS_NAME_MAX);
  Util_SafeStrCpy(status->drive0_full_path, d0->fullname, DISK_STATUS_PATH_MAX);

  status->drive1_last_error = static_cast<int32_t>(d1->last_error);
  status->drive1_loaded = (d1->driver != nullptr) ? 1 : 0;
  status->drive1_spinning = (d1->spinning > 0) ? 1 : 0;
  status->drive1_writing = (d1->writelight > 0) ? 1 : 0;
  status->drive1_write_protected = DiskIsEffectivelyWriteProtected(1) ? 1 : 0;
  Util_SafeStrCpy(status->drive1_name, d1->imagename, DISK_STATUS_NAME_MAX);
  Util_SafeStrCpy(status->drive1_full_path, d1->fullname, DISK_STATUS_PATH_MAX);
}

static auto Disk_ABI_Command(void* instance, uint32_t cmd, const void* data,
                             size_t size) -> PeripheralStatus {
  (void)instance;
  switch (cmd) {
    case DISK_CMD_INSERT: {
      if (!data || size < sizeof(DiskInsertCmd_t)) return PERIPHERAL_ERROR;
      auto* c = static_cast<const DiskInsertCmd_t*>(data);
      if (!IsDriveValid(c->drive)) return PERIPHERAL_ERROR;
      DiskInsert_Internal(c->drive, c->path, c->write_protected != 0,
                          c->create_if_necessary != 0);
      return PERIPHERAL_OK;
    }
    case DISK_CMD_EJECT: {
      if (!data || size < sizeof(DiskEjectCmd_t)) return PERIPHERAL_ERROR;
      auto* c = static_cast<const DiskEjectCmd_t*>(data);
      if (!IsDriveValid(c->drive)) return PERIPHERAL_ERROR;
      DiskEject(c->drive);
      return PERIPHERAL_OK;
    }
    case DISK_CMD_SWAP_DRIVES:
      DiskDriveSwap();
      return PERIPHERAL_OK;
    case DISK_CMD_BOOT:
      DiskBoot();
      return PERIPHERAL_OK;
    case DISK_CMD_SET_PROTECT: {
      if (!data || size < sizeof(DiskSetProtectCmd_t)) return PERIPHERAL_ERROR;
      auto* c = static_cast<const DiskSetProtectCmd_t*>(data);
      if (!IsDriveValid(c->drive)) return PERIPHERAL_ERROR;
      DiskSetProtect(c->drive, c->write_protected != 0);
      return PERIPHERAL_OK;
    }
    default:
      return PERIPHERAL_ERROR;
  }
}

static auto Disk_ABI_Query(void* instance, uint32_t cmd, void* data,
                           size_t* size) -> PeripheralStatus {
  (void)instance;
  if (cmd == DISK_CMD_GET_STATUS) {
    if (!data || !size || *size < sizeof(DiskStatus_t)) {
      if (size) *size = sizeof(DiskStatus_t);
      return PERIPHERAL_ERROR;
    }
    PopulateDiskStatus(static_cast<DiskStatus_t*>(data));
    *size = sizeof(DiskStatus_t);
    return PERIPHERAL_OK;
  }
  return PERIPHERAL_ERROR;
}

static auto Disk_ABI_SaveState(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus {
  (void)instance;
  if (size == nullptr) return PERIPHERAL_ERROR;

  if (buffer == nullptr) {
    *size = sizeof(DiskSavedState_t);
    return PERIPHERAL_OK;
  }

  if (*size < sizeof(DiskSavedState_t)) {
    return PERIPHERAL_ERROR;
  }

  auto* ds = static_cast<DiskSavedState_t*>(buffer);
  memset(ds, 0, sizeof(DiskSavedState_t));

  ds->header.version = DISK_STATE_VERSION;
  ds->header.size = sizeof(DiskSavedState_t);

  for (int i = 0; i < 2; ++i) {
    Disk_t* fptr = &g_aFloppyDisk[i];
    DiskDriveState_t* dds = &ds->drives[i];

    Util_SafeStrCpy(dds->fullname, fptr->fullname, MAX_DISK_FULL_NAME + 1);
    dds->track = fptr->track;
    dds->phase = fptr->phase;
    dds->byte_pos = fptr->byte;
    dds->user_write_protected = fptr->user_write_protected;
    dds->trackimagedata = fptr->trackimagedata;
    dds->trackimagedirty = fptr->trackimagedirty;
    dds->spinning = fptr->spinning;
    dds->writelight = fptr->writelight;
    dds->nibbles = fptr->nibbles;
    if (fptr->trackimage != nullptr) {
      memcpy(dds->nTrack, fptr->trackimage, NIBBLES_PER_TRACK);
    } else {
      memset(dds->nTrack, 0, NIBBLES_PER_TRACK);
    }
  }

  ds->phases = phases;
  ds->currdrive = static_cast<uint16_t>(currdrive);
  ds->diskaccessed = diskaccessed;
  ds->enhancedisk = enhancedisk;
  ds->floppylatch = floppylatch;
  ds->floppymotoron = floppymotoron;
  ds->floppywritemode = floppywritemode;

  return PERIPHERAL_OK;
}

static auto Disk_ABI_LoadState(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus {
  (void)instance;
  if (buffer == nullptr || size < sizeof(DiskSavedState_t)) {
    return PERIPHERAL_ERROR;
  }

  const auto* ds = static_cast<const DiskSavedState_t*>(buffer);
  if (ds->header.version != DISK_STATE_VERSION) {
    return PERIPHERAL_ERROR;
  }

  phases = ds->phases;
  currdrive = ds->currdrive;
  diskaccessed = ds->diskaccessed;
  enhancedisk = ds->enhancedisk;
  floppylatch = ds->floppylatch;
  floppymotoron = ds->floppymotoron;
  floppywritemode = ds->floppywritemode;

  for (int i = 0; i < 2; ++i) {
    const DiskDriveState_t* dds = &ds->drives[i];

    RemoveDisk(i);

    DiskError_e err =
        DiskInsert_Internal(i, dds->fullname, dds->user_write_protected, false);
    if (err == DISK_ERR_NONE) {
      Disk_t* fptr = &g_aFloppyDisk[i];
      fptr->track = dds->track;
      fptr->phase = dds->phase;
      fptr->byte = dds->byte_pos;
      fptr->trackimagedata = dds->trackimagedata != 0;
      fptr->trackimagedirty = dds->trackimagedirty != 0;
      fptr->spinning = dds->spinning;
      fptr->writelight = dds->writelight;
      fptr->nibbles = dds->nibbles;

      if (fptr->trackimagedata) {
        if (fptr->trackimage == nullptr) {
          fptr->trackimage = static_cast<uint8_t*>(malloc(NIBBLES_PER_TRACK));
        }
        if (fptr->nibbles > 0) {
          memcpy(fptr->trackimage, dds->nTrack, NIBBLES_PER_TRACK);
        } else {
          memset(fptr->trackimage, 0, NIBBLES_PER_TRACK);
        }
      }
    } else {
      DiskError_e saved_err =
          static_cast<DiskError_e>(g_aFloppyDisk[i].last_error);
      memset(&g_aFloppyDisk[i], 0, sizeof(Disk_t));
      g_aFloppyDisk[i].last_error = saved_err;
    }
  }

  if (g_pDiskHost) {
    g_pDiskHost->NotifyStatusChanged(g_nDiskSlot);
  }

  return PERIPHERAL_OK;
}

Peripheral_t g_disk_peripheral = {LINAPPLE_ABI_VERSION,
                                  "Disk II",
                                  0xFE,  // Slots 1-7
                                  Disk_ABI_Init,
                                  Disk_ABI_Reset,
                                  Disk_ABI_Shutdown,
                                  Disk_ABI_Think,
                                  nullptr,  // on_vblank
                                  Disk_ABI_SaveState,
                                  Disk_ABI_LoadState,
                                  Disk_ABI_Command,
                                  Disk_ABI_Query};

extern "C" void Register_Disk() {
  Peripheral_Register_Builtin(&g_disk_peripheral);
}

#ifdef BUILD_SHARED_PERIPHERAL
EXPORT_PERIPHERAL(g_disk_peripheral)
#endif

auto Disk_IORead(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite,
                 uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  (void)instance;
  addr &= 0xFF;

  switch (addr & 0xf) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
      return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
    case 0x8:
    case 0x9:
      return DiskControlMotor(pc, addr, bWrite, d, nCyclesLeft);
    case 0xA:
    case 0xB:
      return DiskEnable(pc, addr, bWrite, d, nCyclesLeft);
    case 0xC:
      return DiskReadWrite(pc, addr, bWrite, d, nCyclesLeft);
    case 0xD:
      return DiskSetLatchValue(pc, addr, bWrite, d, nCyclesLeft);
    case 0xE:
      return DiskSetReadMode(pc, addr, bWrite, d, nCyclesLeft);
    case 0xF:
      return DiskSetWriteMode(pc, addr, bWrite, d, nCyclesLeft);
  }

  return 0;
}

auto Disk_IOWrite(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite,
                  uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  (void)instance;
  addr &= 0xFF;

  switch (addr & 0xf) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
      return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
    case 0x8:
    case 0x9:
      return DiskControlMotor(pc, addr, bWrite, d, nCyclesLeft);
    case 0xA:
    case 0xB:
      return DiskEnable(pc, addr, bWrite, d, nCyclesLeft);
    case 0xC:
      return DiskReadWrite(pc, addr, bWrite, d, nCyclesLeft);
    case 0xD:
      return DiskSetLatchValue(pc, addr, bWrite, d, nCyclesLeft);
    case 0xE:
      return DiskSetReadMode(pc, addr, bWrite, d, nCyclesLeft);
    case 0xF:
      return DiskSetWriteMode(pc, addr, bWrite, d, nCyclesLeft);
  }

  return 0;
}
