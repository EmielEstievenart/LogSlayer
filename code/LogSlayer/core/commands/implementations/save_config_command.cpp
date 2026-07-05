#include "implementations/save_config_command.hpp"

#include <cctype>
#include <string>

#include "commands/session_config_store.hpp"
#include "session_snapshot.hpp"

namespace slayerlog
{
namespace
{
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

SaveConfigCommand::SaveConfigCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& SaveConfigCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"save-config",
                                               "Save the current session as a named config",
                                               "save-config <name>",
                                               {"Snapshots the open sources, per-source time formats and offsets, filters, hidden columns,", "display toggles, hide-before cutoff, and view position into the settings INI.",
                                                "Restore it with load-config <name> or start with it via: LogSlayer --config <name>.", "Saving to an existing name overwrites it.", "Example: save-config crashhunt"}};
    return descriptor;
}

CommandResult SaveConfigCommand::execute(std::string_view arguments)
{
    const std::string name = trim_text(arguments);
    const auto name_error  = validate_session_config_name(name);
    if (name_error.has_value())
    {
        return {false, *name_error + ". Usage: " + descriptor().usage};
    }

    if (_context.settings_store == nullptr)
    {
        return {false, "Settings are unavailable this run, configs cannot be saved"};
    }

    const auto commands = serialize_session_commands(_context.tracked_sources, _context.processed_sources, &_context.log_view);
    const auto error    = save_session_config(*_context.settings_store, name, commands);
    if (error.has_value())
    {
        return {false, *error};
    }

    return {true, "Saved config '" + name + "' (" + std::to_string(commands.size()) + " commands)"};
}

} // namespace slayerlog
