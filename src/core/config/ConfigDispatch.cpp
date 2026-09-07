// SPDX-License-Identifier: GPL-2.0-only
#include "core/config/ConfigDispatch.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Peripheral_Types.h"
#include "core/config/ConfigSchema.h"
#include "core/config/Toml.h"

namespace {

constexpr uint32_t clks_per_frame_pal = 20280;
constexpr uint32_t clks_per_frame_ntsc = 17030;

auto is_hex_color(const std::string& str) -> bool {
  if (str.size() != 7 && str.size() != 9) {
    return false;
  }
  if (str[0] != '#') {
    return false;
  }
  for (size_t i = 1; i < str.size(); ++i) {
    if (std::isxdigit(static_cast<unsigned char>(str[i])) == 0) {
      return false;
    }
  }
  return true;
}

auto str_case_compare(const std::string& a, const std::string& b) -> bool {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

auto find_key_case_insensitive(const TomlTable_t* table, const std::string& key,
                               std::string* out_key) -> bool {
  if (table == nullptr) {
    return false;
  }
  for (const auto& kv : *table) {
    if (str_case_compare(kv.first, key)) {
      if (out_key != nullptr) {
        *out_key = kv.first;
      }
      return true;
    }
  }
  return false;
}

auto find_table_case_insensitive(const TomlDocument_t* doc,
                                 const std::string& name)
    -> const TomlTable_t* {
  if (doc == nullptr) {
    return nullptr;
  }
  const auto* exact = toml_find_table(doc, name);
  if (exact != nullptr) {
    return exact;
  }
  for (const auto& kv : doc->sections) {
    if (str_case_compare(kv.first, name)) {
      return &kv.second;
    }
  }
  return nullptr;
}

auto build_slot_table_names(const char* card_name, const char* card_id,
                            int slot) -> std::vector<std::string> {
  std::vector<std::string> slot_tables;
  const std::string slot_suffix = ".Slot" + std::to_string(slot);

  if (card_name != nullptr && card_name[0] != '\0') {
    slot_tables.push_back(std::string("Peripheral.") + card_name + slot_suffix);
    slot_tables.push_back(std::string("Slot") + std::to_string(slot) + "." +
                          card_name);
  }
  if (card_id != nullptr && card_id[0] != '\0') {
    slot_tables.push_back(std::string("Peripheral.") + card_id + slot_suffix);
    const std::string cid = card_id;
    if (cid.rfind("linapple.", 0) == 0) {
      slot_tables.push_back(std::string("Peripheral.") + cid.substr(9) +
                            slot_suffix);
    }
  }
  slot_tables.push_back(std::string("Slot") + std::to_string(slot));
  return slot_tables;
}

auto build_base_table_names(const char* card_name, const char* card_id)
    -> std::vector<std::string> {
  std::vector<std::string> base_tables;
  if (card_name != nullptr && card_name[0] != '\0') {
    base_tables.push_back(std::string("Peripheral.") + card_name);
    base_tables.emplace_back(card_name);
  }
  if (card_id != nullptr && card_id[0] != '\0') {
    base_tables.push_back(std::string("Peripheral.") + card_id);
    const std::string cid = card_id;
    if (cid.rfind("linapple.", 0) == 0) {
      base_tables.push_back(std::string("Peripheral.") + cid.substr(9));
    }
  }
  return base_tables;
}

}  // namespace

auto config_card_type_to_id(PeripheralCardType_t type) -> const char* {
  switch (type) {
    case PeripheralCardType_t::ParallelPrinter:
      return "linapple.printer";
    case PeripheralCardType_t::SuperSerial:
      return "linapple.ssc";
    case PeripheralCardType_t::Mockingboard:
      return "linapple.mockingboard";
    case PeripheralCardType_t::DiskII:
      return "linapple.disk_II";
    case PeripheralCardType_t::Harddisk:
      return "linapple.harddisk";
    case PeripheralCardType_t::Mouse:
      return "linapple.mouse";
    case PeripheralCardType_t::Clock:
      return "linapple.clock";
    case PeripheralCardType_t::Empty:
    default:
      return nullptr;
  }
}

auto config_card_type_to_section(PeripheralCardType_t type) -> const char* {
  switch (type) {
    case PeripheralCardType_t::ParallelPrinter:
      return "Printer";
    case PeripheralCardType_t::SuperSerial:
      return "SuperSerial";
    case PeripheralCardType_t::Mockingboard:
      return "Mockingboard";
    case PeripheralCardType_t::DiskII:
      return "DiskII";
    case PeripheralCardType_t::Harddisk:
      return "Harddisk";
    case PeripheralCardType_t::Mouse:
      return "Mouse";
    case PeripheralCardType_t::Clock:
      return "Clock";
    case PeripheralCardType_t::Empty:
    default:
      return nullptr;
  }
}

auto config_resolve_peripheral_option(const TomlDocument_t* doc,
                                      const char* card_name,
                                      const char* card_id, int slot,
                                      const PeripheralConfigOption_t& option,
                                      std::string* out_value) -> bool {
  if (option.name == nullptr || out_value == nullptr) {
    return false;
  }

  // Precedence 1: Slot-specific override tables
  if (doc != nullptr) {
    const auto slot_tables = build_slot_table_names(card_name, card_id, slot);
    for (const auto& tbl_name : slot_tables) {
      const auto* tbl = find_table_case_insensitive(doc, tbl_name);
      if (tbl != nullptr) {
        std::string matched_key;
        if (find_key_case_insensitive(tbl, option.name, &matched_key)) {
          *out_value = toml_table_get_string(tbl, matched_key, "");
          return true;
        }
      }
    }

    // Precedence 2: Base peripheral tables
    const auto base_tables = build_base_table_names(card_name, card_id);
    for (const auto& tbl_name : base_tables) {
      const auto* tbl = find_table_case_insensitive(doc, tbl_name);
      if (tbl != nullptr) {
        std::string matched_key;
        if (find_key_case_insensitive(tbl, option.name, &matched_key)) {
          *out_value = toml_table_get_string(tbl, matched_key, "");
          return true;
        }
      }
    }
  }

  // Precedence 3: Schema default value
  if (option.default_value != nullptr) {
    *out_value = option.default_value;
    return true;
  }

  return false;
}

auto config_dispatch_slot(int slot, PeripheralCardType_t card_type,
                          const TomlDocument_t* doc,
                          ConfigDispatchResult_t* result) -> int {
  if (slot < 0 || slot >= static_cast<int>(config_slot_count)) {
    if (result != nullptr) {
      result->error_count++;
      result->messages.emplace_back("Slot out of range: " +
                                    std::to_string(slot));
    }
    return -1;
  }

  if (slot == 0) {
    if (card_type != PeripheralCardType_t::Empty) {
      if (result != nullptr) {
        result->error_count++;
        result->messages.emplace_back(
            "Slot 0 is reserved for motherboard memory/ROM and cannot hold "
            "expansion cards.");
      }
      return -1;
    }
    return 0;
  }

  if (card_type == PeripheralCardType_t::Empty) {
    if (!peripheral_is_slot_empty(slot)) {
      peripheral_unregister(slot);
    }
    return 0;
  }

  const char* id = config_card_type_to_id(card_type);
  const char* name = config_card_type_to_section(card_type);
  if (id == nullptr) {
    if (result != nullptr) {
      result->error_count++;
      result->messages.emplace_back(
          "Unsupported peripheral card type in slot " + std::to_string(slot));
    }
    return -1;
  }

  auto* p = peripheral_find_builtin(id);
  if (p == nullptr) {
    p = peripheral_find_internal(id);
  }
  if (p == nullptr) {
    if (result != nullptr) {
      result->error_count++;
      result->messages.emplace_back("Peripheral descriptor not found for ID: " +
                                    std::string(id));
    }
    return -1;
  }

  const auto* current = peripheral_get_registered(slot);
  if (current != nullptr && current != p) {
    peripheral_unregister(slot);
  }

  if (peripheral_is_slot_empty(slot)) {
    int reg = peripheral_register(p, slot);
    if (reg != 0) {
      if (result != nullptr) {
        result->error_count++;
        result->messages.emplace_back("Failed to register peripheral in slot " +
                                      std::to_string(slot));
      }
      return -1;
    }
  }

  std::vector<std::string> applied_keys;
  const auto* schema = peripheral_get_config_schema(slot);
  if (schema != nullptr && schema->options != nullptr) {
    for (size_t i = 0; i < schema->option_count; ++i) {
      const auto& opt = schema->options[i];
      if (opt.name == nullptr) {
        continue;
      }
      std::string val;
      if (config_resolve_peripheral_option(doc, name, id, slot, opt, &val)) {
        PeripheralStatus_t st =
            peripheral_configure(slot, opt.name, val.c_str());
        if (st == peripheral_ok) {
          applied_keys.emplace_back(opt.name);
          if (result != nullptr) {
            result->applied_options_count++;
          }
        } else if (st != peripheral_incompatible) {
          if (result != nullptr) {
            result->error_count++;
            result->messages.emplace_back("Error configuring " +
                                          std::string(opt.name) + " on slot " +
                                          std::to_string(slot));
          }
        }
      }
    }
  }

  // Dispatch extra keys defined in slot-specific and base tables
  if (doc != nullptr) {
    std::vector<std::string> extra_tables =
        build_slot_table_names(name, id, slot);
    const auto base_tables = build_base_table_names(name, id);
    extra_tables.insert(extra_tables.end(), base_tables.begin(),
                        base_tables.end());

    for (const auto& tbl_name : extra_tables) {
      const auto* tbl = find_table_case_insensitive(doc, tbl_name);
      if (tbl != nullptr) {
        for (const auto& kv : *tbl) {
          bool already_applied = false;
          for (const auto& ak : applied_keys) {
            if (str_case_compare(ak, kv.first)) {
              already_applied = true;
              break;
            }
          }
          if (!already_applied) {
            std::string val = toml_table_get_string(tbl, kv.first, "");
            PeripheralStatus_t st =
                peripheral_configure(slot, kv.first.c_str(), val.c_str());
            if (st == peripheral_ok) {
              applied_keys.push_back(kv.first);
              if (result != nullptr) {
                result->applied_options_count++;
              }
            } else if (st != peripheral_incompatible) {
              applied_keys.push_back(kv.first);
              if (result != nullptr) {
                result->error_count++;
                result->messages.emplace_back("Error configuring extra key " +
                                              kv.first + " on slot " +
                                              std::to_string(slot));
              }
            }
          }
        }
      }
    }
  }

  return 0;
}

auto config_dispatch_slots(const LinAppleConfig_t& config,
                           ConfigDispatchResult_t* result) -> int {
  int err_count = 0;
  for (size_t slot = 1; slot < config_slot_count; ++slot) {
    int ret = config_dispatch_slot(static_cast<int>(slot),
                                   config.slots.cards.at(slot),
                                   config.raw_doc.get(), result);
    if (ret != 0) {
      err_count++;
    }
  }
  return err_count == 0 ? 0 : -1;
}

auto config_dispatch_core(const CoreConfig_t& core) -> bool {
  switch (core.machine) {
    case MachineType_t::Apple2:
      g_apple2_type = A2TYPE_APPLE2;
      break;
    case MachineType_t::Apple2Plus:
      g_apple2_type = A2TYPE_APPLE2PLUS;
      break;
    case MachineType_t::Apple2JPlus:
      g_apple2_type = A2TYPE_APPLE2JPLUS;
      break;
    case MachineType_t::Apple2e:
      g_apple2_type = A2TYPE_APPLE2E;
      break;
    case MachineType_t::Apple2eEnhanced:
    default:
      g_apple2_type = A2TYPE_APPLE2EENHANCED;
      break;
  }

  double speed_factor = core.emulation_speed;
  if (std::isnan(speed_factor) || std::isinf(speed_factor)) {
    speed_factor = 1.0;
  }
  int speed_val = static_cast<int>(speed_factor * SPEED_NORMAL);
  if (speed_val < SPEED_MIN) {
    speed_val = SPEED_MIN;
  }
  if (speed_val > emulation_speed_max) {
    speed_val = emulation_speed_max;
  }
  g_state.speed = static_cast<uint32_t>(speed_val);

  return true;
}

auto config_dispatch_video(const VideoConfig_t& video) -> bool {
  if (video.video_standard == VideoStandard_t::PAL) {
    g_state.video_scanner_ntsc = false;
    g_state.clks_per_frame = clks_per_frame_pal;
  } else {
    g_state.video_scanner_ntsc = true;
    g_state.clks_per_frame = clks_per_frame_ntsc;
  }

  switch (video.video_emulation) {
    case VideoEmulation_t::MonochromeCustom:
      g_videotype = VT_MONO_CUSTOM;
      break;
    case VideoEmulation_t::ColorStandard:
      g_videotype = VT_COLOR_STANDARD;
      break;
    case VideoEmulation_t::ColorTextOptimized:
      g_videotype = VT_COLOR_TEXT_OPTIMIZED;
      break;
    case VideoEmulation_t::ColorTvEmulation:
      g_videotype = VT_COLOR_TVEMU;
      break;
    case VideoEmulation_t::ColorHalfShift:
      g_videotype = VT_COLOR_HALF_SHIFT_DIM;
      break;
    case VideoEmulation_t::MonochromeAmber:
      g_videotype = VT_MONO_AMBER;
      break;
    case VideoEmulation_t::MonochromeGreen:
      g_videotype = VT_MONO_GREEN;
      break;
    case VideoEmulation_t::MonochromeWhite:
      g_videotype = VT_MONO_WHITE;
      break;
  }

  if (video.video_emulation == VideoEmulation_t::MonochromeCustom &&
      is_hex_color(video.monochrome_color)) {
    const unsigned long r =
        std::strtoul(video.monochrome_color.substr(1, 2).c_str(), nullptr, 16);
    const unsigned long g =
        std::strtoul(video.monochrome_color.substr(3, 2).c_str(), nullptr, 16);
    const unsigned long b =
        std::strtoul(video.monochrome_color.substr(5, 2).c_str(), nullptr, 16);
    monochrome = RGB(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                     static_cast<uint8_t>(b));
  }

  g_state.fullscreen = video.fullscreen;
  if (video.screen_width > 0 && video.screen_height > 0) {
    g_state.screen_width = static_cast<uint32_t>(video.screen_width);
    g_state.screen_height = static_cast<uint32_t>(video.screen_height);
  } else if (video.screen_factor > 0.0 && !std::isnan(video.screen_factor) &&
             !std::isinf(video.screen_factor)) {
    g_state.screen_width = static_cast<uint32_t>(
        static_cast<double>(SCREEN_WIDTH) * video.screen_factor);
    g_state.screen_height = static_cast<uint32_t>(
        static_cast<double>(SCREEN_HEIGHT) * video.screen_factor);
  }

  g_show_leds = video.show_leds;
  g_singlethreaded = video.multithreaded ? 0 : 1;

  return true;
}

auto config_dispatch_audio(const AudioConfig_t& audio) -> bool {
  (void)audio;
  return true;
}

auto config_dispatch_keyboard(const KeyboardConfig_t& keyboard) -> bool {
  (void)keyboard;
  return true;
}

auto config_dispatch_joystick(const JoystickConfig_t& joystick) -> bool {
  (void)joystick;
  return true;
}

auto config_dispatch_all(const LinAppleConfig_t& config,
                         ConfigDispatchResult_t* result) -> int {
  config_dispatch_core(config.core);
  config_dispatch_video(config.video);
  config_dispatch_audio(config.audio);
  config_dispatch_keyboard(config.keyboard);
  config_dispatch_joystick(config.joystick);
  return config_dispatch_slots(config, result);
}
