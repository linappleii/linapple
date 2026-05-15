// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory,
// cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
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

#include "apple2/peripherals/disk/Disk.h"

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Structs.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskFTP.h"
#include "apple2/peripherals/disk/DiskGCR.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/DoDriver.h"
#include "apple2/peripherals/disk/formats/IieDriver.h"
#include "apple2/peripherals/disk/formats/Nb2Driver.h"
#include "apple2/peripherals/disk/formats/NibDriver.h"
#include "apple2/peripherals/disk/formats/PoDriver.h"
#include "apple2/peripherals/disk/formats/Woz2Driver.h"
#include "apple2/peripherals/disk/ftpparse.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

namespace {
constexpr uint32_t DISK_SPINUP_TICKS = 20000;
constexpr uint32_t DISK_WRITE_LIGHT_TICKS = 20000;
constexpr uint8_t DISK_IO_ADDR_MASK = 0x0F;
constexpr uint8_t DISK_IO_ADDR_HI_MASK = 0xFF;
constexpr uint8_t DISK_LATCH_BIT = 0x80;
constexpr uint8_t DISK_FLOATING_BUS = 0xFF;

constexpr uint8_t DISK_IO_STEPPER_0 = 0x0;
constexpr uint8_t DISK_IO_STEPPER_1 = 0x1;
constexpr uint8_t DISK_IO_STEPPER_2 = 0x2;
constexpr uint8_t DISK_IO_STEPPER_3 = 0x3;
constexpr uint8_t DISK_IO_STEPPER_4 = 0x4;
constexpr uint8_t DISK_IO_STEPPER_5 = 0x5;
constexpr uint8_t DISK_IO_STEPPER_6 = 0x6;
constexpr uint8_t DISK_IO_STEPPER_7 = 0x7;
constexpr uint8_t DISK_IO_MOTOR_OFF = 0x8;
constexpr uint8_t DISK_IO_MOTOR_ON = 0x9;
constexpr uint8_t DISK_IO_DRIVE_1 = 0xA;
constexpr uint8_t DISK_IO_DRIVE_2 = 0xB;
constexpr uint8_t DISK_IO_READ_WRITE = 0xC;
constexpr uint8_t DISK_IO_SHIFT_REG = 0xD;
constexpr uint8_t DISK_IO_READ_MODE = 0xE;
constexpr uint8_t DISK_IO_WRITE_MODE = 0xF;
constexpr uint8_t DISK_IO_STEPPER_ALT = 0xE0;
}  // namespace

struct DiskPeripheral_t {
  std::array<Disk_t, DRIVES> drives{};
  uint16_t currdrive = 0;
  bool diskaccessed = false;
  uint8_t floppylatch = 0;
  bool floppymotoron = false;
  bool floppywritemode = false;
  uint16_t phases = 0;
  uint32_t s_spin_accumulator = 0;
  uint32_t s_rotation_accumulator = 0;
  HostInterface_t* host = nullptr;
  int slot = 0;
  bool enhancedisk = true;

  DiskPeripheral_t() = default;
};

static void CheckSpinning(DiskPeripheral_t* dp);
static auto IsDriveValid(const int iDrive) -> bool;
static void ReadTrack(DiskPeripheral_t* dp, int drive);
static void RemoveDisk(DiskPeripheral_t* dp, int drive);
static void WriteTrack(DiskPeripheral_t* dp, int drive);

static auto DiskInsert_Internal(DiskPeripheral_t* dp, int drive,
                                const char* imageFileName, bool writeProtected,
                                bool createIfNecessary) -> DiskError_e;

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

static auto DiskControlMotor(void* instance, uint16_t pc, uint16_t addr,
                             uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t;

static auto DiskControlStepper(void* instance, uint16_t pc, uint16_t addr,
                               uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t;

static auto DiskEnable(void* instance, uint16_t pc, uint16_t addr,
                       uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t;

static auto DiskReadWrite(void* instance, uint16_t pc, uint16_t addr,
                          uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t;

static auto DiskSetLatchValue(void* instance, uint16_t pc, uint16_t addr,
                              uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t;

static auto DiskSetReadMode(void* instance, uint16_t pc, uint16_t addr,
                            uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t;

static auto DiskSetWriteMode(void* instance, uint16_t pc, uint16_t addr,
                             uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t;

#if (0)  // Set to 1 to enable disk logging
#define LOG_DISK(format, ...) Logger::Info(format, __VA_ARGS__)
#else
#define LOG_DISK(...)
#endif

static auto CheckSpinning(DiskPeripheral_t* dp) -> void {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  Disk_t* fptr = &dp->drives[dp->currdrive];
  bool was_spinning = (fptr->spinning > 0);
  if (dp->floppymotoron) {
    fptr->spinning = DISK_SPINUP_TICKS;
  }
  bool now_spinning = (fptr->spinning > 0);

  if (was_spinning != now_spinning) {
    if (dp->host) {
      dp->host->NotifyActivityChanged(dp->slot, now_spinning);
      dp->host->NotifyStatusChanged(dp->slot);
    }
  }
}

static auto DiskIsEffectivelyWriteProtected(DiskPeripheral_t* dp,
                                            const int iDrive) -> bool {
  if (iDrive < 0 || iDrive >= DRIVES) return false;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  Disk_t* fptr = &dp->drives[iDrive];

  bool protected_result = false;

  // Layer 3: User toggle / Layer 2: OS read-only
  if (fptr->user_write_protected || fptr->os_readonly) {
    protected_result = true;
  }
  // Layer 1: Driver/Format capability
  else if (fptr->driver != nullptr) {
    bool has_write_cap = (fptr->driver->capabilities & 0x01) != 0;
    if (!has_write_cap) {
      protected_result = true;
    } else if (fptr->driver->is_write_protected != nullptr) {
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
  if (last_sep != nullptr) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    startpos = last_sep + 1;
  }
  Util_SafeStrCpy(imagetitle, startpos, MAX_DISK_FULL_NAME);

  bool found = false;
  int loop = 0;
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  while (imagetitle[loop] != '\0' && !found) {
    if (IsCharLower(imagetitle[loop])) {
      found = true;
    } else {
      loop++;
    }
  }

  if ((!found) && (loop > 2)) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for (char* p = imagetitle + 1; *p != '\0'; ++p) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      *p = static_cast<char>(tolower(static_cast<uint8_t>(*p)));
    }
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

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

static void AllocTrack(DiskPeripheral_t* dp, int drive) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  Disk_t* fptr = &dp->drives[drive];
  const int NIBBLES_HARDWARE = 6656;
  fptr->trackimage = new uint8_t[NIBBLES_HARDWARE]();
}

static void ReadTrack(DiskPeripheral_t* dp, int iDrive) {
  if (!IsDriveValid(iDrive)) {
    return;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  Disk_t* pFloppy = &dp->drives[iDrive];

  if (pFloppy->track >= TRACKS) {
    pFloppy->trackimagedata = false;
    return;
  }

  if (!pFloppy->trackimage) {
    AllocTrack(dp, iDrive);
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

static void RemoveDisk(DiskPeripheral_t* dp, int iDrive) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  Disk_t* pFloppy = &dp->drives[iDrive];

  if (pFloppy->driver) {
    if (pFloppy->trackimage && pFloppy->trackimagedirty) {
      WriteTrack(dp, iDrive);
    }

    pFloppy->driver->close(pFloppy->driver_instance);
    pFloppy->driver = nullptr;
    pFloppy->driver_instance = nullptr;

    if (pFloppy->trackimage) {
      delete[] pFloppy->trackimage;
      pFloppy->trackimage = nullptr;
    }

    pFloppy->trackimagedata = false;

    if (dp->host) {
      const char* key =
          (iDrive == 0) ? REGVALUE_DISK_IMAGE1 : REGVALUE_DISK_IMAGE2;
      dp->host->SetConfig("Slots", key, "");
      dp->host->NotifyStatusChanged(dp->slot);
    }
  }

  pFloppy->os_readonly = false;
  pFloppy->user_write_protected = false;
  pFloppy->last_error = DISK_ERR_NONE;
  memset(pFloppy->imagename, 0, MAX_DISK_IMAGE_NAME + 1);
  memset(pFloppy->fullname, 0, MAX_DISK_FULL_NAME + 1);
}

static void WriteTrack(DiskPeripheral_t* dp, int iDrive) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  Disk_t* pFloppy = &dp->drives[iDrive];

  if (pFloppy->track >= TRACKS) {
    return;
  }

  if (DiskIsEffectivelyWriteProtected(dp, iDrive)) return;

  if (pFloppy->trackimage && pFloppy->driver_instance) {
    pFloppy->driver->write_track(pFloppy->driver_instance, pFloppy->track,
                                 pFloppy->phase, pFloppy->trackimage,
                                 pFloppy->nibbles);
  }

  pFloppy->trackimagedirty = false;
}

static void DiskBoot(DiskPeripheral_t* dp) {
  if (dp->drives[0].driver) {
    dp->floppymotoron = false;
  }
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
// Justification: Functions are part of the Peripheral ABI or internal
// helpers that mimic it, where parameter order is fixed or follows convention.

static auto DiskControlMotor(void* instance, uint16_t, uint16_t address,
                             uint8_t, uint8_t, uint32_t) -> uint8_t {
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  dp->floppymotoron = (address & 1) != 0;
  CheckSpinning(dp);
  return MemReturnRandomData(1);
}

static auto DiskControlStepper(void* instance, uint16_t, uint16_t address,
                               uint8_t, uint8_t, uint32_t) -> uint8_t {
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  Disk_t* fptr = &dp->drives[dp->currdrive];
  int phase = (address >> 1) & 3;
  int phase_bit = (1 << phase);

  if (address & 1) {
    dp->phases |= phase_bit;
  } else {
    dp->phases &= ~phase_bit;
  }

  int direction = 0;
  if (dp->phases & (1 << ((fptr->phase + 1) & 3))) {
    direction += 1;
  }
  if (dp->phases & (1 << ((fptr->phase + 3) & 3))) {
    direction -= 1;
  }

  if (direction) {
    int oldphase = fptr->phase;
    fptr->phase = MAX(0, MIN(79, fptr->phase + direction));
    fptr->track = MIN(TRACKS - 1, fptr->phase >> 1);

    if (fptr->phase != oldphase) {
      if (fptr->trackimage && fptr->trackimagedirty) {
        WriteTrack(dp, dp->currdrive);
      }
      fptr->trackimagedata = false;
    }
  }
  return (address == DISK_IO_STEPPER_ALT) ? DISK_FLOATING_BUS
                                          : MemReturnRandomData(1);
}

static void DiskDestroy(DiskPeripheral_t* dp) {
  DiskLoader_Shutdown();
  RemoveDisk(dp, 0);
  RemoveDisk(dp, 1);
}

static auto DiskEnable(void* instance, uint16_t pc, uint16_t address,
                       uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t {
  (void)pc;
  (void)bWrite;
  (void)d;
  (void)nCyclesLeft;
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  dp->currdrive = address & 1;
  dp->drives[!dp->currdrive].spinning = 0;
  dp->drives[!dp->currdrive].writelight = 0;
  CheckSpinning(dp);
  return 0;
}

static void DiskEject(DiskPeripheral_t* dp, const int iDrive) {
  if (IsDriveValid(iDrive)) {
    RemoveDisk(dp, iDrive);
    if (dp->host) {
      const char* key =
          (iDrive == 0) ? REGVALUE_DISK_IMAGE1 : REGVALUE_DISK_IMAGE2;
      dp->host->SetConfig("Slots", key, "");
    }
  }
}

static auto DiskInsert_Internal(DiskPeripheral_t* dp, int drive,
                                const char* imageFileName, bool writeProtected,
                                bool createIfNecessary) -> DiskError_e {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  Disk_t* fptr = &dp->drives[drive];

  if (fptr->driver != nullptr) {
    RemoveDisk(dp, drive);
  }
  memset(fptr, 0, sizeof(Disk_t));

  fptr->user_write_protected = writeProtected;
  fptr->os_readonly = false;
  DiskError_e error = DiskLoader_Open(
      imageFileName, createIfNecessary, static_cast<uint8_t>(dp->enhancedisk),
      &fptr->os_readonly, const_cast<DiskFormatDriver_t**>(&fptr->driver),
      &fptr->driver_instance);

  fptr->last_error = error;

  if (error == DISK_ERR_NONE) {
    char* tmp = GetImageTitle(imageFileName, fptr);
    constexpr size_t TITLE_BUF_LEN = MAX_DISK_IMAGE_NAME + 32;
    char s_title[TITLE_BUF_LEN];
    snprintf(s_title, sizeof(s_title), "%.*s - %.*s",
             static_cast<int>(strlen(g_pAppTitle)), g_pAppTitle,
             static_cast<int>(strlen(tmp)), tmp);
    Linapple_UpdateTitle(s_title);

    if (dp->host != nullptr) {
      const char* key =
          (drive == 0) ? REGVALUE_DISK_IMAGE1 : REGVALUE_DISK_IMAGE2;
      dp->host->SetConfig("Slots", key, imageFileName);
      dp->host->NotifyStatusChanged(dp->slot);
    }
  } else {
    if (dp->host != nullptr) {
      dp->host->NotifyStatusChanged(dp->slot);
    }
  }
  return error;
}

static void DiskSetProtect(DiskPeripheral_t* dp, const int iDrive,
                           const bool bWriteProtect) {
  if (IsDriveValid(iDrive)) {
    dp->drives[iDrive].user_write_protected = bWriteProtect;
    if (dp->host) {
      dp->host->NotifyStatusChanged(dp->slot);
    }
  }
}

static auto DiskReadWrite(void* instance, uint16_t, uint16_t, uint8_t, uint8_t,
                          uint32_t) -> uint8_t {
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  Disk_t* fptr = &dp->drives[dp->currdrive];
  dp->diskaccessed = true;
  if ((!fptr->trackimagedata) && fptr->driver) {
    ReadTrack(dp, dp->currdrive);
  }
  if (!fptr->trackimagedata) {
    return DISK_FLOATING_BUS;
  }
  uint8_t result = 0;

  bool is_protected = DiskIsEffectivelyWriteProtected(dp, dp->currdrive);

  if ((!dp->floppywritemode) || (!is_protected)) {
    if (dp->floppywritemode) {
      if (dp->floppylatch & DISK_LATCH_BIT) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        *(fptr->trackimage + fptr->byte) = dp->floppylatch;
        fptr->trackimagedirty = true;
      } else {
        return 0;
      }
    } else {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      result = *(fptr->trackimage + fptr->byte);
    }
  }
  if (++fptr->byte >= static_cast<uint32_t>(fptr->nibbles)) {
    fptr->byte = 0;
  }
  return result;
}

static void DiskReset(DiskPeripheral_t* dp) {
  dp->floppymotoron = false;
  dp->phases = 0;
}

static auto DiskSetLatchValue(void* instance, uint16_t, uint16_t, uint8_t write,
                              uint8_t value, uint32_t) -> uint8_t {
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  if (write) {
    dp->floppylatch = value;
  }
  return dp->floppylatch;
}

static auto DiskSetReadMode(void* instance, uint16_t, uint16_t, uint8_t,
                            uint8_t, uint32_t) -> uint8_t {
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  dp->floppywritemode = false;
  return MemReturnRandomData(
      DiskIsEffectivelyWriteProtected(dp, dp->currdrive));
}

static auto DiskSetWriteMode(void* instance, uint16_t, uint16_t, uint8_t,
                             uint8_t, uint32_t) -> uint8_t {
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  dp->floppywritemode = true;
  bool modechange = !dp->drives[dp->currdrive].writelight;
  dp->drives[dp->currdrive].writelight = DISK_WRITE_LIGHT_TICKS;
  if (modechange) {
    if (dp->host) dp->host->NotifyStatusChanged(dp->slot);
  }
  return MemReturnRandomData(1);
}

static auto DiskUpdatePosition(DiskPeripheral_t* dp, uint32_t cycles) -> void {
  dp->s_spin_accumulator += cycles;
  constexpr uint32_t DISK_SPIN_SHIFT = 6;
  constexpr uint32_t DISK_SPIN_MASK = 63;
  uint32_t spin_ticks = dp->s_spin_accumulator >> DISK_SPIN_SHIFT;
  dp->s_spin_accumulator &= DISK_SPIN_MASK;

  dp->s_rotation_accumulator += cycles;
  constexpr uint32_t DISK_ROTATION_SHIFT = 5;
  constexpr uint32_t DISK_ROTATION_MASK = 31;
  uint32_t rotation_ticks = dp->s_rotation_accumulator >> DISK_ROTATION_SHIFT;
  dp->s_rotation_accumulator &= DISK_ROTATION_MASK;

  int loop = DRIVES;
  while (loop--) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
    // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    Disk_t* fptr = &dp->drives[loop];
    if (fptr->spinning && !dp->floppymotoron) {
      if (spin_ticks >= fptr->spinning) {
        fptr->spinning = 0;
        if (dp->host) {
          dp->host->NotifyActivityChanged(dp->slot, false);
          dp->host->NotifyStatusChanged(dp->slot);
        }
      } else {
        fptr->spinning -= spin_ticks;
      }
    }
    if (dp->floppywritemode && (dp->currdrive == static_cast<uint16_t>(loop)) &&
        fptr->spinning) {
      fptr->writelight = DISK_WRITE_LIGHT_TICKS;
    } else if (fptr->writelight) {
      if (spin_ticks >= fptr->writelight) {
        fptr->writelight = 0;
        if (dp->host) {
          dp->host->NotifyStatusChanged(dp->slot);
        }
      } else {
        fptr->writelight -= spin_ticks;
      }
    }
    if ((!dp->enhancedisk) && (!dp->diskaccessed) && fptr->spinning) {
      if (dp->host) dp->host->RequestPreciseTiming();
      fptr->byte += rotation_ticks;
      if (fptr->byte >= static_cast<uint32_t>(fptr->nibbles)) {
        fptr->byte %=
            (fptr->nibbles ? static_cast<uint32_t>(fptr->nibbles) : 1);
      }
    }
  }
  dp->diskaccessed = false;
}

static auto DiskDriveSwap(DiskPeripheral_t* dp) -> bool {
  if (dp->drives[0].spinning || dp->drives[1].spinning) {
    return false;
  }

  Disk_t temp{};
  memcpy(&temp, &dp->drives[0], sizeof(Disk_t));
  memcpy(&dp->drives[0], &dp->drives[1], sizeof(Disk_t));
  memcpy(&dp->drives[1], &temp, sizeof(Disk_t));

  constexpr size_t TITLE_BUF_LEN = MAX_DISK_IMAGE_NAME + 32;
  char s_title[TITLE_BUF_LEN];
  snprintf(s_title, TITLE_BUF_LEN, "%.*s - %.*s",
           static_cast<int>(strlen(g_pAppTitle)), g_pAppTitle,
           static_cast<int>(strlen(dp->drives[0].imagename)),
           dp->drives[0].imagename);
  Linapple_UpdateTitle(s_title);

  if (dp->host) {
    dp->host->NotifyStatusChanged(dp->slot);
  }

  return true;
}

auto Disk_IORead(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite,
                 uint8_t d, uint32_t nCyclesLeft) -> uint8_t;
auto Disk_IOWrite(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite,
                  uint8_t d, uint32_t nCyclesLeft) -> uint8_t;

auto DiskInitialize(DiskPeripheral_t* dp) -> void {
  int loop = DRIVES;
  while (loop--) {
    memset(&dp->drives[loop], 0, sizeof(Disk_t));
  }
  dp->s_spin_accumulator = 0;
  dp->s_rotation_accumulator = 0;
}

static void SyncDriverOptions(DiskPeripheral_t* dp) {
  for (int i = 0; i < DRIVES; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
    // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    Disk_t* fptr = &dp->drives[i];
    if (fptr->driver && fptr->driver->command) {
      uint8_t enh = dp->enhancedisk ? 1 : 0;
      fptr->driver->command(fptr->driver_instance,
                            DISK_DRIVER_CMD_SET_ENHANCED_SPEED, &enh, 1);
    }
  }
}

static auto Disk_ABI_Init(int slot, HostInterface_t* host) -> void* {
  auto* dp = new DiskPeripheral_t();
  dp->host = host;
  dp->slot = slot;

  DiskLoader_Init();
  // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast)
  // Justification: Driver registration requires non-const pointers to drivers.
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_woz2_driver));
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_iie_driver));
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_nib_driver));
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_nb2_driver));
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_do_driver));
  DiskLoader_Register(const_cast<DiskFormatDriver_t*>(&g_po_driver));
  // NOLINTEND(cppcoreguidelines-pro-type-const-cast)

  uint32_t enh_val = 1;
  LOAD("Enhance Disk Speed", &enh_val);
  dp->enhancedisk = (enh_val != 0);

  DiskInitialize(dp);

  char path1[PATH_MAX_LEN] = {0};
  host->GetConfig("Slots", REGVALUE_DISK_IMAGE1, path1, sizeof(path1));

  char path2[PATH_MAX_LEN] = {0};
  host->GetConfig("Slots", REGVALUE_DISK_IMAGE2, path2, sizeof(path2));

  if (path1[0]) {
    DiskInsert_Internal(dp, 0, path1, false, false);
  }
  if (path2[0]) {
    DiskInsert_Internal(dp, 1, path2, false, false);
  }

  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,
  // cppcoreguidelines-pro-type-const-cast) Justification: Hardware-level ROM
  // registration requires cast to raw bytes.
  host->RegisterCxROM(slot,
                      reinterpret_cast<uint8_t*>(const_cast<char*>(Disk2_rom)));
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,
  // cppcoreguidelines-pro-type-const-cast)

  host->RegisterIO(slot, Disk_IORead, Disk_IOWrite, nullptr, nullptr);

  return dp;
}

static void Disk_ABI_Reset(void* instance) {
  if (!instance) return;
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  for (int i = 0; i < 2; ++i) {
    RemoveDisk(dp, i);
  }
  DiskReset(dp);
}

static void Disk_ABI_Shutdown(void* instance) {
  if (!instance) return;
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  DiskDestroy(dp);
  delete dp;
}

static auto Disk_ABI_Think(void* instance, uint32_t cycles) -> void {
  if (!instance) return;
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  DiskUpdatePosition(dp, cycles);
}

static void PopulateDiskStatus(DiskPeripheral_t* dp, DiskStatus_t* status) {
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  Disk_t* d0 = &dp->drives[0];
  Disk_t* d1 = &dp->drives[1];
  // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,
  // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

  status->drive0_last_error = static_cast<int32_t>(d0->last_error);
  status->drive0_loaded = (d0->driver != nullptr) ? 1 : 0;
  status->drive0_spinning = (d0->spinning > 0) ? 1 : 0;
  status->drive0_writing = (d0->writelight > 0) ? 1 : 0;
  status->drive0_write_protected =
      DiskIsEffectivelyWriteProtected(dp, 0) ? 1 : 0;
  Util_SafeStrCpy(status->drive0_name, d0->imagename, DISK_STATUS_NAME_MAX);
  Util_SafeStrCpy(status->drive0_full_path, d0->fullname, DISK_STATUS_PATH_MAX);

  status->drive1_last_error = static_cast<int32_t>(d1->last_error);
  status->drive1_loaded = (d1->driver != nullptr) ? 1 : 0;
  status->drive1_spinning = (d1->spinning > 0) ? 1 : 0;
  status->drive1_writing = (d1->writelight > 0) ? 1 : 0;
  status->drive1_write_protected =
      DiskIsEffectivelyWriteProtected(dp, 1) ? 1 : 0;
  Util_SafeStrCpy(status->drive1_name, d1->imagename, DISK_STATUS_NAME_MAX);
  Util_SafeStrCpy(status->drive1_full_path, d1->fullname, DISK_STATUS_PATH_MAX);
}

static auto Disk_ABI_Command(void* instance, uint32_t cmd, const void* data,
                             size_t size) -> PeripheralStatus {
  if (!instance) return PERIPHERAL_ERROR;
  auto* dp = static_cast<DiskPeripheral_t*>(instance);

  switch (cmd) {
    case DISK_CMD_INSERT: {
      if (!data || size < sizeof(DiskInsertCmd_t)) return PERIPHERAL_ERROR;
      auto* c = static_cast<const DiskInsertCmd_t*>(data);
      if (!IsDriveValid(c->drive)) return PERIPHERAL_ERROR;
      DiskInsert_Internal(dp, c->drive, c->path, c->write_protected != 0,
                          c->create_if_necessary != 0);
      return PERIPHERAL_OK;
    }
    case DISK_CMD_EJECT: {
      if (!data || size < sizeof(DiskEjectCmd_t)) return PERIPHERAL_ERROR;
      auto* c = static_cast<const DiskEjectCmd_t*>(data);
      if (!IsDriveValid(c->drive)) return PERIPHERAL_ERROR;
      DiskEject(dp, c->drive);
      return PERIPHERAL_OK;
    }
    case DISK_CMD_SWAP_DRIVES:
      DiskDriveSwap(dp);
      return PERIPHERAL_OK;
    case DISK_CMD_BOOT:
      DiskBoot(dp);
      return PERIPHERAL_OK;
    case DISK_CMD_SET_PROTECT: {
      if (!data || size < sizeof(DiskSetProtectCmd_t)) return PERIPHERAL_ERROR;
      auto* c = static_cast<const DiskSetProtectCmd_t*>(data);
      if (!IsDriveValid(c->drive)) return PERIPHERAL_ERROR;
      DiskSetProtect(dp, c->drive, c->write_protected != 0);
      return PERIPHERAL_OK;
    }
    case DISK_DRIVER_CMD_SET_ENHANCED_SPEED: {
      if (size < 1) return PERIPHERAL_ERROR;
      dp->enhancedisk = (*static_cast<const uint8_t*>(data) != 0);
      SyncDriverOptions(dp);
      return PERIPHERAL_OK;
    }
    default:
      return PERIPHERAL_ERROR;
  }
}

static auto Disk_ABI_Query(void* instance, uint32_t cmd, void* data,
                           size_t* size) -> PeripheralStatus {
  if (!instance) return PERIPHERAL_ERROR;
  auto* dp = static_cast<DiskPeripheral_t*>(instance);

  if (cmd == DISK_CMD_GET_STATUS) {
    if (!data || !size || *size < sizeof(DiskStatus_t)) {
      if (size) *size = sizeof(DiskStatus_t);
      return PERIPHERAL_ERROR;
    }
    PopulateDiskStatus(dp, static_cast<DiskStatus_t*>(data));
    *size = sizeof(DiskStatus_t);
    return PERIPHERAL_OK;
  }
  return PERIPHERAL_ERROR;
}

static auto Disk_ABI_SaveState(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus {
  if (!instance || !size) return PERIPHERAL_ERROR;
  auto* dp = static_cast<DiskPeripheral_t*>(instance);

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

  for (int i = 0; i < DRIVES; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
    // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    Disk_t* fptr = &dp->drives[i];
    DiskDriveState_t* dds = &ds->drives[i];

    Util_SafeStrCpy(dds->fullname, fptr->fullname, MAX_DISK_FULL_NAME + 1);
    dds->track = fptr->track;
    dds->phase = fptr->phase;
    dds->byte_pos = static_cast<int32_t>(fptr->byte);
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

  ds->phases = dp->phases;
  ds->currdrive = static_cast<uint16_t>(dp->currdrive);
  ds->diskaccessed = dp->diskaccessed;
  ds->enhancedisk = dp->enhancedisk ? 1 : 0;
  ds->floppylatch = dp->floppylatch;
  ds->floppymotoron = dp->floppymotoron;
  ds->floppywritemode = dp->floppywritemode;

  return PERIPHERAL_OK;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static auto Disk_ABI_LoadState(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus {
  if (!instance || buffer == nullptr || size < sizeof(DiskSavedState_t)) {
    return PERIPHERAL_ERROR;
  }
  auto* dp = static_cast<DiskPeripheral_t*>(instance);

  const auto* ds = static_cast<const DiskSavedState_t*>(buffer);
  if (ds->header.version != DISK_STATE_VERSION) {
    return PERIPHERAL_ERROR;
  }

  dp->phases = ds->phases;
  dp->currdrive = ds->currdrive;
  dp->diskaccessed = ds->diskaccessed != 0;
  dp->enhancedisk = (ds->enhancedisk != 0);
  dp->floppylatch = ds->floppylatch;
  dp->floppymotoron = ds->floppymotoron != 0;
  dp->floppywritemode = ds->floppywritemode != 0;

  for (int i = 0; i < DRIVES; ++i) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,
    // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const DiskDriveState_t* dds = &ds->drives[i];

    RemoveDisk(dp, i);

    DiskError_e err = DiskInsert_Internal(
        dp, i, dds->fullname, dds->user_write_protected != 0, false);
    if (err == DISK_ERR_NONE) {
      Disk_t* fptr = &dp->drives[i];
      // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,
      // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      fptr->track = dds->track;
      fptr->phase = dds->phase;
      fptr->byte = static_cast<uint32_t>(dds->byte_pos);
      fptr->trackimagedata = dds->trackimagedata != 0;
      fptr->trackimagedirty = dds->trackimagedirty != 0;
      fptr->spinning = dds->spinning;
      fptr->writelight = dds->writelight;
      fptr->nibbles = dds->nibbles;

      if (fptr->trackimagedata) {
        if (fptr->trackimage == nullptr) {
          fptr->trackimage = new uint8_t[NIBBLES_PER_TRACK]();
        }
        if (fptr->nibbles > 0) {
          memcpy(fptr->trackimage, dds->nTrack, NIBBLES_PER_TRACK);
        } else {
          memset(fptr->trackimage, 0, NIBBLES_PER_TRACK);
        }
      }
    } else {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
      // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      auto saved_err = static_cast<DiskError_e>(dp->drives[i].last_error);
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
      // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      memset(&dp->drives[i], 0, sizeof(Disk_t));
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,
      // cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      dp->drives[i].last_error = saved_err;
    }
  }

  if (dp->host) {
    dp->host->NotifyStatusChanged(dp->slot);
  }
  SyncDriverOptions(dp);

  return PERIPHERAL_OK;
}

auto Disk_IORead(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite,
                 uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  if (!instance) return DISK_FLOATING_BUS;
  addr &= DISK_IO_ADDR_HI_MASK;

  switch (addr & DISK_IO_ADDR_MASK) {
    case DISK_IO_STEPPER_0:
    case DISK_IO_STEPPER_1:
    case DISK_IO_STEPPER_2:
    case DISK_IO_STEPPER_3:
    case DISK_IO_STEPPER_4:
    case DISK_IO_STEPPER_5:
    case DISK_IO_STEPPER_6:
    case DISK_IO_STEPPER_7:
      return DiskControlStepper(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_MOTOR_OFF:
    case DISK_IO_MOTOR_ON:
      return DiskControlMotor(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_DRIVE_1:
    case DISK_IO_DRIVE_2:
      return DiskEnable(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_READ_WRITE:
      return DiskReadWrite(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_SHIFT_REG:
      return DiskSetLatchValue(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_READ_MODE:
      return DiskSetReadMode(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_WRITE_MODE:
      return DiskSetWriteMode(instance, pc, addr, bWrite, d, nCyclesLeft);
    default:
      break;
  }

  return 0;
}

auto Disk_IOWrite(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite,
                  uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  if (!instance) return 0;
  addr &= DISK_IO_ADDR_HI_MASK;

  switch (addr & DISK_IO_ADDR_MASK) {
    case DISK_IO_STEPPER_0:
    case DISK_IO_STEPPER_1:
    case DISK_IO_STEPPER_2:
    case DISK_IO_STEPPER_3:
    case DISK_IO_STEPPER_4:
    case DISK_IO_STEPPER_5:
    case DISK_IO_STEPPER_6:
    case DISK_IO_STEPPER_7:
      return DiskControlStepper(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_MOTOR_OFF:
    case DISK_IO_MOTOR_ON:
      return DiskControlMotor(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_DRIVE_1:
    case DISK_IO_DRIVE_2:
      return DiskEnable(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_READ_WRITE:
      return DiskReadWrite(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_SHIFT_REG:
      return DiskSetLatchValue(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_READ_MODE:
      return DiskSetReadMode(instance, pc, addr, bWrite, d, nCyclesLeft);
    case DISK_IO_WRITE_MODE:
      return DiskSetWriteMode(instance, pc, addr, bWrite, d, nCyclesLeft);
    default:
      break;
  }

  return 0;
}

Peripheral_t g_disk_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.disk_ii",
    .name = "Disk II",
    .description = "Apple II floppy disk controller emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = DISK_DEFAULT_SLOT,
    .init = Disk_ABI_Init,
    .reset = Disk_ABI_Reset,
    .shutdown = Disk_ABI_Shutdown,
    .think = Disk_ABI_Think,
    .on_vblank = nullptr,
    .save_state = Disk_ABI_SaveState,
    .load_state = Disk_ABI_LoadState,
    .command = Disk_ABI_Command,
    .query = Disk_ABI_Query};

PERIPHERAL_REGISTER(g_disk_peripheral)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory,
// cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
