// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

void PrinterFrontend_Initialize();
void PrinterFrontend_Destroy();
void PrinterFrontend_Reset();
void printer_frontend_update(uint32_t totalcycles);
void printer_frontend_send_char(uint8_t c);
void printer_frontend_check_status();

void Printer_SetIdleLimit(uint32_t Duration);
auto printer_get_idle_limit() -> uint32_t;

extern bool g_printer_append;
