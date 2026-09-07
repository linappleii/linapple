// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include <vector>

#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "core/config/ConfigSchema.h"
#include "core/config/Toml.h"

struct ConfigDispatchResult_t {
  int applied_options_count = 0;
  int error_count = 0;
  std::vector<std::string> messages;
};

// Maps a PeripheralCardType_t enum value to its canonical built-in ID (e.g.
// "linapple.disk_II")
auto config_card_type_to_id(PeripheralCardType_t type) -> const char*;

// Maps a PeripheralCardType_t enum value to its canonical TOML section name
// (e.g. "DiskII")
auto config_card_type_to_section(PeripheralCardType_t type) -> const char*;

// Resolves a single peripheral configuration option using Hierarchical Delivery
// Precedence:
// 1. Slot-specific override ([Peripheral.<Name>.Slot<N>].<Key> or
// [Slot<N>.<Name>].<Key>)
// 2. Base peripheral table ([Peripheral.<Name>].<Key>)
// 3. Schema default value (option.default_value)
auto config_resolve_peripheral_option(const TomlDocument_t* doc,
                                      const char* card_name,
                                      const char* card_id, int slot,
                                      const PeripheralConfigOption_t& option,
                                      std::string* out_value) -> bool;

// Dispatches configuration to a single slot:
// Registers card if needed, queries schema, resolves options hierarchically,
// and calls peripheral_configure.
auto config_dispatch_slot(int slot, PeripheralCardType_t card_type,
                          const TomlDocument_t* doc,
                          ConfigDispatchResult_t* result = nullptr) -> int;

// Dispatches all slot configurations according to config.slots and
// config.raw_doc
auto config_dispatch_slots(const LinAppleConfig_t& config,
                           ConfigDispatchResult_t* result = nullptr) -> int;

// Dispatches Core subsystem settings (Machine type, speed)
auto config_dispatch_core(const CoreConfig_t& core) -> bool;

// Dispatches Video subsystem settings (Standard, emulation type, monochrome
// color, dimensions)
auto config_dispatch_video(const VideoConfig_t& video) -> bool;

// Dispatches Audio subsystem settings
auto config_dispatch_audio(const AudioConfig_t& audio) -> bool;

// Dispatches Keyboard subsystem settings
auto config_dispatch_keyboard(const KeyboardConfig_t& keyboard) -> bool;

// Dispatches Joystick subsystem settings
auto config_dispatch_joystick(const JoystickConfig_t& joystick) -> bool;

// Dispatches full LinApple configuration across all subsystems and slots
auto config_dispatch_all(const LinAppleConfig_t& config,
                         ConfigDispatchResult_t* result = nullptr) -> int;
