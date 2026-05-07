#include "Peripheral_Internal.h"

#include <dirent.h>
#include <dlfcn.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "LinAppleCore.h"
#include "apple2/Structs.h"
#include "core/Common_Globals.h"
#include "core/Log.h"
#include "core/Util_Path.h"

struct LoadedPlugin {
  Peripheral_t* p;
  void* handle;
  std::string path;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::vector<LoadedPlugin> g_loaded_plugins;

static bool g_plugins_initialized = false;

/**
 * Justification: Peripheral Manager requires a static list of built-in hardware
 * to support runtime slot assignment via configuration.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-interfaces-global-init)
static const std::array<Peripheral_t*, 9> g_builtin_peripherals = {{
#if defined(ENABLE_PERIPHERAL_SPEAKER)
    &g_speaker_peripheral,
#endif
#if defined(ENABLE_PERIPHERAL_MOCKINGBOARD)
    &g_mockingboard_peripheral,
#endif
#if defined(ENABLE_PERIPHERAL_DISK)
    &g_disk_peripheral,
#endif
#if defined(ENABLE_PERIPHERAL_SSC)
    &g_ssc_peripheral,
#endif
#if defined(ENABLE_PERIPHERAL_PRINTER)
    &g_printer_peripheral,
#endif
#if defined(ENABLE_PERIPHERAL_HARDDISK)
    &g_harddisk_peripheral,
#endif
#if defined(ENABLE_PERIPHERAL_MOUSE)
    &g_mouse_peripheral,
#endif
#if defined(ENABLE_PERIPHERAL_CLOCK)
    &g_clock_peripheral,
#endif
    nullptr}};

auto Peripheral_Find_Internal(const char* name) -> Peripheral_t* {
  if (!name) return nullptr;

  Peripheral_Plugins_Init();

  for (auto const& p : g_builtin_peripherals) {
    if (p && strcmp(p->name, name) == 0) {
      return p;
    }
  }

  for (auto const& lp : g_loaded_plugins) {
    if (lp.p && strcmp(lp.p->name, name) == 0) {
      return lp.p;
    }
  }

  // Support legacy configuration names to prevent breakage of existing user setups.
  if (strcmp(name, "No-Slot Clock") == 0 || strcmp(name, "Clock") == 0) {
    return Peripheral_Find_Internal("Clock Card");
  }

  return nullptr;
}

const char* Peripheral_GetPluginPath(const char* name) {
  if (!name) return nullptr;

  Peripheral_Plugins_Init();

  for (auto const& lp : g_loaded_plugins) {
    if (lp.p && strcmp(lp.p->name, name) == 0) {
      return lp.path.c_str();
    }
  }
  return nullptr;
}

static auto GetDefaultPeripheralForSlot(int slot) -> const char* {
  const int SLOT_PRINTER = 1;
  const int SLOT_SSC = 2;
  const int SLOT_DISK = 6;
  const int SLOT_HARDDISK = 7;

  switch (slot) {
    case SLOT_PRINTER:
      return "Parallel Printer";
    case SLOT_SSC:
      return "Super Serial Card";
    case SLOT_DISK:
      return "Disk II";
    case SLOT_HARDDISK:
      return hddenabled ? "Harddisk" : nullptr;
    default: {
      uint32_t clock_slot = 0;
      if (LOAD(REGVALUE_CLOCK_ENABLED, &clock_slot) &&
          clock_slot == static_cast<uint32_t>(slot)) {
        return "Clock Card";
      }
      if (slot == 4) {
        if (g_Slot4 == CT_Mockingboard) return "Mockingboard";
        if (g_Slot4 == CT_MouseInterface) return "Mouse Interface";
      }
      return nullptr;
    }
  }
}

void Peripheral_Register_Internal() {
#if defined(ENABLE_PERIPHERAL_KEYBOARD)
  // Keyboard is internal (Slot 0)
  Peripheral_Register(&keyboard_peripheral, 0);
#endif

#if defined(ENABLE_PERIPHERAL_SPEAKER)
  // Speaker is internal (Slot 0)
  Peripheral_Register(&g_speaker_peripheral, 0);
#endif

  for (int slot = 1; slot < NUM_SLOTS; ++slot) {
    const size_t KEY_SIZE = 16;
    // Justification: Formatting slot key name for registry lookup.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,
    // cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
    char key[KEY_SIZE];
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,
    // cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    snprintf(key, sizeof(key), "Slot %d", slot);

    std::string name;
    // Justification: Loading slot configuration from registry.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    bool in_config = ConfigLoadString("Slots", key, &name);

    if (in_config) {
      if (name == "None" || name.empty()) {
        continue;
      }
    } else {
      const char* default_name = GetDefaultPeripheralForSlot(slot);
      if (default_name) {
        name = default_name;
      } else {
        continue;
      }
    }

    Peripheral_t* p = Peripheral_Find_Internal(name.c_str());
    if (p) {
      Peripheral_Register(p, slot);
    }
  }
}

void Linapple_ListHardware() {
  Peripheral_Plugins_Init();

  printf("Built-in Peripherals:\n");
  printf("---------------------\n");
  for (auto const& p : g_builtin_peripherals) {
    if (p) {
      printf("- %-20s (Compatible Slots: ", p->name);
      bool first = true;
      for (int i = 0; i < NUM_SLOTS; ++i) {
        if (p->compatible_slots & (1u << static_cast<uint32_t>(i))) {
          if (!first) printf(", ");
          printf("%d", i);
          first = false;
        }
      }
      printf(")\n");
    }
  }
  printf("\n");

  if (!g_loaded_plugins.empty()) {
    printf("Dynamically Loaded Peripherals:\n");
    printf("-------------------------------\n");
    for (auto const& plugin : g_loaded_plugins) {
      printf("- %-20s (Compatible Slots: ", plugin.p->name);
      bool first = true;
      for (int i = 0; i < NUM_SLOTS; ++i) {
        if (plugin.p->compatible_slots & (1u << static_cast<uint32_t>(i))) {
          if (!first) printf(", ");
          printf("%d", i);
          first = false;
        }
      }
      printf(") [%s]\n", plugin.path.c_str());
    }
    printf("\n");
  }
}

void Peripheral_Plugins_Init(void) {
  if (g_plugins_initialized) {
    return;
  }
  g_plugins_initialized = true;

  auto paths = Path::GetPluginSearchPaths();
  for (const auto& path : paths) {
    DIR* dir = opendir(path.c_str());
    if (!dir) continue;

    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
      std::string filename = ent->d_name;
      if (filename.length() > 3 &&
          filename.substr(filename.length() - 3) == ".so") {
        if (filename.find('/') != std::string::npos) {
          continue;
        }
        std::string fullPath = Path::Join(path, filename);
        void* handle = dlopen(fullPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle) {
          auto* p = reinterpret_cast<Peripheral_t*>(
              dlsym(handle, "linapple_peripheral_descriptor"));
          if (p) {
            if (p->abi_version == LINAPPLE_ABI_VERSION) {
              // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
              Logger::Info("Loaded plugin: %s from %s\n", p->name,
                           fullPath.c_str());
              g_loaded_plugins.push_back({p, handle, fullPath});
            } else {
              // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
              Logger::Warning("Plugin ABI mismatch: %s (expected %d, got %d)\n",
                              fullPath.c_str(), LINAPPLE_ABI_VERSION,
                              p->abi_version);
              dlclose(handle);
            }
          } else {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            Logger::Warning(
                "Invalid plugin (missing linapple_peripheral_descriptor): %s\n",
                fullPath.c_str());
            dlclose(handle);
          }
        } else {
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
          Logger::Warning("Failed to load plugin: %s (%s)\n", fullPath.c_str(),
                          dlerror());
        }
      }
    }
    closedir(dir);
  }
}

void Peripheral_Plugins_Shutdown(void) {
  for (auto& plugin : g_loaded_plugins) {
    if (plugin.handle) {
      dlclose(plugin.handle);
    }
  }
  g_loaded_plugins.clear();
  g_plugins_initialized = false;
}
