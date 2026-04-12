#include "core/Common.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <sys/stat.h>

#include "apple2/Harddisk.h"
#include "apple2/Disk.h"
#include "apple2/Memory.h"
#include "apple2/CPU.h"
#include "core/Log.h"
#include "core/Common_Globals.h"
#include "core/Util_Text.h"
#include "core/Util_Path.h"
#include "core/Registry.h"

extern void FrameRefreshStatus(int);

char Hddrvr_dat[] = "\xA9\x20\xA9\x00\xA9\x03\xA9\x3C\xA9\x00\x8D\xF2\xC0\xA9\x70\x8D"
                    "\xF3\xC0\xAD\xF0\xC0\x48\xAD\xF1\xC0\x18\xC9\x01\xD0\x01\x38\x68"
                    "\x90\x03\x4C\x00\xC6\xA9\x70\x85\x43\xA9\x00\x85\x44\x85\x46\x85"
                    "\x47\xA9\x08\x85\x45\xA9\x01\x85\x42\x20\x46\xC7\x90\x03\x4C\x00"
                    "\xC6\xA2\x70\x4C\x01\x08\x18\xA5\x42\x8D\xF2\xC0\xA5\x43\x8D\xF3"
                    "\xC0\xA5\x44\x8D\xF4\xC0\xA5\x45\x8D\xF5\xC0\xA5\x46\x8D\xF6\xC0"
                    "\xA5\x47\x8D\xF7\xC0\xAD\xF0\xC0\x48\xA5\x42\xC9\x01\xD0\x03\x20"
                    "\x7D\xC7\xAD\xF1\xC0\x18\xC9\x01\xD0\x01\x38\x68\x60\x98\x48\xA0"
                    "\x00\xAD\xF8\xC0\x91\x44\xC8\xD0\xF8\xE6\x45\xA0\x00\xAD\xF8\xC0"
                    "\x91\x44\xC8\xD0\xF8\x68\xA8\x60\x00\x00\x00\x00\x00\x00\x00\x00"
                    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\x7F\xD7\x46";


typedef struct {
  char hd_imagename[16];
  char hd_fullname[128];
  unsigned char hd_error;
  unsigned short hd_memblock;
  unsigned short hd_diskblock;
  unsigned short hd_buf_ptr;
  bool hd_imageloaded;
  FILE* hd_file;
  unsigned char hd_buf[513];
} HDD, *PHDD;

static bool g_bHD_RomLoaded = false;
bool g_bHD_Enabled = false;

static unsigned char g_nHD_UnitNum = DRIVE_1;
static unsigned char g_nHD_Command;
static HDD g_HardDrive[2] = {};
static unsigned int g_uSlot = 7;
static int HDDStatus = DISK_STATUS_OFF;

int HD_GetStatus(void) { return HDDStatus; }
void HD_ResetStatus(void) { HDDStatus = DISK_STATUS_OFF; }

static void GetImageTitle(const char* imageFileName, PHDD pHardDrive)
{
  char imagetitle[128];
  const char* startpos = imageFileName;

  if (strrchr(startpos, FILE_SEPARATOR)) {
    startpos = strrchr(startpos, FILE_SEPARATOR) + 1;
  }
  Util_SafeStrCpy(imagetitle, startpos, 127);
  imagetitle[127] = 0;

  bool found = false;
  int loop = 0;
  while (imagetitle[loop] && !found) {
    if (IsCharLower(imagetitle[loop])) found = true;
    else loop++;
  }

  Util_SafeStrCpy(pHardDrive->hd_fullname, imagetitle, 127);

  if (imagetitle[0]) {
    char* dot = strrchr(imagetitle, '.');
    if (dot && dot > imagetitle) *dot = 0;
  }

  Util_SafeStrCpy(pHardDrive->hd_imagename, imagetitle, 15);
}

static void NotifyInvalidImage(char *filename) { printf("HDD: Could not load %s\n", filename); }

static size_t Util_GetFileSize(FILE* f) {
  long current = ftell(f);
  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  fseek(f, current, SEEK_SET);
  return size;
}

static void HD_CleanupDrive(int nDrive)
{
  if (g_HardDrive[nDrive].hd_file) {
    fclose(g_HardDrive[nDrive].hd_file);
    g_HardDrive[nDrive].hd_file = NULL;
  }
  g_HardDrive[nDrive].hd_imageloaded = false;
  g_HardDrive[nDrive].hd_imagename[0] = 0;
  g_HardDrive[nDrive].hd_fullname[0] = 0;
}

static bool HD_Load_Image(int nDrive, const char *filename)
{
  g_HardDrive[nDrive].hd_file = fopen(filename, "r+b");
  g_HardDrive[nDrive].hd_imageloaded = g_HardDrive[nDrive].hd_file != NULL;
  return g_HardDrive[nDrive].hd_imageloaded;
}

static unsigned char HD_IO_EMUL(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft);

bool HD_CardIsEnabled() { return g_bHD_RomLoaded && g_bHD_Enabled; }

void HD_SetEnabled(bool bEnabled) {
  if (g_bHD_Enabled == bEnabled) return;
  g_bHD_Enabled = bEnabled;
  uint8_t* pCxRomPeripheral = MemGetCxRomPeripheral();
  if (pCxRomPeripheral == NULL) return;
  if (g_bHD_Enabled) HD_Load_Rom(pCxRomPeripheral, g_uSlot);
  else memset(pCxRomPeripheral + g_uSlot * 256, 0, 0x100);
  RegisterIoHandler(g_uSlot, HD_IO_EMUL, HD_IO_EMUL, NULL, NULL, NULL, NULL);
}

const char* HD_GetFullName(int nDrive) { return g_HardDrive[nDrive].hd_fullname; }

void HD_Load_Rom(uint8_t* pCxRomPeripheral, unsigned int uSlot) {
  if (!g_bHD_Enabled) return;
  g_uSlot = uSlot;
  memcpy(pCxRomPeripheral + uSlot * 256, Hddrvr_dat, 0x100);
  g_bHD_RomLoaded = true;
}

void HD_Cleanup() {
  for (int i = 0; i < 2; i++) HD_CleanupDrive(i);
}

void HD_Eject(const int iDrive) {
  if (g_HardDrive[iDrive].hd_imageloaded) {
    HD_CleanupDrive(iDrive);
    if (iDrive == 0) {
      Configuration::Instance().SetString("Preferences", REGVALUE_HDD_IMAGE1, "");
    } else {
      Configuration::Instance().SetString("Preferences", REGVALUE_HDD_IMAGE2, "");
    }
    Configuration::Instance().Save();
  }
}

bool HD_InsertDisk(int nDrive, const char* imageFileName) {
  if (!imageFileName || *imageFileName == 0x00) return false;
  if (g_HardDrive[nDrive].hd_imageloaded) HD_CleanupDrive(nDrive);
  bool result = HD_Load_Image(nDrive, imageFileName);
  if (result) GetImageTitle(imageFileName, &g_HardDrive[nDrive]);
  else NotifyInvalidImage((char *) imageFileName);
  return result;
}

bool HD_InsertDisk2(int nDrive, const char* pszFilename) {
  return HD_InsertDisk(nDrive, pszFilename);
}

#define DEVICE_OK        0x00
#define DEVICE_UNKNOWN_ERROR  0x03
#define DEVICE_IO_ERROR      0x08

static unsigned char HD_IO_EMUL(unsigned short pc, unsigned short addr, unsigned char bWrite, unsigned char d, uint32_t nCyclesLeft) {
  unsigned char r = DEVICE_OK;
  addr &= 0xFF;
  if (!HD_CardIsEnabled()) return r;
  PHDD pHDD = &g_HardDrive[g_nHD_UnitNum >> 7];
  if (bWrite == 0) { // read
    switch (addr) {
      case 0xF0: {
        if (pHDD->hd_imageloaded) {
          switch (g_nHD_Command) {
            default:
            case 0x00: //status
              if (Util_GetFileSize(pHDD->hd_file) == 0) {
                pHDD->hd_error = 1;
                r = DEVICE_IO_ERROR;
              }
              break;
            case 0x01: //read
            {
              HDDStatus = DISK_STATUS_READ;
              size_t br = Util_GetFileSize(pHDD->hd_file);
              if ((size_t)(pHDD->hd_diskblock * 512) <= br) {
                fseek(pHDD->hd_file, pHDD->hd_diskblock * 512, SEEK_SET);
                if (fread(pHDD->hd_buf, 1, 512, pHDD->hd_file) == 512) {
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
            }
              break;
            case 0x02: //write
            {
              HDDStatus = DISK_STATUS_WRITE;
              size_t bw = Util_GetFileSize(pHDD->hd_file);
              if ((size_t)(pHDD->hd_diskblock * 512) <= bw) {
                memmove(pHDD->hd_buf,  mem + pHDD->hd_memblock,  512);
                fseek(pHDD->hd_file, pHDD->hd_diskblock * 512, SEEK_SET);
                if (fwrite(pHDD->hd_buf, 1, 512, pHDD->hd_file) == 512) {
                  pHDD->hd_error = 0;
                  r = 0;
                } else {
                  pHDD->hd_error = 1;
                  r = DEVICE_IO_ERROR;
                }
              } else {
                fseek(pHDD->hd_file, 0, SEEK_END);
                size_t fsize = ftell(pHDD->hd_file);
                unsigned int addblocks = pHDD->hd_diskblock - (fsize / 512);
                memset(pHDD->hd_buf,  0,  512);
                while (addblocks--) fwrite(pHDD->hd_buf, 1, 512, pHDD->hd_file);
                if (fseek(pHDD->hd_file, pHDD->hd_diskblock * 512, SEEK_SET) == 0) {
                  memmove(pHDD->hd_buf,  mem + pHDD->hd_memblock,  512);
                  if (fwrite(pHDD->hd_buf, 1, 512, pHDD->hd_file) == 512) {
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
              HDDStatus = DISK_STATUS_WRITE;
              break;
          }
        } else {
          HDDStatus = DISK_STATUS_OFF;
          pHDD->hd_error = 1;
          r = DEVICE_UNKNOWN_ERROR;
        }
      }
        break;
      case 0xF1: r = pHDD->hd_error; break;
      case 0xF2: r = g_nHD_Command; break;
      case 0xF3: r = g_nHD_UnitNum; break;
      case 0xF4: r = (unsigned char)(pHDD->hd_memblock & 0x00FF); break;
      case 0xF5: r = (unsigned char)((pHDD->hd_memblock & 0xFF00) >> 8); break;
      case 0xF6: r = (unsigned char)(pHDD->hd_diskblock & 0x00FF); break;
      case 0xF7: r = (unsigned char)((pHDD->hd_diskblock & 0xFF00) >> 8); break;
      case 0xF8: r = pHDD->hd_buf[pHDD->hd_buf_ptr]; pHDD->hd_buf_ptr++; break;
      default: return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
    }
  } else { // write
    switch (addr) {
      case 0xF2: g_nHD_Command = d; break;
      case 0xF3: g_nHD_UnitNum = d; break;
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
