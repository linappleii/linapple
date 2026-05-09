#ifndef DISK_H
#define DISK_H

#include <cstdint>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/Common.h"
#include "core/Peripheral.h"

#pragma once

auto DiskInitialize() -> void;

auto DiskUpdatePosition(uint32_t) -> void;

extern Peripheral_t g_disk_peripheral;

#endif
