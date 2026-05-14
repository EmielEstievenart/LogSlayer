#include "settings_ini.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <SimpleIni.h>

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

} // namespace

struct SettingsIni::Impl
{
    Impl() : ini(false, true, false)
    {
        ini.SetSpaces(false);
    }

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

} // namespace slayerlog
