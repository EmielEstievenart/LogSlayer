#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "settings_ini.hpp"

namespace slayerlog
{

std::filesystem::path default_settings_file_path();

class SettingsStore
{
public:
    explicit SettingsStore(std::filesystem::path file_path);

    bool load(std::string& error_message);
    bool save(std::string& error_message) const;
    bool ensure_default_values(std::string_view section, std::string_view key, const std::vector<std::string>& values, std::string& error_message);

    const std::filesystem::path& file_path() const;
    SettingsIni& ini();
    const SettingsIni& ini() const;

private:
    std::filesystem::path _file_path;
    SettingsIni _ini;
};

} // namespace slayerlog
