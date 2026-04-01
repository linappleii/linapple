#include "stdafx.h"
#include "Registry.h"
#include "Util_Path.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <sys/stat.h>

// Helper to trim strings
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
            std::string value = trim(line.substr(pos + 1));
            m_data[currentSection][key] = value;
        }
    }
    return true;
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

// C-style wrapper for legacy LOAD macro
bool Config_Load(const char* section, const char* key, uint32_t* value) {
    if (Configuration::Instance().GetString(section, key).empty()) return false;
    *value = Configuration::Instance().GetInt(section, key, *value);
    return true;
}

char *php_trim(char *c, int len) {
    std::string s(c, len);
    std::string t = trim(s);
    return strdup(t.c_str());
}
