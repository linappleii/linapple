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

/**
 * Justification: Peripheral Manager requires a registry of built-in hardware
 * to support runtime slot assignment via configuration.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::vector<Peripheral_t*> g_builtin_registry;

static bool g_plugins_initialized = false;
static bool g_builtins_discovered = false;

extern "C" {
__attribute__((weak)) void Register_Speaker();
__attribute__((weak)) void Register_Mockingboard();
__attribute__((weak)) void Register_Disk();
__attribute__((weak)) void Register_SSC();
__attribute__((weak)) void Register_Printer();
__attribute__((weak)) void Register_Harddisk();
__attribute__((weak)) void Register_Mouse();
__attribute__((weak)) void Register_Clock();
__attribute__((weak)) void Register_Keyboard();
__attribute__((weak)) void Register_Joystick();
}

void Peripheral_Register_Builtin(Peripheral_t* p) {
  if (p) {
    g_builtin_registry.push_back(p);
  }
}

static void Discover_Builtins() {
  if (g_builtins_discovered) {
    return;
  }
  g_builtins_discovered = true;

  if (Register_Speaker) Register_Speaker();
  if (Register_Mockingboard) Register_Mockingboard();
  if (Register_Disk) Register_Disk();
  if (Register_SSC) Register_SSC();
  if (Register_Printer) Register_Printer();
  if (Register_Harddisk) Register_Harddisk();
  if (Register_Mouse) Register_Mouse();
  if (Register_Clock) Register_Clock();
  if (Register_Keyboard) Register_Keyboard();
  if (Register_Joystick) Register_Joystick();
}

auto Peripheral_Find_Internal(const char* name) -> Peripheral_t* {
  if (!name) return nullptr;

  Discover_Builtins();
  Peripheral_Plugins_Init();

  for (auto const& p : g_builtin_registry) {
    if (p && strcmp(p->name, name) == 0) {
      return p;
    }
  }

  for (auto const& lp : g_loaded_plugins) {
    if (lp.p && strcmp(lp.p->name, name) == 0) {
      return lp.p;
    }
  }

  // Support legacy configuration names to prevent breakage of existing user
  // setups.
  if (strcmp(name, "No-Slot Clock") == 0 || strcmp(name, "Clock") == 0) {
    return Peripheral_Find_Internal("Clock Card");
  }

  return nullptr;
}

auto Peripheral_GetPluginPath(const char* name) -> const char* {
  if (!name) return nullptr;

  Discover_Builtins();
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
  Discover_Builtins();

  // Internal peripherals (Slot 0)
  Peripheral_t* kbd = Peripheral_Find_Internal("Keyboard");
  if (kbd) {
    Peripheral_Register(kbd, 0);
  }

  Peripheral_t* spkr = Peripheral_Find_Internal("Speaker");
  if (spkr) {
    Peripheral_Register(spkr, 0);
  }

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
  Discover_Builtins();
  Peripheral_Plugins_Init();

  printf("Built-in Peripherals:\n");
  printf("---------------------\n");
  for (auto const& p : g_builtin_registry) {
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

void Peripheral_Plugins_Init() {
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

void Peripheral_Plugins_Shutdown() {
  for (auto& plugin : g_loaded_plugins) {
    if (plugin.handle) {
      dlclose(plugin.handle);
    }
  }
  g_loaded_plugins.clear();
  g_plugins_initialized = false;
}
