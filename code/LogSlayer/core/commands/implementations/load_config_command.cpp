#include "implementations/load_config_command.hpp"

#include <cctype>
#include <string>

#include "commands/session_config_store.hpp"
#include "commands/session_replay.hpp"
#include "commands/session_reset.hpp"

namespace slayerlog
{
namespace
{

constexpr std::size_t max_reported_errors = 3;

std::string trim_text(std::string_view text)
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

} // namespace

LoadConfigCommand::LoadConfigCommand(CommandContext context, CommandManager& command_manager) : _context(context), _command_manager(command_manager)
{
}

const CommandDescriptor& LoadConfigCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"load-config",
                                               "Replace the session with a saved config",
                                               "load-config <name>",
                                               {"Resets the session (like reset-session), then replays the config's commands.", "Failing commands are reported and skipped; the rest of the config still loads.",
                                                "Save the current session first with save-config if you want to return to it.", "Example: load-config crashhunt"}};
    return descriptor;
}

CommandResult LoadConfigCommand::execute(std::string_view arguments)
{
    const std::string name = trim_text(arguments);
    if (name.empty())
    {
        return {false, "Usage: " + descriptor().usage};
    }

    if (_replaying)
    {
        return {false, "load-config cannot run from inside a config"};
    }

    if (_context.settings_store == nullptr)
    {
        return {false, "Settings are unavailable this run, configs cannot be loaded"};
    }

    const auto command_lines = load_session_config(*_context.settings_store, name);
    if (!command_lines.has_value())
    {
        return {false, "No config named '" + name + "' (see list-configs)"};
    }

    reset_session_state(_context);

    _replaying               = true;
    const auto replay_report = replay_session_commands(*command_lines, _command_manager, _context);
    _replaying               = false;

    if (!replay_report.errors.empty())
    {
        std::string details;
        for (std::size_t error_index = 0; error_index < replay_report.errors.size() && error_index < max_reported_errors; ++error_index)
        {
            if (!details.empty())
            {
                details += "\n";
            }
            details += replay_report.errors[error_index];
        }
        if (replay_report.errors.size() > max_reported_errors)
        {
            details += "\n+" + std::to_string(replay_report.errors.size() - max_reported_errors) + " more";
        }

        _context.notifier.warning("Config '" + name + "' loaded with " + std::to_string(replay_report.errors.size()) + " error(s)", details);
    }

    return {true, "Loaded config '" + name + "' (" + std::to_string(replay_report.executed_count) + " commands, " + std::to_string(replay_report.errors.size()) + " errors)"};
}

} // namespace slayerlog
