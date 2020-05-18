#pragma once

extern bool g_bHD_Enabled;

bool HD_CardIsEnabled();

void HD_SetEnabled(bool bEnabled);

LPCTSTR HD_GetFullName(int drive);

void HD_Load_Rom(LPBYTE pCxRomPeripheral, unsigned int uSlot);

void HD_Cleanup();

bool HD_InsertDisk2(int nDrive, LPCTSTR pszFilename);

bool HD_InsertDisk(int nDrive, LPCTSTR imagefilename);

void HD_Select(int nDrive);

void HD_FTP_Select(int nDrive);

int HD_GetStatus(void);

void HD_ResetStatus(void);
