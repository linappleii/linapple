#include <cstdint>
#pragma once

extern bool g_bHD_Enabled;

bool HD_CardIsEnabled();

void HD_SetEnabled(bool bEnabled);

const char* HD_GetFullName(int drive);

void HD_Load_Rom(uint8_t* pCxRomPeripheral, unsigned int uSlot);

void HD_Eject(const int iDrive);

void HD_Cleanup();

bool HD_InsertDisk2(int nDrive, const char* pszFilename);

bool HD_InsertDisk(int nDrive, const char* imagefilename);

void HD_Select(int nDrive);

void HD_FTP_Select(int nDrive);

int HD_GetStatus(void);

void HD_ResetStatus(void);
