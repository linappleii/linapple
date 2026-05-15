// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
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

/* Description: Harddisk hardware emulation (Core)
 */

#include "apple2/peripherals/harddisk/Harddisk.h"

#include <sys/stat.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskFTP.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

static const char Hddrvr_dat[] =
    "\xA9\x20\xA9\x00\xA9\x03\xA9\x3C\xA9\x00\x8D\xF2\xC0\xA9\x70\x8D"
    "\xF3\xC0\xAD\xF0\xC0\x48\xAD\xF1\xC0\x18\xC9\x01\xD0\x01\x38\x68"
    "\x90\x03\x4C\x00\xC6\xA9\x70\x85\x43\xA9\x00\x85\x44\x85\x46\x85"
    "\x47\xA9\x08\x85\x45\xA9\x01\x85\x42\x20\x46\xC7\x90\x03\x4C\x00"
    "\xC6\xA2\x70\x4C\x01\x08\x18\xA5\x42\x8D\xF2\xC0\xA5\x43\x8D\xF3"
    "\xC0\xA5\x44\x8D\xF4\xC0\xA5\x45\x8D\xF6\xC0\xA5\x47\x8D\xF7\xC0"
    "\xAD\xF0\xC0\x48\xA5\x42\xC9\x01\xD0\x03\x20\x7D\xC7\xAD\xF1\xC0"
    "\x18\xC9\x01\xD0\x01\x38\x68\x60\x98\x48\xA0\x00\xAD\xF8\xC0"
    "\x91\x44\xC8\xD0\xF8\xE6\x45\xA0\x00\xAD\xF8\xC0\x91\x44\xC8\xD0"
    "\xF8\x68\xA8\x60\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\x7F\xD7\x46";

constexpr size_t HD_IMAGE_NAME_MAX = 16;
constexpr size_t HD_FULL_NAME_MAX = 128;
constexpr size_t HD_BUFFER_SIZE = 513;

constexpr uint8_t HD_REG_CMD_EXEC = 0xF0;
constexpr uint8_t HD_REG_ERROR = 0xF1;
constexpr uint8_t HD_REG_COMMAND = 0xF2;
constexpr uint8_t HD_REG_UNIT = 0xF3;
constexpr uint8_t HD_REG_MEMBLOCK_LO = 0xF4;
constexpr uint8_t HD_REG_MEMBLOCK_HI = 0xF5;
constexpr uint8_t HD_REG_DISKBLOCK_LO = 0xF6;
constexpr uint8_t HD_REG_DISKBLOCK_HI = 0xF7;
constexpr uint8_t HD_REG_BUFFER = 0xF8;

namespace {
constexpr uint16_t HD_IO_ADDR_BASE = 0xC0F0;
constexpr uint16_t HD_IO_ADDR_END = 0xC0F8;
constexpr uint8_t HD_IO_ADDR_HI_MASK = 0xFF;
constexpr int HD_BLOCK_SIZE = 512;
constexpr int HD_ROM_SIZE = 256;
constexpr int HD_DEFAULT_SLOT = 7;
constexpr int HD_PATH_MAX = 512;

constexpr uint8_t HD_MASK_LO = 0xFF;
constexpr uint16_t HD_MASK_HI = 0xFF00;
constexpr int HD_SHIFT_8 = 8;
constexpr int HD_UNIT_NUM_DRIVE_BIT = 7;
}  // namespace

// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum HarddiskIOCommand_e {
  HD_IO_CMD_STATUS = 0x00,
  HD_IO_CMD_READ = 0x01,
  HD_IO_CMD_WRITE = 0x02,
  HD_IO_CMD_FORMAT = 0x03
};

struct HarddiskDrive_t {
  char hd_imagename[HD_IMAGE_NAME_MAX]{};
  char hd_fullname[HD_FULL_NAME_MAX]{};
  uint8_t hd_error = 0;
  uint16_t hd_memblock = 0;
  uint16_t hd_diskblock = 0;
  uint16_t hd_buf_ptr = 0;
  bool hd_imageloaded = false;
  HarddiskFormatDriver_t* driver = nullptr;
  void* driver_instance = nullptr;
  bool os_readonly = false;
  bool user_write_protected = false;
  HarddiskError_e last_error = HARDDISK_ERR_NONE;
  uint8_t hd_buf[HD_BUFFER_SIZE]{};

  HarddiskDrive_t() = default;
};

struct HarddiskPeripheral_t {
  HarddiskDrive_t drives[2];
  uint8_t unit_num = DRIVE_1;
  uint8_t command = 0;
  bool rom_loaded = false;
  bool enabled = false;
  uint32_t slot = static_cast<uint32_t>(HD_DEFAULT_SLOT);
  int status = DISK_STATUS_OFF;
  HostInterface_t* host = nullptr;

  HarddiskPeripheral_t() = default;
};

static void GetImageTitle(const char* imageFileName,
                          HarddiskDrive_t* pHardDrive) {
  char imagetitle[HD_FULL_NAME_MAX];
  const char* startpos = imageFileName;

  const char* last_sep = strrchr(startpos, FILE_SEPARATOR);
  if (last_sep != nullptr) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    startpos = last_sep + 1;
  }
  Util_SafeStrCpy(imagetitle, startpos, HD_FULL_NAME_MAX - 1);
  imagetitle[HD_FULL_NAME_MAX - 1] = 0;

  bool found = false;
  int loop = 0;
  while (imagetitle[loop] && !found) {
    if (IsCharLower(imagetitle[loop])) {
      found = true;
    } else {
      loop++;
    }
  }

  Util_SafeStrCpy(pHardDrive->hd_fullname, imagetitle, HD_FULL_NAME_MAX - 1);

  if (imagetitle[0]) {
    char* dot = strrchr(imagetitle, '.');
    if (dot && dot > imagetitle) *dot = 0;
  }

  Util_SafeStrCpy(pHardDrive->hd_imagename, imagetitle, HD_IMAGE_NAME_MAX - 1);
}

static void NotifyInvalidImage(const char* filename) {
  printf("HDD: Could not load %s\n", filename);
}

static auto IsDriveValid(const int iDrive) -> bool {
  return (iDrive >= 0 && iDrive < HARDDISK_DRIVE_COUNT);
}

static void HD_CleanupDrive(HarddiskDrive_t* pDrive) {
  if (pDrive->driver && pDrive->driver_instance) {
    pDrive->driver->close(pDrive->driver_instance);
  }
  pDrive->driver = nullptr;
  pDrive->driver_instance = nullptr;
  pDrive->hd_imageloaded = false;
  pDrive->hd_imagename[0] = 0;
  pDrive->hd_fullname[0] = 0;
  pDrive->os_readonly = false;
  pDrive->user_write_protected = false;
  pDrive->last_error = HARDDISK_ERR_NONE;
}

static auto HD_Insert_Internal(HarddiskPeripheral_t* hp, int drive,
                               const char* imageFileName, bool writeProtected)
    -> HarddiskError_e {
  if (!IsDriveValid(drive)) return HARDDISK_ERR_IO;
  HarddiskDrive_t* pDrive = &hp->drives[drive];

  if (pDrive->hd_imageloaded) {
    HD_CleanupDrive(pDrive);
  }

  pDrive->user_write_protected = writeProtected;
  HarddiskError_e error =
      HarddiskLoader_Open(imageFileName, &pDrive->os_readonly, &pDrive->driver,
                          &pDrive->driver_instance);

  pDrive->last_error = error;

  if (error == HARDDISK_ERR_NONE) {
    pDrive->hd_imageloaded = true;
    GetImageTitle(imageFileName, pDrive);
    if (hp->host) {
      const char* key = (drive == 0) ? "Harddisk Image 1" : "Harddisk Image 2";
      hp->host->SetConfig("Preferences", key, imageFileName);
      hp->host->NotifyStatusChanged(static_cast<int>(hp->slot));
    }
  } else {
    NotifyInvalidImage(imageFileName);
    if (hp->host) {
      hp->host->NotifyStatusChanged(static_cast<int>(hp->slot));
    }
  }
  return error;
}

static auto HD_IO_EMUL(void* instance, uint16_t pc, uint16_t addr,
                       uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t;

static auto HD_ABI_Command(void* instance, uint32_t cmd, const void* data,
                           size_t size) -> PeripheralStatus {
  if (!instance) return PERIPHERAL_ERROR;
  auto* hp = static_cast<HarddiskPeripheral_t*>(instance);

  switch (static_cast<HarddiskCmd_e>(cmd)) {
    case HARDDISK_CMD_INSERT: {
      if (!data || size < sizeof(HarddiskInsertCmd_t)) return PERIPHERAL_ERROR;
      auto* c = static_cast<const HarddiskInsertCmd_t*>(data);
      if (!IsDriveValid(c->drive)) return PERIPHERAL_ERROR;
      HD_Insert_Internal(hp, c->drive, c->path, c->write_protected != 0);
      return PERIPHERAL_OK;
    }
    case HARDDISK_CMD_EJECT: {
      if (!data || size < sizeof(HarddiskEjectCmd_t)) return PERIPHERAL_ERROR;
      auto* c = static_cast<const HarddiskEjectCmd_t*>(data);
      if (!IsDriveValid(c->drive)) return PERIPHERAL_ERROR;
      HD_CleanupDrive(&hp->drives[c->drive]);
      if (hp->host) {
        const char* key =
            (c->drive == 0) ? "Harddisk Image 1" : "Harddisk Image 2";
        hp->host->SetConfig("Preferences", key, "");
        hp->host->NotifyStatusChanged(static_cast<int>(hp->slot));
      }
      return PERIPHERAL_OK;
    }
    case HARDDISK_CMD_SET_PROTECT: {
      if (!data || size < sizeof(HarddiskSetProtectCmd_t)) {
        return PERIPHERAL_ERROR;
      }
      auto* c = static_cast<const HarddiskSetProtectCmd_t*>(data);
      if (!IsDriveValid(c->drive)) return PERIPHERAL_ERROR;
      hp->drives[c->drive].user_write_protected = (c->write_protected != 0);
      if (hp->host) {
        hp->host->NotifyStatusChanged(static_cast<int>(hp->slot));
      }
      return PERIPHERAL_OK;
    }
    case HARDDISK_CMD_RESET_STATUS: {
      hp->status = DISK_STATUS_OFF;
      return PERIPHERAL_OK;
    }
    default:
      return PERIPHERAL_INCOMPATIBLE;
  }
}

static auto HD_ABI_Query(void* instance, uint32_t cmd, void* data, size_t* size)
    -> PeripheralStatus {
  if (!instance || !size) return PERIPHERAL_ERROR;
  auto* hp = static_cast<HarddiskPeripheral_t*>(instance);

  if (cmd == HARDDISK_CMD_GET_STATUS) {
    if (!data || *size < sizeof(HarddiskStatus_t)) {
      *size = sizeof(HarddiskStatus_t);
      return PERIPHERAL_ERROR;
    }
    auto* s = static_cast<HarddiskStatus_t*>(data);
    memset(s, 0, sizeof(HarddiskStatus_t));

    s->drive0_last_error = static_cast<int32_t>(hp->drives[0].last_error);
    s->drive0_loaded = hp->drives[0].hd_imageloaded ? 1 : 0;
    s->drive0_write_protected =
        (hp->drives[0].user_write_protected || hp->drives[0].os_readonly) ? 1
                                                                          : 0;
    Util_SafeStrCpy(s->drive0_name, hp->drives[0].hd_imagename,
                    HARDDISK_STATUS_NAME_MAX);
    Util_SafeStrCpy(s->drive0_full_path, hp->drives[0].hd_fullname,
                    HARDDISK_STATUS_PATH_MAX);

    s->drive1_last_error = static_cast<int32_t>(hp->drives[1].last_error);
    s->drive1_loaded = hp->drives[1].hd_imageloaded ? 1 : 0;
    s->drive1_write_protected =
        (hp->drives[1].user_write_protected || hp->drives[1].os_readonly) ? 1
                                                                          : 0;
    Util_SafeStrCpy(s->drive1_name, hp->drives[1].hd_imagename,
                    HARDDISK_STATUS_NAME_MAX);
    Util_SafeStrCpy(s->drive1_full_path, hp->drives[1].hd_fullname,
                    HARDDISK_STATUS_PATH_MAX);

    s->activity_status = static_cast<uint8_t>(hp->status);

    *size = sizeof(HarddiskStatus_t);
    return PERIPHERAL_OK;
  }
  return PERIPHERAL_INCOMPATIBLE;
}

static auto HD_ABI_Init(int slot, HostInterface_t* host) -> void* {
  auto* hp = new HarddiskPeripheral_t();
  hp->host = host;
  hp->slot = static_cast<uint32_t>(slot);
  hp->enabled = true;

  HarddiskLoader_Init();

  if (hp->enabled) {
    uint8_t slot_rom[HD_ROM_SIZE];
    memcpy(slot_rom, Hddrvr_dat, HD_ROM_SIZE);
    host->RegisterCxROM(slot, slot_rom);
    hp->rom_loaded = true;
  }

  // Register surgical I/O for Slot 7 ($C0F0-$C0F8)
  for (uint16_t addr = HD_IO_ADDR_BASE; addr <= HD_IO_ADDR_END; ++addr) {
    host->RegisterDirectIO(hp, addr, HD_IO_EMUL, HD_IO_EMUL);
  }

  // Auto-load images from config if present
  char path[HD_PATH_MAX];
  if (host->GetConfig("Preferences", "Harddisk Image 1", path, sizeof(path)) &&
      path[0]) {
    HD_Insert_Internal(hp, 0, path, false);
  }
  if (host->GetConfig("Preferences", "Harddisk Image 2", path, sizeof(path)) &&
      path[0]) {
    HD_Insert_Internal(hp, 1, path, false);
  }

  return hp;
}

static void HD_ABI_Reset(void* instance) { (void)instance; }

static void HD_ABI_Shutdown(void* instance) {
  if (!instance) return;
  auto* hp = static_cast<HarddiskPeripheral_t*>(instance);
  for (auto& drive : hp->drives) {
    HD_CleanupDrive(&drive);
  }
  HarddiskLoader_Shutdown();
  delete hp;
}

constexpr uint8_t DEVICE_OK = 0x00;
constexpr uint8_t DEVICE_UNKNOWN_ERROR = 0x03;
constexpr uint8_t DEVICE_IO_ERROR = 0x08;

static auto HD_IO_EMUL(void* instance, uint16_t pc, uint16_t addr,
                       uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t {
  if (!instance) return DEVICE_UNKNOWN_ERROR;
  auto* hp = static_cast<HarddiskPeripheral_t*>(instance);

  uint8_t r = DEVICE_OK;
  addr &= HD_IO_ADDR_HI_MASK;
  if (!hp->rom_loaded || !hp->enabled) {
    return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
  }

  HarddiskDrive_t* pHDD = &hp->drives[hp->unit_num >> HD_UNIT_NUM_DRIVE_BIT];

  if (bWrite == 0) {  // read
    switch (addr) {
      case HD_REG_CMD_EXEC: {
        if (pHDD->hd_imageloaded) {
          switch (static_cast<HarddiskIOCommand_e>(hp->command)) {
            default:
            case HD_IO_CMD_STATUS:  // status
              if (pHDD->driver->get_total_blocks(pHDD->driver_instance) == 0) {
                pHDD->hd_error = 1;
                r = DEVICE_IO_ERROR;
              }
              break;
            case HD_IO_CMD_READ:  // read
            {
              hp->status = DISK_STATUS_READ;
              if (pHDD->driver->read_block(pHDD->driver_instance,
                                           pHDD->hd_diskblock,
                                           pHDD->hd_buf) == HARDDISK_ERR_NONE) {
                pHDD->hd_error = 0;
                r = 0;
                pHDD->hd_buf_ptr = 0;
              } else {
                pHDD->hd_error = 1;
                r = DEVICE_IO_ERROR;
              }
            } break;
            case HD_IO_CMD_WRITE:  // write
            {
              hp->status = DISK_STATUS_WRITE;
              // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
              memmove(pHDD->hd_buf, mem + pHDD->hd_memblock, HD_BLOCK_SIZE);
              if (pHDD->driver->write_block(pHDD->driver_instance,
                                            pHDD->hd_diskblock, pHDD->hd_buf) ==
                  HARDDISK_ERR_NONE) {
                pHDD->hd_error = 0;
                r = 0;
              } else {
                pHDD->hd_error = 1;
                r = DEVICE_IO_ERROR;
              }
            } break;
            case HD_IO_CMD_FORMAT:  // format
              hp->status = DISK_STATUS_WRITE;
              break;
          }
        } else {
          hp->status = DISK_STATUS_OFF;
          pHDD->hd_error = 1;
          r = DEVICE_UNKNOWN_ERROR;
        }
      } break;
      case HD_REG_ERROR:
        r = pHDD->hd_error;
        break;
      case HD_REG_COMMAND:
        r = hp->command;
        break;
      case HD_REG_UNIT:
        r = hp->unit_num;
        break;
      case HD_REG_MEMBLOCK_LO:
        r = static_cast<uint8_t>(pHDD->hd_memblock & HD_MASK_LO);
        break;
      case HD_REG_MEMBLOCK_HI:
        r = static_cast<uint8_t>((pHDD->hd_memblock & HD_MASK_HI) >> HD_SHIFT_8);
        break;
      case HD_REG_DISKBLOCK_LO:
        r = static_cast<uint8_t>(pHDD->hd_diskblock & HD_MASK_LO);
        break;
      case HD_REG_DISKBLOCK_HI:
        r = static_cast<uint8_t>((pHDD->hd_diskblock & HD_MASK_HI) >> HD_SHIFT_8);
        break;
      case HD_REG_BUFFER:
        r = pHDD->hd_buf[pHDD->hd_buf_ptr];
        pHDD->hd_buf_ptr++;
        break;
      default:
        return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    }
  } else {  // write
    switch (addr) {
      case HD_REG_COMMAND:
        hp->command = d;
        break;
      case HD_REG_UNIT:
        hp->unit_num = d;
        break;
      case HD_REG_MEMBLOCK_LO:
        pHDD->hd_memblock =
            static_cast<uint16_t>((pHDD->hd_memblock & HD_MASK_HI) | d);
        break;
      case HD_REG_MEMBLOCK_HI:
        pHDD->hd_memblock =
            static_cast<uint16_t>((pHDD->hd_memblock & HD_MASK_LO) | (d << HD_SHIFT_8));
        break;
      case HD_REG_DISKBLOCK_LO:
        pHDD->hd_diskblock =
            static_cast<uint16_t>((pHDD->hd_diskblock & HD_MASK_HI) | d);
        break;
      case HD_REG_DISKBLOCK_HI:
        pHDD->hd_diskblock =
            static_cast<uint16_t>((pHDD->hd_diskblock & HD_MASK_LO) | (d << HD_SHIFT_8));
        break;
      default:
        return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    }
  }
  if (hp->host) hp->host->NotifyStatusChanged(static_cast<int>(hp->slot));
  return r;
}

Peripheral_t g_harddisk_peripheral = {
    .abi_version      = LINAPPLE_ABI_VERSION,
    .id               = "linapple.harddisk",
    .name             = "Harddisk",
    .description      = "SmartPort hard disk controller emulation",
    .author           = "LinApple Contributors",
    .version          = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot     = HD_DEFAULT_SLOT,
    .init             = HD_ABI_Init,
    .reset            = HD_ABI_Reset,
    .shutdown         = HD_ABI_Shutdown,
    .think            = nullptr,
    .on_vblank        = nullptr,
    .save_state       = nullptr,
    .load_state       = nullptr,
    .command          = HD_ABI_Command,
    .query            = HD_ABI_Query
};

PERIPHERAL_REGISTER(g_harddisk_peripheral)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
