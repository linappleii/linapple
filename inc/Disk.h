#include <cstdint>
#pragma once

constexpr int DRIVE_1 = 0;
constexpr int DRIVE_2 = 1;

constexpr int DRIVES = 2;
constexpr int TRACKS = 35;

extern bool enhancedisk;

void DiskInitialize();
void DiskDestroy();

void DiskBoot();

void DiskEject(const int iDrive);

const char* DiskGetFullName(int);

enum Disk_Status_e {
  DISK_STATUS_OFF, DISK_STATUS_READ, DISK_STATUS_WRITE, DISK_STATUS_PROT, NUM_DISK_STATUS
};

void DiskGetLightStatus(int *pDisk1Status_, int *pDisk2Status_);

const char* DiskGetName(int);

int DiskInsert(int, const char*, bool, bool);

bool DiskIsSpinning();

void DiskNotifyInvalidImage(const char*, int);

void DiskReset();

bool DiskGetProtect(const int iDrive);

void DiskSetProtect(const int iDrive, const bool bWriteProtect);

void DiskSelect(int);

void Disk_FTP_SelectImage(int);

void DiskUpdatePosition(unsigned int);

bool DiskDriveSwap();

void DiskLoadRom(uint8_t* pCxRomPeripheral, unsigned int uSlot);

unsigned int DiskGetSnapshot(SS_CARD_DISK2 *pSS, unsigned int dwSlot);

unsigned int DiskSetSnapshot(SS_CARD_DISK2 *pSS, unsigned int dwSlot);
