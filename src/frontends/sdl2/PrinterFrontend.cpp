#include "frontends/sdl2/PrinterFrontend.h"

#include <cstdint>
#include <cstdio>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

static uint32_t inactivity = 0;
static uint32_t g_PrinterIdleLimit = 10;
static FilePtr file(nullptr, fclose);
bool g_bPrinterAppend = true;

static auto CheckPrint() -> bool {
  inactivity = 0;
  if (!file) {
    file.reset(fopen(g_state.sParallelPrinterFile.data(),
                     (g_bPrinterAppend) ? "ab" : "wb"));
  }
  return (file != nullptr);
}

static void ClosePrint() {
  file.reset();
  inactivity = 0;
}

void PrinterFrontend_Initialize() {
  // Initialization logic if any
}

void PrinterFrontend_Destroy() { ClosePrint(); }

void PrinterFrontend_Reset() { ClosePrint(); }

void PrinterFrontend_Update(uint32_t totalcycles) {
  if (!file) {
    return;
  }
  inactivity += totalcycles;
  constexpr uint32_t IDLE_FACTOR = 1000 * 1000;
  if (inactivity > (Printer_GetIdleLimit() * IDLE_FACTOR)) {
    // inactive, so close the file (next print will overwrite it)
    ClosePrint();
  }
}

void PrinterFrontend_SendChar(uint8_t value) {
  if (!CheckPrint()) {
    return;
  }
  constexpr uint8_t ASCII_MASK = 0x7F;
  char c = static_cast<char>(value & ASCII_MASK);
  fwrite(&c, 1, 1, file.get());
}

void PrinterFrontend_CheckStatus() { CheckPrint(); }

auto Printer_GetIdleLimit() -> uint32_t { return g_PrinterIdleLimit; }

void Printer_SetIdleLimit(uint32_t Duration) { g_PrinterIdleLimit = Duration; }
