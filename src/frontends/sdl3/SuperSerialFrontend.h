// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

// Opaque host side interface for the Super Serial Card
auto super_serial_frontend_initialize(const char* serial_port_path) -> bool;
auto super_serial_frontend_close() -> void;
auto super_serial_frontend_send_byte(uint8_t byte) -> void;
auto super_serial_frontend_set_serial_port_path(const char* serial_port_path)
    -> void;
auto super_serial_frontend_set_loopback(bool enable) -> void;
auto super_serial_frontend_is_active() -> bool;
auto super_serial_frontend_update_state(uint32_t baud, uint32_t bits, int parity,
                                     int stop) -> void;
