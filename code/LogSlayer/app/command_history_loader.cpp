#include "command_history_loader.hpp"

#include "debug_log.hpp"
#include "settings_store.hpp"

namespace slayerlog
{

std::optional<CommandHistory> load_command_history(SettingsStore& settings_store, bool settings_loaded, std::string& settings_error_message)
{
    if (!settings_loaded)
    {
        SLAYERLOG_LOG_WARNING("Command history is disabled because settings failed to load from " << settings_store.file_path());
        return std::nullopt;
    }

    std::optional<CommandHistory> command_history;
    command_history.emplace(settings_store);
    if (command_history->load(settings_error_message))
    {
        return command_history;
    }

    SLAYERLOG_LOG_ERROR("Failed to load command history from " << settings_store.file_path() << ": " << settings_error_message << "; command history saves are disabled for this run");
    settings_error_message.clear();
    return std::nullopt;
}

} // namespace slayerlog
