#include "Common.h"
#include <cstdio>
#include <cstdint>
#include "PrinterFrontend.h"
#include "Common_Globals.h"
#include "ParallelPrinter.h"

static unsigned int inactivity = 0;
static unsigned int g_PrinterIdleLimit = 10;
static FILE *file = NULL;
bool g_bPrinterAppend = true;

static bool CheckPrint()
{
  inactivity = 0;
  if (file == NULL) {
    file = fopen(g_state.sParallelPrinterFile, (g_bPrinterAppend) ? "ab" : "wb");
  }
  return (file != NULL);
}

static void ClosePrint() {
  if (file != NULL) {
    fclose(file);
    file = NULL;
  }
  inactivity = 0;
}

void PrinterFrontend_Initialize() {
  // Initialization logic if any
}

void PrinterFrontend_Destroy() {
  ClosePrint();
}

void PrinterFrontend_Reset() {
  ClosePrint();
}

void PrinterFrontend_Update(unsigned int totalcycles) {
  if (file == NULL) {
    return;
  }
  if ((inactivity += totalcycles) > (Printer_GetIdleLimit() * 1000 * 1000))
  {
    // inactive, so close the file (next print will overwrite it)
    ClosePrint();
  }
}

void PrinterFrontend_SendChar(uint8_t value) {
  if (!CheckPrint()) {
    return;
  }
  char c = value & 0x7F;
  fwrite(&c, 1, 1, file);
}

void PrinterFrontend_CheckStatus() {
  CheckPrint();
}

unsigned int Printer_GetIdleLimit()
{
  return g_PrinterIdleLimit;
}

void Printer_SetIdleLimit(unsigned int Duration)
{
  g_PrinterIdleLimit = Duration;
}
