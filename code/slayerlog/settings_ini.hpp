#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace slayerlog
{

class SettingsIni
{
public:
    SettingsIni();
    ~SettingsIni();

    SettingsIni(SettingsIni&&) noexcept;
    SettingsIni& operator=(SettingsIni&&) noexcept;
    SettingsIni(const SettingsIni&)            = delete;
    SettingsIni& operator=(const SettingsIni&) = delete;

    bool parse(std::string_view text, std::string& error_message);
    std::string serialize() const;

    std::vector<std::string> values(std::string_view section, std::string_view key) const;
    void set_values(std::string section, std::string key, const std::vector<std::string>& values);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace slayerlog
