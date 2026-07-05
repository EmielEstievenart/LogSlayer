#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slayerlog
{

class SettingsStore;

/// The config slot auto-saved on clean exit and replayed by --resume-last.
inline constexpr std::string_view last_session_config_name = "__last";

/**
 * Named session configs persisted in the settings INI. Each config is stored
 * as a [config:<name>] section holding one command= entry per snapshot command
 * line, in execution order.
 */

/// Returns an error message when @p name cannot be used as a config name.
std::optional<std::string> validate_session_config_name(std::string_view name);

/// Writes (or overwrites) @p name with @p command_lines and saves the store.
/// Returns std::nullopt on success or an error message.
std::optional<std::string> save_session_config(SettingsStore& settings_store, std::string_view name, const std::vector<std::string>& command_lines);

/// The command lines of config @p name, or std::nullopt when it does not exist.
std::optional<std::vector<std::string>> load_session_config(const SettingsStore& settings_store, std::string_view name);

/// Names of all stored configs, in file order.
std::vector<std::string> list_session_configs(const SettingsStore& settings_store);

/// Removes config @p name and saves the store. Returns an error message when
/// the config does not exist or saving fails.
std::optional<std::string> remove_session_config(SettingsStore& settings_store, std::string_view name);

} // namespace slayerlog
