#include "frontends/common/AppEnvironment.h"
#include <unistd.h>
#include <string>
#include "core/Util_Path.h"
#include "core/Registry.h"
#include "core/Log.h"
#include "core/Util_Text.h"

void AppEnv_ResolvePaths(AppConfig* config) {
    if (!config) return;

    std::string configPath;

    // 1. Explicit --config CLI override
    if (config->szConfigPath[0] != '\0') {
        if (access(config->szConfigPath, R_OK) == 0) {
            configPath = config->szConfigPath;
        }
    }

    // 2. XDG Base Directory Specification (~/.config/linapple/)
    if (configPath.empty()) {
        std::string xdg = Path::GetUserConfigDir() + "linapple.conf";
        if (access(xdg.c_str(), R_OK) == 0) {
            configPath = xdg;
        }
    }

    // 3. Current Working Directory
    if (configPath.empty()) {
        if (access("linapple.conf", R_OK) == 0) {
            configPath = "linapple.conf";
        }
    }

    // 4. System-wide installation paths
    if (configPath.empty()) {
        // FindDataFile handles /etc/linapple/ and /usr/share/linapple/ via GetDataSearchPaths
        // which includes relative paths from the executable and common system paths.
        configPath = Path::FindDataFile("linapple.conf");
    }

    // Fallback: use XDG path even if it doesn't exist yet
    if (configPath.empty()) {
        configPath = Path::GetUserConfigDir() + "linapple.conf";
        Path::EnsureDirExists(Path::GetUserConfigDir());
    }

    // Populate back to config
    Util_SafeStrCpy(config->szConfigPath, configPath.c_str(), PATH_MAX_LEN);

    // Consolidate Registry (Configuration) initialization.
    // Use the path from config->szConfigPath to ensure consistency if truncation occurred.
    Configuration::Instance().Load(config->szConfigPath);

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
