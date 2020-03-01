#pragma once

void PrintDestroy();

void PrintLoadRom(LPBYTE pCxRomPeripheral, UINT uSlot);

void PrintReset();

void PrintUpdate(DWORD);

void Printer_SetIdleLimit(unsigned int Duration);

unsigned int Printer_GetIdleLimit();

extern bool g_bPrinterAppend;
