#include "settings_ini.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace slayerlog
{

namespace
{

std::string trim(std::string_view text)
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

std::vector<std::string_view> split_lines(std::string_view text)
{
    std::vector<std::string_view> lines;

    std::size_t line_start = 0;
    while (line_start <= text.size())
    {
        const std::size_t line_end = text.find('\n', line_start);
        if (line_end == std::string_view::npos)
        {
            lines.push_back(text.substr(line_start));
            break;
        }

        lines.push_back(text.substr(line_start, line_end - line_start));
        line_start = line_end + 1;
    }

    return lines;
}

std::string to_line(std::string_view line)
{
    if (!line.empty() && line.back() == '\r')
    {
        return std::string(line.substr(0, line.size() - 1));
    }

    return std::string(line);
}

} // namespace

bool SettingsIni::parse(std::string_view text, std::string& error_message)
{
    _sections.clear();
    error_message.clear();

    IniSection* current_section = nullptr;

    const auto lines = split_lines(text);
    for (std::size_t line_number = 0; line_number < lines.size(); ++line_number)
    {
        std::string line         = to_line(lines[line_number]);
        const std::string parsed = trim(line);
        if (parsed.empty() || parsed[0] == ';' || parsed[0] == '#')
        {
            continue;
        }

        if (parsed.front() == '[')
        {
            if (parsed.back() != ']')
            {
                error_message = "Invalid section header at line " + std::to_string(line_number + 1);
                _sections.clear();
                return false;
            }

            const std::string section_name = trim(parsed.substr(1, parsed.size() - 2));
            if (section_name.empty())
            {
                error_message = "Empty section name at line " + std::to_string(line_number + 1);
                _sections.clear();
                return false;
            }

            current_section = find_section(section_name);
            if (current_section == nullptr)
            {
                _sections.push_back(IniSection {section_name, {}});
                current_section = &_sections.back();
            }

            continue;
        }

        const std::size_t separator_index = parsed.find('=');
        if (separator_index == std::string::npos)
        {
            error_message = "Invalid key-value entry at line " + std::to_string(line_number + 1);
            _sections.clear();
            return false;
        }

        const std::string key   = trim(parsed.substr(0, separator_index));
        const std::string value = trim(parsed.substr(separator_index + 1));
        if (key.empty())
        {
            error_message = "Empty key at line " + std::to_string(line_number + 1);
            _sections.clear();
            return false;
        }

        if (current_section == nullptr)
        {
            _sections.push_back(IniSection {});
            current_section = &_sections.back();
        }

        current_section->entries.push_back(IniKeyValue {key, value});
    }

    return true;
}

std::string SettingsIni::serialize() const
{
    std::ostringstream output;

    bool first_section = true;
    for (const auto& section : _sections)
    {
        if (!first_section)
        {
            output << '\n';
        }
        first_section = false;

        if (!section.name.empty())
        {
            output << '[' << section.name << "]\n";
        }

        for (const auto& entry : section.entries)
        {
            output << entry.key << '=' << entry.value << '\n';
        }
    }

    return output.str();
}

std::vector<std::string> SettingsIni::values(std::string_view section, std::string_view key) const
{
    const auto* existing_section = find_section(section);
    if (existing_section == nullptr)
    {
        return {};
    }

    std::vector<std::string> extracted_values;
    for (const auto& entry : existing_section->entries)
    {
        if (entry.key == key)
        {
            extracted_values.push_back(entry.value);
        }
    }

    return extracted_values;
}

void SettingsIni::set_values(std::string section, std::string key, const std::vector<std::string>& values)
{
    IniSection* target_section = find_section(section);
    if (target_section == nullptr)
    {
        _sections.push_back(IniSection {std::move(section), {}});
        target_section = &_sections.back();
    }

    target_section->entries.erase(std::remove_if(target_section->entries.begin(), target_section->entries.end(),
                                                 [&](const IniKeyValue& entry) { return entry.key == key; }),
                                  target_section->entries.end());

    for (const auto& value : values)
    {
        target_section->entries.push_back(IniKeyValue {key, value});
    }
}

IniSection* SettingsIni::find_section(std::string_view name)
{
    const auto match = std::find_if(_sections.begin(), _sections.end(), [&](const IniSection& section) { return section.name == name; });
    return match == _sections.end() ? nullptr : &*match;
}

const IniSection* SettingsIni::find_section(std::string_view name) const
{
    const auto match = std::find_if(_sections.begin(), _sections.end(), [&](const IniSection& section) { return section.name == name; });
    return match == _sections.end() ? nullptr : &*match;
}

} // namespace slayerlog
