// SPDX-License-Identifier: GPL-2.0-only
#include "Peripheral_Internal.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-type-reinterpret-cast,misc-include-cleaner,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-init-variables):
// Dynamic peripheral plugin loading and internal registry inspection
#include <dirent.h>
#include <dlfcn.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "LinAppleCore.h"
#include "apple2/SnapshotTypes.h"
#include "core/Log.h"
#include "core/Util_Path.h"

struct LoadedPlugin_t {
  Peripheral_t* p;
  void* handle;
  std::string path;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::vector<LoadedPlugin_t> g_loaded_plugins;
static bool g_plugins_initialized = false;

extern auto Peripheral_GetBuiltinRegistry() -> std::vector<Peripheral_t*>&;

auto Peripheral_Find_Internal(const char* name) -> Peripheral_t* {
  if (name == nullptr) {
    return nullptr;
  }

  Peripheral_Plugins_Init();

  for (auto const& p : Peripheral_GetBuiltinRegistry()) {
    if (p != nullptr &&
        (strcmp(p->name, name) == 0 || strcmp(p->id, name) == 0)) {
      return p;
    }
  }

  for (auto const& lp : g_loaded_plugins) {
    if (lp.p != nullptr &&
        (strcmp(lp.p->name, name) == 0 || strcmp(lp.p->id, name) == 0)) {
      return lp.p;
    }
  }

  if (strcmp(name, "No-Slot Clock") == 0 || strcmp(name, "Clock") == 0) {
    return Peripheral_Find_Internal("Clock Card");
  }

  return nullptr;
}

auto Peripheral_GetPluginPath(const char* name) -> const char* {
  if (name == nullptr) {
    return nullptr;
  }

  Peripheral_Plugins_Init();

  for (auto const& lp : g_loaded_plugins) {
    if (lp.p != nullptr &&
        (strcmp(lp.p->name, name) == 0 || strcmp(lp.p->id, name) == 0)) {
      return lp.path.c_str();
    }
  }
  return nullptr;
}

auto Peripheral_Register_Internal() -> void {
  for (auto* p : Peripheral_GetBuiltinRegistry()) {
    if (p != nullptr && p->default_slot == 0) {
      Peripheral_Register(p, 0);
    }
  }

  for (int slot = 1; slot < NUM_SLOTS; ++slot) {
    constexpr size_t key_size = 16;
    char key[key_size];
    snprintf(key, sizeof(key), "Slot %d", slot);

    std::string name;
    bool in_config = ConfigLoadString("Slots", key, &name);

    if (in_config) {
      if (name == "None" || name.empty()) {
        continue;
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
      } else if (slot == 7 && hddenabled) {
        name = "linapple.harddisk";
      }
      if (name.empty()) {
        continue;
      }
    }

    Peripheral_t* p = Peripheral_Find_Internal(name.c_str());
    if (p != nullptr) {
      Peripheral_Register(p, slot);
    }
  }
}

auto Linapple_ListHardware() -> void {
  Peripheral_Plugins_Init();

  printf("Built-in Peripherals:\n");
  printf("---------------------\n");
  for (auto const& p : Peripheral_GetBuiltinRegistry()) {
    if (p != nullptr) {
      printf("- %-24s [%s] v%s\n", p->name, p->id, p->version);
      printf("  Author: %s\n", p->author);
      printf("  Desc:   %s\n", p->description);
      printf("  Slots:  ");
      bool first = true;
      for (int i = 0; i < NUM_SLOTS; ++i) {
        if (p->compatible_slots & (1u << static_cast<uint32_t>(i))) {
          if (!first) printf(", ");
          printf("%d", i);
          first = false;
        }
      }
      printf("\n\n");
    }
  }

  if (!g_loaded_plugins.empty()) {
    printf("Dynamically Loaded Peripherals:\n");
    printf("-------------------------------\n");
    for (auto const& plugin : g_loaded_plugins) {
      printf("- %-24s [%s] v%s\n", plugin.p->name, plugin.p->id,
             plugin.p->version);
      printf("  Path:   %s\n", plugin.path.c_str());
      printf("  Author: %s\n", plugin.p->author);
      printf("  Desc:   %s\n", plugin.p->description);
      printf("  Slots:  ");
      bool first = true;
      for (int i = 0; i < NUM_SLOTS; ++i) {
        if (plugin.p->compatible_slots & (1u << static_cast<uint32_t>(i))) {
          if (!first) printf(", ");
          printf("%d", i);
          first = false;
        }
      }
      printf("\n\n");
    }
  }
}

auto Peripheral_Plugins_Init() -> void {
  if (g_plugins_initialized) {
    return;
  }
  g_plugins_initialized = true;

  auto paths = Path::GetPluginSearchPaths();
  for (const auto& path : paths) {
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
      continue;
    }

    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
      std::string filename = ent->d_name;
      if (filename.length() > 3 &&
          filename.substr(filename.length() - 3) == ".so") {
        if (filename.find('/') != std::string::npos) {
          continue;
        }
        std::string full_path = Path::Join(path, filename);
        void* handle = dlopen(full_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle != nullptr) {
          auto* p = reinterpret_cast<Peripheral_t*>(
              dlsym(handle, "linapple_peripheral_descriptor"));
          if (p != nullptr) {
            if (p->abi_version == LINAPPLE_ABI_VERSION) {
              Logger::Info("Loaded plugin: %s from %s\n", p->name,
                           full_path.c_str());
              g_loaded_plugins.push_back({p, handle, full_path});
            } else {
              Logger::Warning("Plugin ABI mismatch: %s (expected %d, got %d)\n",
                              full_path.c_str(), LINAPPLE_ABI_VERSION,
                              p->abi_version);
              dlclose(handle);
            }
          } else {
            Logger::Warning(
                "Invalid plugin (missing linapple_peripheral_descriptor): %s\n",
                full_path.c_str());
            dlclose(handle);
          }
        } else {
          Logger::Warning("Failed to load plugin: %s (%s)\n", full_path.c_str(),
                          dlerror());
        }
      }
    }
    closedir(dir);
  }
}

auto Peripheral_Plugins_Shutdown() -> void {
  for (auto& plugin : g_loaded_plugins) {
    if (plugin.handle != nullptr) {
      dlclose(plugin.handle);
    }
  }
  g_loaded_plugins.clear();
  g_plugins_initialized = false;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-type-reinterpret-cast,misc-include-cleaner,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-init-variables)
