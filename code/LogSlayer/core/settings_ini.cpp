#include "settings_ini.hpp"

#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include <SimpleIni.h>

#include <cctype>

namespace slayerlog
{

namespace
{

std::string simple_ini_error_message(SI_Error error)
{
    switch (error)
    {
    case SI_NOMEM:
        return "Failed to parse INI data: out of memory";
    case SI_FILE:
        return "Failed to parse INI data: file is too large";
    case SI_FAIL:
        return "Failed to parse INI data";
    default:
        return "Failed to parse INI data: error " + std::to_string(error);
    }
}

std::string trim_copy(std::string_view text)
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

bool validate_section_headers(std::string_view text, std::string& error_message)
{
    std::size_t line_number = 1;
    std::size_t line_start  = 0;
    while (line_start <= text.size())
    {
        const std::size_t line_end = text.find('\n', line_start);
        const std::size_t count    = line_end == std::string_view::npos ? text.size() - line_start : line_end - line_start;
        std::string line           = trim_copy(text.substr(line_start, count));
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
            line = trim_copy(line);
        }

        if (!line.empty() && line.front() == '[' && line.find(']') == std::string::npos)
        {
            error_message = "Malformed settings INI section header at line " + std::to_string(line_number) + ": missing closing ]";
            return false;
        }

        if (line_end == std::string_view::npos)
        {
            break;
        }

        line_start = line_end + 1;
        ++line_number;
    }

    return true;
}

} // namespace

struct SettingsIni::Impl
{
    Impl() : ini(false, true, false) { ini.SetSpaces(false); }

    CSimpleIniCaseA ini;
};

SettingsIni::SettingsIni() : _impl(std::make_unique<Impl>())
{
}

SettingsIni::~SettingsIni() = default;

SettingsIni::SettingsIni(SettingsIni&&) noexcept = default;

SettingsIni& SettingsIni::operator=(SettingsIni&&) noexcept = default;

bool SettingsIni::parse(std::string_view text, std::string& error_message)
{
    _impl->ini.Reset();
    error_message.clear();

    if (!validate_section_headers(text, error_message))
    {
        return false;
    }

    const std::string input(text);
    const SI_Error result = _impl->ini.LoadData(input);
    if (result < 0)
    {
        error_message = simple_ini_error_message(result);
        _impl->ini.Reset();
        return false;
    }

    return true;
}

std::string SettingsIni::serialize() const
{
    std::string output;
    _impl->ini.Save(output, false);
    return output;
}

std::vector<std::string> SettingsIni::values(std::string_view section, std::string_view key) const
{
    const std::string section_name(section);
    const std::string key_name(key);

    CSimpleIniCaseA::TNamesDepend simple_ini_values;
    if (!_impl->ini.GetAllValues(section_name.c_str(), key_name.c_str(), simple_ini_values))
    {
        return {};
    }

    simple_ini_values.sort(CSimpleIniCaseA::Entry::LoadOrder());

    std::vector<std::string> extracted_values;
    for (const auto& entry : simple_ini_values)
    {
        extracted_values.emplace_back(entry.pItem);
    }

    return extracted_values;
}

void SettingsIni::set_values(std::string section, std::string key, const std::vector<std::string>& values)
{
    _impl->ini.Delete(section.c_str(), key.c_str(), false);

    for (const auto& value : values)
    {
        _impl->ini.SetValue(section.c_str(), key.c_str(), value.c_str());
    }
}

std::vector<std::string> SettingsIni::sections() const
{
    CSimpleIniCaseA::TNamesDepend simple_ini_sections;
    _impl->ini.GetAllSections(simple_ini_sections);
    simple_ini_sections.sort(CSimpleIniCaseA::Entry::LoadOrder());

    std::vector<std::string> extracted_sections;
    for (const auto& entry : simple_ini_sections)
    {
        extracted_sections.emplace_back(entry.pItem);
    }

    return extracted_sections;
}

bool SettingsIni::has_section(std::string_view section) const
{
    const std::string section_name(section);
    return _impl->ini.GetSection(section_name.c_str()) != nullptr;
}

void SettingsIni::remove_section(std::string_view section)
{
    const std::string section_name(section);
    _impl->ini.Delete(section_name.c_str(), nullptr, true);
}

} // namespace slayerlog
