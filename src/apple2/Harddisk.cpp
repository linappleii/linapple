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

#include "apple2/Harddisk.h"

#include <sys/stat.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/CPU.h"
#include "apple2/DiskCommands.h"
#include "apple2/DiskFTP.h"
#include "apple2/Memory.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "apple2/HarddiskFormatDriver.h"
#include "apple2/HarddiskLoader.h"

extern void FrameRefreshStatus(int);

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

struct HarddiskDrive_t {
  char hd_imagename[16];
  char hd_fullname[128];
  uint8_t hd_error;
  uint16_t hd_memblock;
  uint16_t hd_diskblock;
  uint16_t hd_buf_ptr;
  bool hd_imageloaded;
  FilePtr hd_file; // Keep for now until Phase 3 (Raw driver extraction)
  uint8_t hd_buf[513];

  HarddiskDrive_t()
      : hd_error(0),
        hd_memblock(0),
        hd_diskblock(0),
        hd_buf_ptr(0),
        hd_imageloaded(false),
        hd_file(nullptr, fclose) {
    memset(hd_imagename, 0, sizeof(hd_imagename));
    memset(hd_fullname, 0, sizeof(hd_fullname));
    memset(hd_buf, 0, sizeof(hd_buf));
  }
};

struct HarddiskPeripheral_t {
  HarddiskDrive_t drives[2];
  uint8_t unit_num;
  uint8_t command;
  bool rom_loaded;
  bool enabled;
  uint32_t slot;
  int status;
  HostInterface_t* host;

  HarddiskPeripheral_t()
      : unit_num(DRIVE_1),
        command(0),
        rom_loaded(false),
        enabled(false),
        slot(7),
        status(DISK_STATUS_OFF),
        host(nullptr) {}
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static HarddiskPeripheral_t* g_pHDInstance = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

auto HD_GetStatus() -> int {
  return g_pHDInstance ? g_pHDInstance->status : DISK_STATUS_OFF;
}

void HD_ResetStatus() {
  if (g_pHDInstance) g_pHDInstance->status = DISK_STATUS_OFF;
}

static void GetImageTitle(const char* imageFileName, HarddiskDrive_t* pHardDrive)
{
  char imagetitle[128];
  const char* startpos = imageFileName;

  const char* last_sep = strrchr(startpos, FILE_SEPARATOR);
  if (last_sep) {
    startpos = last_sep + 1;
  }
  Util_SafeStrCpy(imagetitle, startpos, 127);
  imagetitle[127] = 0;

  bool found = false;
  int loop = 0;
  while (imagetitle[loop] && !found) {
    if (IsCharLower(imagetitle[loop])) {
      found = true;
    } else {
      loop++;
    }
  }

  Util_SafeStrCpy(pHardDrive->hd_fullname, imagetitle, 127);

  if (imagetitle[0]) {
    char* dot = strrchr(imagetitle, '.');
    if (dot && dot > imagetitle) *dot = 0;
  }

  Util_SafeStrCpy(pHardDrive->hd_imagename, imagetitle, 15);
}

static void NotifyInvalidImage(const char *filename) { printf("HDD: Could not load %s\n", filename); }

static auto Util_GetFileSize(FILE* f) -> size_t {
  long current = ftell(f);
  fseek(f, 0, SEEK_END);
  size_t size = static_cast<size_t>(ftell(f));
  fseek(f, current, SEEK_SET);
  return size;
}

static void HD_CleanupDrive(HarddiskDrive_t* pDrive)
{
  pDrive->hd_file.reset();
  pDrive->hd_imageloaded = false;
  pDrive->hd_imagename[0] = 0;
  pDrive->hd_fullname[0] = 0;
}

static auto HD_Load_Image(HarddiskDrive_t* pDrive, const char *filename) -> bool
{
  pDrive->hd_file.reset(fopen(filename, "r+b"));
  pDrive->hd_imageloaded = pDrive->hd_file != nullptr;
  return pDrive->hd_imageloaded;
}

static auto HD_IO_EMUL(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft) -> uint8_t;

static auto HD_ABI_Init(int slot, HostInterface_t* host) -> void* {
  auto* hp = new HarddiskPeripheral_t();
  hp->host = host;
  hp->slot = static_cast<uint32_t>(slot);
  hp->enabled = true; // Default to enabled for now to maintain existing behavior

  HarddiskLoader_Init();

  if (hp->enabled) {
    uint8_t slot_rom[256];
    memcpy(slot_rom, Hddrvr_dat, 256);
    host->RegisterCxROM(slot, slot_rom);
    hp->rom_loaded = true;
  }

  host->RegisterIO(slot, HD_IO_EMUL, HD_IO_EMUL, nullptr, nullptr);

  g_pHDInstance = hp;
  return hp;
}

static void HD_ABI_Reset(void* instance) { (void)instance; }

static void HD_ABI_Shutdown(void* instance) {
  if (!instance) return;
  auto* hp = static_cast<HarddiskPeripheral_t*>(instance);
  for (int i = 0; i < 2; i++) HD_CleanupDrive(&hp->drives[i]);
  HarddiskLoader_Shutdown();
  if (g_pHDInstance == hp) g_pHDInstance = nullptr;
  delete hp;
}

Peripheral_t g_harddisk_peripheral = {
    LINAPPLE_ABI_VERSION,
    "Harddisk",
    0xFE,  // Slots 1-7
    HD_ABI_Init,
    HD_ABI_Reset,
    HD_ABI_Shutdown,
    nullptr,  // think
    nullptr,  // on_vblank
    nullptr,  // save_state
    nullptr,  // load_state
    nullptr,  // command
    nullptr   // query
};

extern "C" void Register_Harddisk() {
  Peripheral_Register_Builtin(&g_harddisk_peripheral);
}

#ifdef BUILD_SHARED_PERIPHERAL
EXPORT_PERIPHERAL(g_harddisk_peripheral)
#endif

auto HD_CardIsEnabled() -> bool {
  return g_pHDInstance && g_pHDInstance->rom_loaded && g_pHDInstance->enabled;
}

void HD_SetEnabled(bool bEnabled) {
  if (!g_pHDInstance) return;
  if (g_pHDInstance->enabled == bEnabled) return;
  g_pHDInstance->enabled = bEnabled;
  if (!g_pHDInstance->host) return;

  if (g_pHDInstance->enabled) {
    uint8_t slot_rom[256];
    memcpy(slot_rom, Hddrvr_dat, 256);
    g_pHDInstance->host->RegisterCxROM(static_cast<int>(g_pHDInstance->slot), slot_rom);
    g_pHDInstance->rom_loaded = true;
  } else {
    uint8_t empty_rom[256] = {0};
    g_pHDInstance->host->RegisterCxROM(static_cast<int>(g_pHDInstance->slot), empty_rom);
    g_pHDInstance->rom_loaded = false;
  }
}

auto HD_GetFullName(int nDrive) -> const char* {
  if (!g_pHDInstance || nDrive < 0 || nDrive >= 2) return "";
  return g_pHDInstance->drives[nDrive].hd_fullname;
}

void HD_Cleanup() {
  if (g_pHDInstance) {
    for (int i = 0; i < 2; i++) HD_CleanupDrive(&g_pHDInstance->drives[i]);
  }
}

void HD_Eject(const int iDrive) {
  if (!g_pHDInstance || iDrive < 0 || iDrive >= 2) return;
  HarddiskDrive_t* pDrive = &g_pHDInstance->drives[iDrive];

  if (pDrive->hd_imageloaded) {
    HD_CleanupDrive(pDrive);
    if (iDrive == 0) {
      Configuration::Instance().SetString("Preferences", "Harddisk Image 1",
                                          "");
    } else {
      Configuration::Instance().SetString("Preferences", "Harddisk Image 2",
                                          "");
    }
    Configuration::Instance().Save();
  }
}

auto HD_InsertDisk(int nDrive, const char* imageFileName) -> bool {
  if (!g_pHDInstance || nDrive < 0 || nDrive >= 2) return false;
  if (!imageFileName || *imageFileName == 0x00) return false;

  HarddiskDrive_t* pDrive = &g_pHDInstance->drives[nDrive];
  if (pDrive->hd_imageloaded) HD_CleanupDrive(pDrive);

  bool result = HD_Load_Image(pDrive, imageFileName);
  if (result) {
    GetImageTitle(imageFileName, pDrive);
  } else {
    NotifyInvalidImage(imageFileName);
  }
  return result;
}

auto HD_InsertDisk2(int nDrive, const char* pszFilename) -> bool {
  return HD_InsertDisk(nDrive, pszFilename);
}

enum { DEVICE_OK = 0x00, DEVICE_UNKNOWN_ERROR = 0x03, DEVICE_IO_ERROR = 0x08 };

static auto HD_IO_EMUL(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  if (!instance) return DEVICE_UNKNOWN_ERROR;
  auto* hp = static_cast<HarddiskPeripheral_t*>(instance);

  uint8_t r = DEVICE_OK;
  addr &= 0xFF;
  if (!hp->rom_loaded || !hp->enabled) return IO_Null(pc, addr, bWrite, d, nCyclesLeft);

  HarddiskDrive_t* pHDD = &hp->drives[hp->unit_num >> 7];

  if (bWrite == 0) { // read
    switch (addr) {
      case 0xF0: {
        if (pHDD->hd_imageloaded) {
          switch (hp->command) {
            default:
            case 0x00:  // status
              if (Util_GetFileSize(pHDD->hd_file.get()) == 0) {
                pHDD->hd_error = 1;
                r = DEVICE_IO_ERROR;
              }
              break;
            case 0x01:  // read
            {
              hp->status = DISK_STATUS_READ;
              size_t br = Util_GetFileSize(pHDD->hd_file.get());
              if (static_cast<size_t>(pHDD->hd_diskblock * 512) <= br) {
                fseek(pHDD->hd_file.get(),
                      static_cast<long>(pHDD->hd_diskblock * 512), SEEK_SET);
                if (fread(pHDD->hd_buf, 1, 512, pHDD->hd_file.get()) == 512) {
                  pHDD->hd_error = 0;
                  r = 0;
                  pHDD->hd_buf_ptr = 0;
                } else {
                  pHDD->hd_error = 1;
                  r = DEVICE_IO_ERROR;
                }
              } else {
                pHDD->hd_error = 1;
                r = DEVICE_IO_ERROR;
              }
            } break;
            case 0x02:  // write
            {
              hp->status = DISK_STATUS_WRITE;
              size_t bw = Util_GetFileSize(pHDD->hd_file.get());
              if (static_cast<size_t>(pHDD->hd_diskblock * 512) <= bw) {
                memmove(pHDD->hd_buf, mem + pHDD->hd_memblock, 512);
                fseek(pHDD->hd_file.get(),
                      static_cast<long>(pHDD->hd_diskblock * 512), SEEK_SET);
                if (fwrite(pHDD->hd_buf, 1, 512, pHDD->hd_file.get()) == 512) {
                  pHDD->hd_error = 0;
                  r = 0;
                } else {
                  pHDD->hd_error = 1;
                  r = DEVICE_IO_ERROR;
                }
              } else {
                fseek(pHDD->hd_file.get(), 0, SEEK_END);
                size_t fsize = ftell(pHDD->hd_file.get());
                uint32_t addblocks = pHDD->hd_diskblock - (fsize / 512);
                memset(pHDD->hd_buf, 0, 512);
                while (addblocks--)
                  fwrite(pHDD->hd_buf, 1, 512, pHDD->hd_file.get());
                if (fseek(pHDD->hd_file.get(),
                          static_cast<long>(pHDD->hd_diskblock * 512),
                          SEEK_SET) == 0) {
                  memmove(pHDD->hd_buf, mem + pHDD->hd_memblock, 512);
                  if (fwrite(pHDD->hd_buf, 1, 512, pHDD->hd_file.get()) ==
                      512) {
                    pHDD->hd_error = 0;
                    r = 0;
                  } else {
                    pHDD->hd_error = 1;
                    r = DEVICE_IO_ERROR;
                  }
                }
              }
            }
              break;
            case 0x03: //format
              hp->status = DISK_STATUS_WRITE;
              break;
          }
        } else {
          hp->status = DISK_STATUS_OFF;
          pHDD->hd_error = 1;
          r = DEVICE_UNKNOWN_ERROR;
        }
      } break;
      case 0xF1: r = pHDD->hd_error; break;
      case 0xF2: r = hp->command; break;
      case 0xF3: r = hp->unit_num; break;
      case 0xF4: r = static_cast<uint8_t>(pHDD->hd_memblock & 0x00FF); break;
      case 0xF5: r = static_cast<uint8_t>((pHDD->hd_memblock & 0xFF00) >> 8); break;
      case 0xF6: r = static_cast<uint8_t>(pHDD->hd_diskblock & 0x00FF); break;
      case 0xF7: r = static_cast<uint8_t>((pHDD->hd_diskblock & 0xFF00) >> 8); break;
      case 0xF8: r = pHDD->hd_buf[pHDD->hd_buf_ptr]; pHDD->hd_buf_ptr++; break;
      default: return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    }
  } else {  // write
    switch (addr) {
      case 0xF2: hp->command = d; break;
      case 0xF3: hp->unit_num = d; break;
      case 0xF4: pHDD->hd_memblock = (pHDD->hd_memblock & 0xFF00) | d; break;
      case 0xF5: pHDD->hd_memblock = (pHDD->hd_memblock & 0x00FF) | (d << 8); break;
      case 0xF6: pHDD->hd_diskblock = (pHDD->hd_diskblock & 0xFF00) | d; break;
      case 0xF7: pHDD->hd_diskblock = (pHDD->hd_diskblock & 0x00FF) | (d << 8); break;
      default: return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    }
  }
  FrameRefreshStatus(DRAW_LEDS);
  return r;
}
