// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/Peripheral_Internal.h"

// Dynamic peripheral plugin loading and internal registry inspection
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-pro-type-vararg, cppcoreguidelines-pro-type-reinterpret-cast, misc-include-cleaner, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-init-variables)
#include <dirent.h>
#include <dlfcn.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/SnapshotTypes.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Path.h"

namespace {
struct LoadedPlugin {
  Peripheral_t* p;
  void* handle;
  std::string path;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::vector<LoadedPlugin> loaded_plugins;
bool plugins_initialized = false;

struct LegacyOverride {
  int slot = 0;
  const char* key_card = "";
  const char* displaced = "";
};

// The shipped conf documents the key as putting the mouse card in slot 4 in
// place of whatever [Slots] names there.
constexpr int mouse_key_slot = 4;
constexpr const char* mouse_card_id = "linapple.mouse";
LegacyOverride legacy_override;

// Harddisk Enable is the legacy key that puts the hard disk in slot 7,
// honoured only where [Slots] has no Slot 7 line: the shipped conf pairs Slot
// 7 = Harddisk with Harddisk Enable = 0, and the slot table must win there.
constexpr int harddisk_key_slot = 7;
constexpr const char* harddisk_card_id = "linapple.harddisk";

// The //e pages its internal 80-column firmware over $C300 unless SLOTC3ROM is
// set (Apple IIe Technical Reference Manual, pp. 134-136), so a card put there
// for a run would never be reached by the Monitor's scan or by PR#3; the II
// and II Plus have no such page.
constexpr int internal_firmware_slot = 3;
std::string run_request_id;
int requested_slot = -1;
}  // namespace

auto peripheral_request_card_for_run(const char* id) -> void {
  run_request_id = (id != nullptr) ? id : "";
}

auto peripheral_requested_slot() -> int { return requested_slot; }

static auto slot_takes_card(const SS_PERIPHERAL_MANIFEST& manifest,
                            const Peripheral_t& card, int slot) -> bool {
  if (slot < 1 || slot >= static_cast<int>(num_slots)) {
    return false;
  }
  if (manifest.peripherals[slot].name[0] != '\0') {
    return false;
  }
  return (card.compatible_slots & (1U << static_cast<uint32_t>(slot))) != 0;
}

static auto apply_run_request(const std::string& id) -> void {
  requested_slot = -1;
  if (id.empty()) {
    return;
  }
  Peripheral_t* card = peripheral_find_internal(id.c_str());
  if (card == nullptr) {
    return;
  }
  const int configured = peripheral_slot_of(card->id);
  if (configured >= 0) {
    requested_slot = configured;
    return;
  }

  SS_PERIPHERAL_MANIFEST manifest{};
  peripheral_get_manifest(&manifest);
  const int preferred =
      (card->default_slot >= 1 && card->default_slot < num_slots)
          ? card->default_slot
          : static_cast<int>(num_slots) - 1;
  if (slot_takes_card(manifest, *card, preferred)) {
    if (peripheral_register(card, preferred) == 0) {
      requested_slot = preferred;
    }
    return;
  }
  for (int slot = preferred - 1; slot >= 1; --slot) {
    if (slot == internal_firmware_slot && !is_apple2()) {
      continue;
    }
    if (!slot_takes_card(manifest, *card, slot)) {
      continue;
    }
    if (peripheral_register(card, slot) != 0) {
      return;
    }
    requested_slot = slot;
    Logger::warning("Slot %d holds %s; %s installed in slot %d for this run\n",
                    preferred, manifest.peripherals[preferred].name, card->name,
                    slot);
    return;
  }
}

auto peripheral_legacy_override(int* slot, const char** key_card,
                                const char** displaced) -> bool {
  if (slot == nullptr || key_card == nullptr || displaced == nullptr) {
    return false;
  }
  if (legacy_override.slot == 0) {
    return false;
  }
  *slot = legacy_override.slot;
  *key_card = legacy_override.key_card;
  *displaced = legacy_override.displaced;
  return true;
}

auto peripheral_find_internal(const char* name) -> Peripheral_t* {
  if (name == nullptr) {
    return nullptr;
  }

  peripheral_plugins_init();

  for (auto const& p : peripheral_get_builtin_registry()) {
    if (p != nullptr &&
        (strcmp(p->name, name) == 0 || strcmp(p->id, name) == 0)) {
      return p;
    }
  }

  for (auto const& lp : loaded_plugins) {
    if (lp.p != nullptr &&
        (strcmp(lp.p->name, name) == 0 || strcmp(lp.p->id, name) == 0)) {
      return lp.p;
    }
  }

  if (strcmp(name, "Clock") == 0) {
    return peripheral_find_internal("Clock Card");
  }

  return nullptr;
}

auto peripheral_get_plugin_path(const char* name) -> const char* {
  if (name == nullptr) {
    return nullptr;
  }

  peripheral_plugins_init();

  for (auto const& lp : loaded_plugins) {
    if (lp.p != nullptr &&
        (strcmp(lp.p->name, name) == 0 || strcmp(lp.p->id, name) == 0)) {
      return lp.path.c_str();
    }
  }
  return nullptr;
}

auto peripheral_register_internal() -> void {
  peripheral_plugins_init();
  // A restart rebuilds the machine from the configuration; so does the record,
  // and a run request is taken once and asked for again by whoever still
  // wants it.
  legacy_override = LegacyOverride{};
  const std::string run_request = run_request_id;
  run_request_id.clear();
  requested_slot = -1;

  for (auto* p : peripheral_get_builtin_registry()) {
    if (p != nullptr && p->default_slot == 0) {
      peripheral_register(p, 0);
    }
  }

  // Slot 0 holds motherboard hardware, which no configuration key names, so
  // the only way a plugin can reach it is by declaring it. A builtin of the
  // same id wins: a statically linked card and its own .so are the same
  // device, and registering both would double every sample it produces.
  for (auto const& lp : loaded_plugins) {
    if (lp.p == nullptr || lp.p->default_slot != 0) {
      continue;
    }
    bool shadowed_by_builtin = false;
    for (const auto* builtin : peripheral_get_builtin_registry()) {
      if (builtin != nullptr && strcmp(builtin->id, lp.p->id) == 0) {
        shadowed_by_builtin = true;
        break;
      }
    }
    if (!shadowed_by_builtin) {
      peripheral_register(lp.p, 0);
    }
  }

  uint32_t mouse_key = 0;
  config_load_int(cfg_sec_configuration, cfg_mouse_in_slot4, &mouse_key);
  const Peripheral_t* mouse_key_card = nullptr;
  if (mouse_key != 0) {
    mouse_key_card = peripheral_find_internal(mouse_card_id);
    if (mouse_key_card == nullptr) {
      Logger::warning(
          "Mouse in slot 4 is set but the Mouse Interface is not built; Slot "
          "%d keeps its card\n",
          mouse_key_slot);
    }
  }

  // [Preferences] is read first: a saved file carries the key there as 1
  // beside the template's [Configuration] 0, and with no Slot 7 line that
  // file means a hard disk.
  uint32_t harddisk_key = 0;
  if (!config_load_int(cfg_sec_preferences, cfg_hdd_enabled, &harddisk_key)) {
    config_load_int(cfg_sec_configuration, cfg_hdd_enabled, &harddisk_key);
  }

  for (int slot = 1; slot < num_slots; ++slot) {
    constexpr size_t key_size = 16;
    char key[key_size];
    snprintf(key, sizeof(key), "Slot %d", slot);

    std::string name;
    const bool in_config = config_load_string("Slots", key, &name);

    if (in_config) {
      if (slot == harddisk_key_slot && harddisk_key != 0) {
        const Peripheral_t* named = peripheral_find_internal(name.c_str());
        if (named == nullptr || strcmp(named->id, harddisk_card_id) != 0) {
          Logger::info(
              "Harddisk Enable is set, but [Slots] names %s for slot %d; the "
              "slot table governs\n",
              name.empty() ? "None" : name.c_str(), slot);
        }
      }
      if (name == "None") {
        name.clear();
      }
    } else {
      if (slot == 1) {
        name = "linapple.printer";
      } else if (slot == 2) {
        name = "linapple.ssc";
      } else if (slot == 4) {
        name = "linapple.mockingboard";
      } else if (slot == 6) {
        name = "linapple.disk_II";
      } else if (slot == harddisk_key_slot && harddisk_key != 0) {
        name = harddisk_card_id;
      }
    }

    if (slot == mouse_key_slot && mouse_key_card != nullptr) {
      const Peripheral_t* displaced =
          name.empty() ? nullptr : peripheral_find_internal(name.c_str());
      if (displaced != mouse_key_card) {
        legacy_override.slot = slot;
        legacy_override.key_card = mouse_key_card->name;
        legacy_override.displaced = displaced != nullptr ? displaced->name : "";
        if (displaced != nullptr) {
          Logger::warning(
              "Slot %d: Mouse in slot 4 installs the %s in place of %s\n", slot,
              mouse_key_card->name, displaced->name);
        }
      }
      name = mouse_card_id;
    }

    if (name.empty()) {
      continue;
    }

    Peripheral_t* p = peripheral_find_internal(name.c_str());
    if (p != nullptr) {
      peripheral_register(p, slot);
    }
  }

  // After the table and the keys, so the request takes only a slot nothing
  // else claimed and the mouse key's slot is simply not free.
  apply_run_request(run_request);
}

auto linapple_list_hardware() -> void {
  peripheral_plugins_init();

  printf("Built-in Peripherals:\n");
  printf("---------------------\n");
  for (auto const& p : peripheral_get_builtin_registry()) {
    if (p != nullptr) {
      printf("- %-24s [%s] v%s\n", p->name, p->id, p->version);
      printf("  Author: %s\n", p->author);
      printf("  Desc:   %s\n", p->description);
      printf("  Slots:  ");
      bool first = true;
      for (int i = 0; i < num_slots; ++i) {
        if ((p->compatible_slots & (1U << static_cast<uint32_t>(i))) != 0U) {
          if (!first) {
            printf(", ");
          }
          printf("%d", i);
          first = false;
        }
      }
      printf("\n\n");
    }
  }

  if (!loaded_plugins.empty()) {
    printf("Dynamically Loaded Peripherals:\n");
    printf("-------------------------------\n");
    for (auto const& plugin : loaded_plugins) {
      printf("- %-24s [%s] v%s\n", plugin.p->name, plugin.p->id,
             plugin.p->version);
      printf("  Path:   %s\n", plugin.path.c_str());
      printf("  Author: %s\n", plugin.p->author);
      printf("  Desc:   %s\n", plugin.p->description);
      printf("  Slots:  ");
      bool first = true;
      for (int i = 0; i < num_slots; ++i) {
        if ((plugin.p->compatible_slots & (1U << static_cast<uint32_t>(i))) !=
            0U) {
          if (!first) {
            printf(", ");
          }
          printf("%d", i);
          first = false;
        }
      }
      printf("\n\n");
    }
  }
}

auto peripheral_plugins_init(const char* plugin_dir) -> void {
  if (plugins_initialized && plugin_dir == nullptr) {
    return;
  }
  plugins_initialized = true;

  std::vector<std::string> paths;
  if (plugin_dir != nullptr && *plugin_dir != '\0') {
    paths.emplace_back(plugin_dir);
  } else {
    paths = Path::get_plugin_search_paths();
  }
  for (const auto& path : paths) {
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
      continue;
    }

    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
      const std::string filename = ent->d_name;
      if (filename.length() > 3 &&
          filename.substr(filename.length() - 3) == ".so") {
        if (filename.find('/') != std::string::npos) {
          continue;
        }
        const std::string full_path = Path::join(path, filename);
        void* handle = dlopen(full_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle != nullptr) {
          auto* p = reinterpret_cast<Peripheral_t*>(
              dlsym(handle, "linapple_peripheral_descriptor"));
          if (p != nullptr) {
            if (p->abi_version != LINAPPLE_ABI_VERSION) {
              Logger::error("Plugin ABI mismatch: %s (expected %d, got %d)\n",
                            full_path.c_str(), LINAPPLE_ABI_VERSION,
                            p->abi_version);
              dlclose(handle);
            } else if (p->id == nullptr || p->name == nullptr ||
                       p->init == nullptr) {
              // An all-zero descriptor reads as ABI version 0, so the version
              // check alone cannot tell a plugin from a blank page, and every
              // later lookup walks these fields.
              Logger::error("Plugin descriptor is incomplete: %s\n",
                            full_path.c_str());
              dlclose(handle);
            } else {
              bool already_loaded = false;
              for (const auto& existing : loaded_plugins) {
                if (existing.p == p ||
                    (existing.p != nullptr && existing.p->id != nullptr &&
                     strcmp(existing.p->id, p->id) == 0)) {
                  already_loaded = true;
                  break;
                }
              }
              for (const auto* builtin : peripheral_get_builtin_registry()) {
                if (builtin == p ||
                    (builtin != nullptr && builtin->id != nullptr &&
                     strcmp(builtin->id, p->id) == 0)) {
                  already_loaded = true;
                  break;
                }
              }
              if (already_loaded) {
                dlclose(handle);
              } else {
                Logger::info("Loaded plugin: %s from %s\n", p->name,
                             full_path.c_str());
                loaded_plugins.push_back({p, handle, full_path});
              }
            }
          } else {
            Logger::error(
                "Invalid plugin (missing linapple_peripheral_descriptor): %s\n",
                full_path.c_str());
            dlclose(handle);
          }
        } else {
          Logger::error("Failed to load plugin: %s (%s)\n", full_path.c_str(),
                        dlerror());
        }
      }
    }
    closedir(dir);
  }
}

auto peripheral_plugins_shutdown() -> void {
  for (const auto& plugin : loaded_plugins) {
    if (plugin.handle != nullptr) {
      dlclose(plugin.handle);
    }
  }
  loaded_plugins.clear();
  plugins_initialized = false;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-pro-type-vararg, cppcoreguidelines-pro-type-reinterpret-cast, misc-include-cleaner, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-init-variables)
