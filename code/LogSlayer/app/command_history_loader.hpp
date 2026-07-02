#pragma once

#include <optional>
#include <string>

#include "commands/command_history.hpp"

namespace slayerlog
{

class SettingsStore;

/// Loads the persisted command history shared by both UIs from the settings
/// store. Returns nullopt (history disabled for the run) when settings failed
/// to load or the history itself fails to parse; the failure is logged, never
/// fatal.
std::optional<CommandHistory> load_command_history(SettingsStore& settings_store, bool settings_loaded, std::string& settings_error_message);

} // namespace slayerlog
