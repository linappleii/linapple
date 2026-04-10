#include "Common.h"
#include <SDL3/SDL.h>
#include "Registry.h"
#include "Util_Path.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <sys/stat.h>
#include <cstring>

static std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace((unsigned char)*start)) {
        start++;
    }
    auto end = s.end();
    if (start == end) return "";
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace((unsigned char)*end));
    return std::string(start, end + 1);
}

static std::string unquote(const std::string& s) {
    if (s.length() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.length() - 2);
    }
    return s;
}

Configuration& Configuration::Instance() {
    static Configuration instance;
    return instance;
}

void Configuration::SetPath(const std::string& path) {
    m_path = path;
}

bool Configuration::Load(const std::string& path) {
    m_path = path;
    m_data.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    std::string currentSection = "Default";
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.length() - 2);
            continue;
        }

        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = trim(line.substr(0, pos));
            std::string value = unquote(trim(line.substr(pos + 1)));
            m_data[currentSection][key] = value;
        }
    }
    return true;
}

void Configuration::LoadDefaults() {
    m_data.clear();
    SetInt("Configuration", "Computer Emulation", 3); // Enhanced //e
    SetInt("Configuration", "Keyboard Type", 0);
    SetInt("Configuration", "Keyboard Rocker Switch", 0);
    SetInt("Configuration", "Sound Emulation", 1);
    SetInt("Configuration", "Soundcard Type", 2); // Mockingboard
    SetInt("Configuration", "Joystick 0", 2); // Keyboard
    SetInt("Configuration", "Joystick 1", 0);
    SetInt("Configuration", "Emulation Speed", 10);
    SetInt("Configuration", "Enhance Disk Speed", 1);
    SetInt("Configuration", "Video Emulation", 1);
    SetString("Configuration", "Monochrome Color", "#C0C0C0");
    SetInt("Configuration", "Mouse in slot 4", 0);
    SetInt("Configuration", "Printer idle limit", 10);
    SetInt("Configuration", "Append to printer file", 1);
    SetInt("Configuration", "Harddisk Enable", 0);
    SetInt("Configuration", "Clock Enable", 4);
    SetInt("Configuration", "Slot 6 Autoload", 0);
    SetInt("Configuration", "Save State On Exit", 0);
    SetInt("Configuration", "Fullscreen", 0);
    SetInt("Configuration", "Boot at Startup", 0);
    SetInt("Configuration", "Show Leds", 1);
    SetString("Configuration", "Screen factor", "1.0");
    
    SetString("Preferences", "FTP Server", "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/");
    SetString("Preferences", "FTP ServerHDD", "ftp://ftp.apple.asimov.net/pub/apple_II/images/");
    SetString("Preferences", "FTP UserPass", "anonymous:my-mail@mail.com");
}

bool Configuration::Save() {
    if (m_path.empty()) {
        m_path = Path::GetUserConfigDir() + "linapple.conf";
    }

#ifdef REGISTRY_WRITEABLE
    std::ofstream file(m_path);
    if (!file.is_open()) return false;

    for (auto const& section : m_data) {
        if (section.first != "Default") {
            file << "[" << section.first << "]" << std::endl;
        }
        for (auto const& kv : section.second) {
            file << kv.first << " = " << kv.second << std::endl;
        }
        file << std::endl;
    }
    return true;
#else
    return false;
#endif
}

std::string Configuration::GetString(const std::string& section, const std::string& key, const std::string& defaultValue) {
    if (m_data.count(section) && m_data[section].count(key)) {
        return m_data[section][key];
    }
    
    for (auto const& s : m_data) {
        if (s.second.count(key)) return s.second.at(key);
    }
    return defaultValue;
}

uint32_t Configuration::GetInt(const std::string& section, const std::string& key, uint32_t defaultValue) {
    std::string val = GetString(section, key);
    if (val.empty()) return defaultValue;
    try {
        return std::stoul(val, nullptr, 0);
    } catch (...) {
        return defaultValue;
    }
}

bool Configuration::GetBool(const std::string& section, const std::string& key, bool defaultValue) {
    std::string val = GetString(section, key);
    if (val.empty()) return defaultValue;
    std::string lowVal = val;
    std::transform(lowVal.begin(), lowVal.end(), lowVal.begin(), ::tolower);
    if (lowVal == "true" || lowVal == "1" || lowVal == "yes") return true;
    if (lowVal == "false" || lowVal == "0" || lowVal == "no") return false;
    return defaultValue;
}

void Configuration::SetString(const std::string& section, const std::string& key, const std::string& value) {
    m_data[section][key] = value;
}

void Configuration::SetInt(const std::string& section, const std::string& key, uint32_t value) {
    m_data[section][key] = std::to_string(value);
}

void Configuration::SetBool(const std::string& section, const std::string& key, bool value) {
    m_data[section][key] = value ? "1" : "0";
}

bool ConfigLoadInt(const char* section, const char* key, uint32_t* value) {
    if (Configuration::Instance().GetString(section, key).empty()) return false;
    *value = Configuration::Instance().GetInt(section, key, *value);
    return true;
}

bool ConfigLoadBool(const char* section, const char* key, bool* value) {
    if (Configuration::Instance().GetString(section, key).empty()) return false;
    *value = Configuration::Instance().GetBool(section, key, *value);
    return true;
}

bool ConfigLoadString(const char* section, const char* key, std::string* value) {
    std::string s = Configuration::Instance().GetString(section, key);
    if (s.empty()) return false;
    *value = s;
    return true;
}

void ConfigSaveInt(const char* section, const char* key, uint32_t value) {
    Configuration::Instance().SetInt(section, key, value);
}

char *php_trim(char *c, int len) {
    std::string s(c, len);
    std::string t = trim(s);
    return strdup(t.c_str());
}
