#include <cstdint>
#include <string>
#include <map>

#pragma once

// Modern C++ Configuration API
class Configuration {
public:
    static Configuration& Instance();

    bool Load(const std::string& path);
    bool Save();
    void SetPath(const std::string& path);
    const std::string& GetPath() const { return m_path; }

    std::string GetString(const std::string& section, const std::string& key, const std::string& defaultValue = "");
    uint32_t GetInt(const std::string& section, const std::string& key, uint32_t defaultValue = 0);
    bool GetBool(const std::string& section, const std::string& key, bool defaultValue = false);

    void SetString(const std::string& section, const std::string& key, const std::string& value);
    void SetInt(const std::string& section, const std::string& key, uint32_t value);
    void SetBool(const std::string& section, const std::string& key, bool value);

private:
    Configuration() {}
    std::string m_path;
    std::map<std::string, std::map<std::string, std::string>> m_data;
};

bool Config_Load(const char* section, const char* key, uint32_t* value);

char *php_trim(char *c, int len);
