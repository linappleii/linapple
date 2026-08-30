#include "frontends/sdl3/PrinterFrontend.h"

#include <cstdint>
#include <cstdio>

#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

constexpr uint32_t DEFAULT_PRINTER_IDLE_LIMIT = 10;
constexpr uint32_t CYCLES_PER_SEC = 1000000;
constexpr uint8_t ASCII_7BIT_MASK = 0x7F;

static uint32_t inactivity = 0;
static uint32_t g_printer_idle_limit = DEFAULT_PRINTER_IDLE_LIMIT;
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
  if ((inactivity += totalcycles) >
      (printer_get_idle_limit() * CYCLES_PER_SEC)) {
    // inactive, so close the file (next print will overwrite it)
    ClosePrint();
  }
}

void printer_frontend_send_char(uint8_t value) {
  if (!check_print()) {
    return;
  }
  char c = static_cast<char>(value & ASCII_7BIT_MASK);
  fwrite(&c, 1, 1, file.get());
}

void printer_frontend_check_status() { check_print(); }

auto printer_get_idle_limit() -> uint32_t { return g_printer_idle_limit; }

void Printer_SetIdleLimit(uint32_t Duration) {
  g_printer_idle_limit = Duration;
}
