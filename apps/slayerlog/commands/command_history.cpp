#include "command_history.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace slayerlog
{

namespace
{

constexpr std::string_view command_history_section = "command_history";
constexpr std::string_view command_entry_key       = "entry";

} // namespace

CommandHistory::CommandHistory(SettingsStore& settings_store, std::size_t max_entries) : _settings_store(settings_store), _max_entries(std::max<std::size_t>(max_entries, 1))
{
}

bool CommandHistory::load(std::string& error_message)
{
    if (!_settings_store.load(error_message))
    {
        return false;
    }

    _entries.clear();
    const auto loaded_entries = _settings_store.ini().values(command_history_section, command_entry_key);
    std::unordered_set<std::string> seen_entries;
    _entries.reserve(loaded_entries.size());
    for (const auto& entry : loaded_entries)
    {
        const std::string trimmed = trim(entry);
        if (!trimmed.empty() && seen_entries.insert(trimmed).second)
        {
            _entries.push_back(trimmed);
        }
    }

    trim_to_limit();
    return true;
}

bool CommandHistory::record_command(std::string_view command_line, std::string& error_message)
{
    error_message.clear();

    const std::string trimmed_command = trim(command_line);
    if (trimmed_command.empty())
    {
        return true;
    }

    _entries.erase(std::remove(_entries.begin(), _entries.end(), trimmed_command), _entries.end());
    _entries.insert(_entries.begin(), trimmed_command);
    trim_to_limit();

    _settings_store.ini().set_values(std::string(command_history_section), std::string(command_entry_key), _entries);
    return _settings_store.save(error_message);
}

const std::vector<std::string>& CommandHistory::entries() const
{
    return _entries;
}

std::vector<std::string> CommandHistory::matching_entries(std::string_view query) const
{
    const std::string normalized_query = lowercase(trim(query));
    if (normalized_query.empty())
    {
        return _entries;
    }

    std::vector<std::string> matches;
    for (const auto& entry : _entries)
    {
        if (lowercase(entry).find(normalized_query) != std::string::npos)
        {
            matches.push_back(entry);
        }
    }

    return matches;
}

std::string CommandHistory::trim(std::string_view text)
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

std::string CommandHistory::lowercase(std::string_view text)
{
    std::string lowered;
    lowered.reserve(text.size());
    for (const char value : text)
    {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
    }

    return lowered;
}

void CommandHistory::trim_to_limit()
{
    if (_entries.size() > _max_entries)
    {
        _entries.resize(_max_entries);
    }
}

} // namespace slayerlog
