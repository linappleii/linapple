#pragma once

#include <cstdint>

void PrinterFrontend_Initialize();
void PrinterFrontend_Destroy();
void PrinterFrontend_Reset();
void PrinterFrontend_Update(uint32_t totalcycles);
void PrinterFrontend_SendChar(uint8_t c);
void PrinterFrontend_CheckStatus();

void Printer_SetIdleLimit(uint32_t Duration);
auto Printer_GetIdleLimit() -> uint32_t;

extern bool g_bPrinterAppend;
