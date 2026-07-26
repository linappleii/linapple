#include "frontends/sdl3/PrinterFrontend.h"

#include <cstdint>
#include <cstdio>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

static uint32_t inactivity = 0;
static uint32_t g_PrinterIdleLimit = 10;
static FilePtr_t file(nullptr, fclose);
bool g_printer_append = true;

static auto check_print() -> bool {
  inactivity = 0;
  if (!file) {
    file.reset(fopen(g_state.sParallelPrinterFile.data(),
                     (g_printer_append) ? "ab" : "wb"));
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

void printer_frontend_update(uint32_t totalcycles) {
  if (!file) {
    return;
  }
  if ((inactivity += totalcycles) > (printer_get_idle_limit() * 1000 * 1000)) {
    // inactive, so close the file (next print will overwrite it)
    ClosePrint();
  }
}

void printer_frontend_send_char(uint8_t value) {
  if (!check_print()) {
    return;
  }
  char c = static_cast<char>(value & 0x7F);
  fwrite(&c, 1, 1, file.get());
}

void printer_frontend_check_status() { check_print(); }

auto printer_get_idle_limit() -> uint32_t { return g_PrinterIdleLimit; }

void Printer_SetIdleLimit(uint32_t Duration) { g_PrinterIdleLimit = Duration; }
