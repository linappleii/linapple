#include "frontends/sdl1/PrinterFrontend.h"

#include <cstdint>
#include <cstdio>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

static uint32_t inactivity = 0;
static uint32_t g_printer_idle_limit = 10;
static FilePtr_t file(nullptr, fclose);
bool g_printer_append = true;

static auto check_print() -> bool {
  inactivity = 0;
  if (!file) {
    file.reset(fopen(g_state.parallel_printer_file.data(),
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
  inactivity += totalcycles;
  constexpr uint32_t idle_factor = 1000 * 1000;
  if (inactivity > (printer_get_idle_limit() * idle_factor)) {
    // inactive, so close the file (next print will overwrite it)
    ClosePrint();
  }
}

void printer_frontend_send_char(uint8_t value) {
  if (!check_print()) {
    return;
  }
  constexpr uint8_t ascii_mask = 0x7F;
  char c = static_cast<char>(value & ascii_mask);
  fwrite(&c, 1, 1, file.get());
}

void printer_frontend_check_status() { check_print(); }

auto printer_get_idle_limit() -> uint32_t { return g_printer_idle_limit; }

void Printer_SetIdleLimit(uint32_t Duration) {
  g_printer_idle_limit = Duration;
}
