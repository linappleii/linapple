#pragma once

#define  DRIVE_1  0
#define  DRIVE_2  1

#define  DRIVES   2
#define  TRACKS   35

extern bool enhancedisk;

void DiskInitialize(); // DiskManagerStartup()
void DiskDestroy(); // no, doesn't "destroy" the disk image.  DiskManagerShutdown()

void DiskBoot();

void DiskEject(const int iDrive);

LPCTSTR DiskGetFullName(int);

enum Disk_Status_e {
  DISK_STATUS_OFF, DISK_STATUS_READ, DISK_STATUS_WRITE, DISK_STATUS_PROT, NUM_DISK_STATUS
};

void DiskGetLightStatus(int *pDisk1Status_, int *pDisk2Status_);

LPCTSTR DiskGetName(int);

int DiskInsert(int, LPCTSTR, bool, bool);

bool DiskIsSpinning();

void DiskNotifyInvalidImage(LPCTSTR, int);

void DiskReset();

bool DiskGetProtect(const int iDrive);

void DiskSetProtect(const int iDrive, const bool bWriteProtect);

void DiskSelect(int);

void Disk_FTP_SelectImage(int);

void DiskUpdatePosition(unsigned int);

bool DiskDriveSwap();

void DiskLoadRom(LPBYTE pCxRomPeripheral, unsigned int uSlot);

unsigned int DiskGetSnapshot(SS_CARD_DISK2 *pSS, unsigned int dwSlot);

unsigned int DiskSetSnapshot(SS_CARD_DISK2 *pSS, unsigned int dwSlot);
