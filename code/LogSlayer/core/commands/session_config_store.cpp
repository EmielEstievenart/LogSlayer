#include "commands/session_config_store.hpp"

#include <cctype>

#include "settings_store.hpp"

namespace slayerlog
{

namespace
{

constexpr std::string_view config_section_prefix = "config:";
constexpr std::string_view config_command_key    = "command";

std::string trim_text(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

std::string config_section_name(std::string_view name)
{
    return std::string(config_section_prefix) + trim_text(name);
}

} // namespace

std::optional<std::string> validate_session_config_name(std::string_view name)
{
    const std::string trimmed_name = trim_text(name);
    if (trimmed_name.empty())
    {
        return "Config name must not be empty";
    }

    for (const char character : trimmed_name)
    {
        if (character == '[' || character == ']' || character == '=' || character == ';' || character == '#' || character == '\r' || character == '\n')
        {
            return std::string("Config name must not contain '") + character + "'";
        }
    }

    return std::nullopt;
}

std::optional<std::string> save_session_config(SettingsStore& settings_store, std::string_view name, const std::vector<std::string>& command_lines)
{
    const auto name_error = validate_session_config_name(name);
    if (name_error.has_value())
    {
        return name_error;
    }

    const std::string section = config_section_name(name);
    settings_store.ini().remove_section(section);
    // The version marker also keeps a zero-command config's section alive in the
    // INI, so an empty session still round-trips through save/load.
    settings_store.ini().set_values(section, "version", {"1"});
    settings_store.ini().set_values(section, std::string(config_command_key), command_lines);

    std::string error_message;
    if (!settings_store.save(error_message))
    {
        return "Failed to save settings: " + error_message;
    }

    return std::nullopt;
}

std::optional<std::vector<std::string>> load_session_config(const SettingsStore& settings_store, std::string_view name)
{
    const std::string section = config_section_name(name);
    if (!settings_store.ini().has_section(section))
    {
        return std::nullopt;
    }

    return settings_store.ini().values(section, config_command_key);
}

std::vector<std::string> list_session_configs(const SettingsStore& settings_store)
{
    std::vector<std::string> names;
    for (const auto& section : settings_store.ini().sections())
    {
        if (section.rfind(config_section_prefix, 0) == 0)
        {
            names.push_back(section.substr(config_section_prefix.size()));
        }
    }

    return names;
}

std::optional<std::string> remove_session_config(SettingsStore& settings_store, std::string_view name)
{
    const std::string section = config_section_name(name);
    if (!settings_store.ini().has_section(section))
    {
        return "No config named '" + trim_text(name) + "'";
    }

    settings_store.ini().remove_section(section);

    std::string error_message;
    if (!settings_store.save(error_message))
    {
        return "Failed to save settings: " + error_message;
    }

    return std::nullopt;
}

} // namespace slayerlog
