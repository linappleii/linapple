#pragma once

#include <cstdint>

void PrinterFrontend_Initialize();
void PrinterFrontend_Destroy();
void PrinterFrontend_Reset();
void PrinterFrontend_Update(unsigned int totalcycles);
void PrinterFrontend_SendChar(uint8_t c);
void PrinterFrontend_CheckStatus();

void Printer_SetIdleLimit(unsigned int Duration);
unsigned int Printer_GetIdleLimit();

extern bool g_bPrinterAppend;
