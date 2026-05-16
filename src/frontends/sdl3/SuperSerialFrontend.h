// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

// Opaque host side interface for the Super Serial Card
auto SuperSerialFrontend_Initialize(const char* serial_port_path) -> bool;
auto SuperSerialFrontend_Close() -> void;
auto SuperSerialFrontend_SendByte(uint8_t byte) -> void;
auto SuperSerialFrontend_SetSerialPortPath(const char* serial_port_path)
    -> void;
auto SuperSerialFrontend_SetLoopback(bool enable) -> void;
auto SuperSerialFrontend_IsActive() -> bool;
auto SuperSerialFrontend_UpdateState(uint32_t baud, uint32_t bits, int parity,
                                     int stop) -> void;
