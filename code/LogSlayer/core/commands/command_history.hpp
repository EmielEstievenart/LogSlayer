#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "settings_store.hpp"

namespace slayerlog
{

class CommandHistory
{
public:
    explicit CommandHistory(SettingsStore& settings_store, std::size_t max_entries = 200);

    bool load(std::string& error_message);
    bool record_command(std::string_view command_line, std::string& error_message);

    const std::vector<std::string>& entries() const;
    std::vector<std::string> matching_entries(std::string_view query) const;

private:
    static std::string trim(std::string_view text);
    static std::string lowercase(std::string_view text);
    void trim_to_limit();

    SettingsStore& _settings_store;
    std::size_t _max_entries;
    std::vector<std::string> _entries;
};

} // namespace slayerlog
