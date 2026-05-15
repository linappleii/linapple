#include "frontends/common/AppEnvironment.h"

#include <unistd.h>

#include <string>
#include <vector>

#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

static constexpr const char* CONFIG_FILE_NAME = "linapple.conf";

void AppEnv_ResolvePaths(AppConfig* config) {
  if (config == nullptr) {
    return;
  }

  std::vector<std::string> searchPaths;

  // 1. Explicit --config CLI override
  if (config->szConfigPath.at(0) != '\0') {
    searchPaths.emplace_back(config->szConfigPath.data());
  }

  // 2. XDG Base Directory Specification (~/.config/linapple/)
  searchPaths.emplace_back(Path::GetUserConfigDir() + CONFIG_FILE_NAME);

  // 3. Current Working Directory
  searchPaths.emplace_back(CONFIG_FILE_NAME);

  // 4. System-wide installation paths
  // FindDataFile handles /etc/linapple/ and /usr/share/linapple/ via
  // GetDataSearchPaths
  searchPaths.emplace_back(Path::FindDataFile(CONFIG_FILE_NAME));

  std::string finalPath;
  bool loaded = false;

  for (const auto& path : searchPaths) {
    if (path.empty()) continue;
    if (Configuration::Instance().Load(path)) {
      finalPath = path;
      loaded = true;
      break;
    }
  }

  // Fallback: if nothing loaded, use XDG path even if it doesn't exist yet
  if (!loaded) {
    finalPath = Path::GetUserConfigDir() + CONFIG_FILE_NAME;
    Path::EnsureDirExists(Path::GetUserConfigDir());
    // We don't call Load() again here as we know it's not there or failed,
    // we just want to set the path where it *should* be saved later.
  }

  // Populate back to config
  Util_SafeStrCpy(config->szConfigPath.data(), finalPath.c_str(), PATH_MAX_LEN);

  // Consolidate Logger initialization
  Logger::Initialize();

  // Set verbosity based on config
  if (config->bVerbose) {
    Logger::SetVerbosity(LogLevel::kPerf);
  } else if (config->bLog) {
    Logger::SetVerbosity(LogLevel::kInfo);
  } else {
    // Default to errors and warnings only to keep console clean for normal use
    Logger::SetVerbosity(LogLevel::kWarning);
  }
}
