#include "settings_store.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace slayerlog
{

namespace
{

std::filesystem::path fallback_settings_file_path()
{
    try
    {
        return std::filesystem::current_path() / "slayerlog_settings.ini";
    }
    catch (...)
    {
        return "slayerlog_settings.ini";
    }
}

std::string env_value(const char* variable_name)
{
#ifdef _WIN32
    char* raw_value      = nullptr;
    std::size_t raw_size = 0;
    if (_dupenv_s(&raw_value, &raw_size, variable_name) != 0 || raw_value == nullptr || raw_size <= 1)
    {
        if (raw_value != nullptr)
        {
            std::free(raw_value);
        }

        return {};
    }

    std::string value = raw_value;
    std::free(raw_value);
    return value;
#else
    const char* value = std::getenv(variable_name);
    return value != nullptr && value[0] != '\0' ? std::string(value) : std::string();
#endif
}

} // namespace

std::filesystem::path default_settings_file_path()
{
#ifdef _WIN32
    const std::string local_app_data = env_value("LOCALAPPDATA");
    if (!local_app_data.empty())
    {
        return std::filesystem::path(local_app_data) / "slayerlog" / "settings.ini";
    }

    const std::string app_data = env_value("APPDATA");
    if (!app_data.empty())
    {
        return std::filesystem::path(app_data) / "slayerlog" / "settings.ini";
    }
#elif defined(__APPLE__)
    const std::string home = env_value("HOME");
    if (!home.empty())
    {
        return std::filesystem::path(home) / "Library" / "Application Support" / "slayerlog" / "settings.ini";
    }
#else
    const std::string xdg_config_home = env_value("XDG_CONFIG_HOME");
    if (!xdg_config_home.empty())
    {
        return std::filesystem::path(xdg_config_home) / "slayerlog" / "settings.ini";
    }

    const std::string home = env_value("HOME");
    if (!home.empty())
    {
        return std::filesystem::path(home) / ".config" / "slayerlog" / "settings.ini";
    }
#endif

    return fallback_settings_file_path();
}

SettingsStore::SettingsStore(std::filesystem::path file_path) : _file_path(std::move(file_path))
{
}

bool SettingsStore::load(std::string& error_message)
{
    error_message.clear();

    std::error_code error_code;
    if (!std::filesystem::exists(_file_path, error_code))
    {
        return true;
    }

    std::ifstream input(_file_path, std::ios::binary);
    if (!input)
    {
        error_message = "Failed to open settings file for reading: " + _file_path.string();
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof())
    {
        error_message = "Failed to read settings file: " + _file_path.string();
        return false;
    }

    return _ini.parse(buffer.str(), error_message);
}

bool SettingsStore::save(std::string& error_message) const
{
    error_message.clear();

    std::error_code error_code;
    const std::filesystem::path parent_path = _file_path.parent_path();
    if (!parent_path.empty())
    {
        std::filesystem::create_directories(parent_path, error_code);
        if (error_code)
        {
            error_message = "Failed to create settings directory: " + parent_path.string();
            return false;
        }
    }

    const std::filesystem::path temporary_path = _file_path.string() + ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error_message = "Failed to open temporary settings file for writing: " + temporary_path.string();
            return false;
        }

        output << _ini.serialize();
        output.flush();
        if (!output)
        {
            error_message = "Failed to write settings file: " + temporary_path.string();
            return false;
        }
    }

    if (std::filesystem::exists(_file_path, error_code))
    {
        std::filesystem::remove(_file_path, error_code);
        if (error_code)
        {
            std::filesystem::remove(temporary_path, error_code);
            error_message = "Failed to replace settings file: " + _file_path.string();
            return false;
        }
    }

    std::filesystem::rename(temporary_path, _file_path, error_code);
    if (error_code)
    {
        std::filesystem::remove(temporary_path, error_code);
        error_message = "Failed to finalize settings file: " + _file_path.string();
        return false;
    }

    return true;
}

bool SettingsStore::ensure_default_values(std::string_view section, std::string_view key, const std::vector<std::string>& values, std::string& error_message)
{
    error_message.clear();

    if (!_ini.values(section, key).empty())
    {
        return true;
    }

    _ini.set_values(std::string(section), std::string(key), values);
    return save(error_message);
}

const std::filesystem::path& SettingsStore::file_path() const
{
    return _file_path;
}

SettingsIni& SettingsStore::ini()
{
    return _ini;
}

const SettingsIni& SettingsStore::ini() const
{
    return _ini;
}

} // namespace slayerlog
