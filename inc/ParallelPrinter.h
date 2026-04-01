#include <cstdint>
#pragma once

void PrintDestroy();

void PrintLoadRom(uint8_t* pCxRomPeripheral, unsigned int uSlot);

void PrintReset();

void PrintUpdate(unsigned int);

void Printer_SetIdleLimit(unsigned int Duration);

unsigned int Printer_GetIdleLimit();

extern bool g_bPrinterAppend;
