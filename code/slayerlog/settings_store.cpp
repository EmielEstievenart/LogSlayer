#include "settings_store.hpp"

#include "debug_log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
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

std::string timestamp_suffix()
{
    const auto now    = std::chrono::system_clock::now();
    const auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm local_time {};

#ifdef _WIN32
    localtime_s(&local_time, &time_t);
#else
    localtime_r(&time_t, &local_time);
#endif

    std::ostringstream stream;
    stream << std::put_time(&local_time, "%Y%m%d-%H%M%S");
    return stream.str();
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
    SLAYERLOG_LOG_INFO("Loading settings file path=" << _file_path.string());

    std::error_code error_code;
    const bool file_exists = std::filesystem::exists(_file_path, error_code);
    if (error_code)
    {
        error_message = "Failed to check settings file existence: " + _file_path.string() + " error=" + error_code.message();
        SLAYERLOG_LOG_ERROR(error_message);
        return false;
    }

    if (!file_exists)
    {
        SLAYERLOG_LOG_INFO("Settings file does not exist yet path=" << _file_path.string());
        return true;
    }

    std::ifstream input(_file_path, std::ios::binary);
    if (!input)
    {
        error_message = "Failed to open settings file for reading: " + _file_path.string();
        SLAYERLOG_LOG_ERROR(error_message);
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof())
    {
        error_message = "Failed to read settings file: " + _file_path.string();
        SLAYERLOG_LOG_ERROR(error_message);
        return false;
    }

    if (!_ini.parse(buffer.str(), error_message))
    {
        SLAYERLOG_LOG_ERROR("Failed to parse settings file path=" << _file_path.string() << " error=" << error_message);
        return false;
    }

    SLAYERLOG_LOG_INFO("Loaded settings file path=" << _file_path.string());
    return true;
}

bool SettingsStore::save(std::string& error_message) const
{
    error_message.clear();
    SLAYERLOG_LOG_INFO("Saving settings file path=" << _file_path.string());

    std::error_code error_code;
    const std::filesystem::path parent_path = _file_path.parent_path();
    if (!parent_path.empty())
    {
        std::filesystem::create_directories(parent_path, error_code);
        if (error_code)
        {
            error_message = "Failed to create settings directory: " + parent_path.string();
            SLAYERLOG_LOG_ERROR(error_message << " error=" << error_code.message());
            return false;
        }
    }

    const std::filesystem::path temporary_path = _file_path.string() + ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error_message = "Failed to open temporary settings file for writing: " + temporary_path.string();
            SLAYERLOG_LOG_ERROR(error_message);
            return false;
        }

        output << _ini.serialize();
        output.flush();
        if (!output)
        {
            error_message = "Failed to write settings file: " + temporary_path.string();
            SLAYERLOG_LOG_ERROR(error_message);
            return false;
        }
    }

    if (std::filesystem::exists(_file_path, error_code))
    {
        const auto backup_path = make_backup_file_path();
        std::filesystem::copy_file(_file_path, backup_path, std::filesystem::copy_options::none, error_code);
        if (error_code)
        {
            const auto backup_error_message = error_code.message();
            std::error_code cleanup_error;
            std::filesystem::remove(temporary_path, cleanup_error);
            error_message = "Failed to back up settings file before replace: " + backup_path.string();
            SLAYERLOG_LOG_ERROR(error_message << " error=" << backup_error_message);
            return false;
        }

        SLAYERLOG_LOG_INFO("Backed up settings file source=" << _file_path.string() << " backup=" << backup_path.string());

        std::filesystem::remove(_file_path, error_code);
        if (error_code)
        {
            const auto remove_error_message = error_code.message();
            std::error_code cleanup_error;
            std::filesystem::remove(temporary_path, cleanup_error);
            error_message = "Failed to replace settings file: " + _file_path.string();
            SLAYERLOG_LOG_ERROR(error_message << " error=" << remove_error_message);
            return false;
        }
    }
    else if (error_code)
    {
        const auto exists_error_message = error_code.message();
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_path, cleanup_error);
        error_message = "Failed to check settings file before replace: " + _file_path.string();
        SLAYERLOG_LOG_ERROR(error_message << " error=" << exists_error_message);
        return false;
    }

    std::filesystem::rename(temporary_path, _file_path, error_code);
    if (error_code)
    {
        const auto rename_error_message = error_code.message();
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_path, cleanup_error);
        error_message = "Failed to finalize settings file: " + _file_path.string();
        SLAYERLOG_LOG_ERROR(error_message << " error=" << rename_error_message);
        return false;
    }

    SLAYERLOG_LOG_INFO("Saved settings file path=" << _file_path.string());
    return true;
}

bool SettingsStore::ensure_default_values(std::string_view section, std::string_view key, const std::vector<std::string>& values, std::string& error_message)
{
    error_message.clear();

    const auto existing_values = _ini.values(section, key);
    if (existing_values.empty())
    {
        SLAYERLOG_LOG_INFO("Seeding settings defaults path=" << _file_path.string() << " section=" << section << " key=" << key << " count=" << values.size());
        _ini.set_values(std::string(section), std::string(key), values);
        return save(error_message);
    }

    std::vector<std::string> merged_values = values;
    for (const auto& existing_value : existing_values)
    {
        if (std::find(merged_values.begin(), merged_values.end(), existing_value) == merged_values.end())
        {
            merged_values.push_back(existing_value);
        }
    }

    if (merged_values == existing_values)
    {
        SLAYERLOG_LOG_INFO("Settings defaults already present path=" << _file_path.string() << " section=" << section << " key=" << key);
        return true;
    }

    SLAYERLOG_LOG_INFO("Merging settings defaults path=" << _file_path.string() << " section=" << section << " key=" << key << " existing_count=" << existing_values.size() << " merged_count=" << merged_values.size());
    _ini.set_values(std::string(section), std::string(key), merged_values);
    return save(error_message);
}

const std::filesystem::path& SettingsStore::file_path() const
{
    return _file_path;
}

std::filesystem::path SettingsStore::make_backup_file_path() const
{
    std::filesystem::path candidate = _file_path.string() + "." + timestamp_suffix() + ".bak";
    std::error_code error_code;
    for (int index = 1; std::filesystem::exists(candidate, error_code); ++index)
    {
        candidate = _file_path.string() + "." + timestamp_suffix() + "." + std::to_string(index) + ".bak";
        error_code.clear();
    }

    return candidate;
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
