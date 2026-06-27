#include "Peripheral_Internal.h"

#include <dirent.h>
#include <dlfcn.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "LinAppleCore.h"
#include "apple2/SnapshotTypes.h"
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

extern auto Peripheral_GetBuiltinRegistry() -> std::vector<Peripheral_t*>&;

static bool g_plugins_initialized = false;

auto Peripheral_Find_Internal(const char* name) -> Peripheral_t* {
  if (!name) return nullptr;

  Peripheral_Plugins_Init();

  for (auto const& p : Peripheral_GetBuiltinRegistry()) {
    if (p && (strcmp(p->name, name) == 0 || strcmp(p->id, name) == 0)) {
      return p;
    }
  }

  for (auto const& lp : g_loaded_plugins) {
    if (lp.p && (strcmp(lp.p->name, name) == 0 || strcmp(lp.p->id, name) == 0)) {
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

  Peripheral_Plugins_Init();

  for (auto const& lp : g_loaded_plugins) {
    if (lp.p && (strcmp(lp.p->name, name) == 0 || strcmp(lp.p->id, name) == 0)) {
      return lp.path.c_str();
    }
  }
  return nullptr;
}

void Peripheral_Register_Internal() {
  // Internal peripherals (Slot 0)
  for (auto* p : Peripheral_GetBuiltinRegistry()) {
    if (p->default_slot == 0) {
      Peripheral_Register(p, 0);
    }
  }

  for (int slot = 1; slot < NUM_SLOTS; ++slot) {
    const size_t KEY_SIZE = 16;
    char key[KEY_SIZE];
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
    if (p) {
      Peripheral_Register(p, slot);
    }
  }
}

void Linapple_ListHardware() {
  Peripheral_Plugins_Init();

  printf("Built-in Peripherals:\n");
  printf("---------------------\n");
  for (auto const& p : Peripheral_GetBuiltinRegistry()) {
    if (p) {
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
