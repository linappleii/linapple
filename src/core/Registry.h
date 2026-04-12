#include <cstdint>
#include <string>
#include <map>

#pragma once

class Configuration {
public:
    static auto Instance() -> Configuration&;

    auto Load(const std::string& path) -> bool;
    void LoadDefaults();
    auto Save() -> bool;
    void SetPath(const std::string& path);
    auto GetPath() const -> const std::string& { return m_path; }

    auto GetString(const std::string& section, const std::string& key, const std::string& defaultValue = "") -> std::string;
    auto GetInt(const std::string& section, const std::string& key, uint32_t defaultValue = 0) -> uint32_t;
    auto GetBool(const std::string& section, const std::string& key, bool defaultValue = false) -> bool;

    void SetString(const std::string& section, const std::string& key, const std::string& value);
    void SetInt(const std::string& section, const std::string& key, uint32_t value);
    void SetBool(const std::string& section, const std::string& key, bool value);

private:
    Configuration() {}
    std::string m_path;
    std::map<std::string, std::map<std::string, std::string>> m_data;
};

auto ConfigLoadInt(const char* section, const char* key, uint32_t* value) -> bool;
auto ConfigLoadBool(const char* section, const char* key, bool* value) -> bool;
auto ConfigLoadString(const char* section, const char* key, std::string* value) -> bool;
void ConfigSaveInt(const char* section, const char* key, uint32_t value);

auto php_trim(char *c, int len) -> char *;
