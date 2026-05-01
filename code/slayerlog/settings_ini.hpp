#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace slayerlog
{

struct IniKeyValue
{
    std::string key;
    std::string value;
};

struct IniSection
{
    std::string name;
    std::vector<IniKeyValue> entries;
};

class SettingsIni
{
public:
    bool parse(std::string_view text, std::string& error_message);
    std::string serialize() const;

    std::vector<std::string> values(std::string_view section, std::string_view key) const;
    void set_values(std::string section, std::string key, const std::vector<std::string>& values);

private:
    IniSection* find_section(std::string_view name);
    const IniSection* find_section(std::string_view name) const;

    std::vector<IniSection> _sections;
};

} // namespace slayerlog
