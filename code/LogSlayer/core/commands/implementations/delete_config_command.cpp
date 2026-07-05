#include "implementations/delete_config_command.hpp"

#include <cctype>
#include <string>

#include "commands/session_config_store.hpp"

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

DeleteConfigCommand::DeleteConfigCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& DeleteConfigCommand::descriptor() const
{
    static const CommandDescriptor descriptor {
        "delete-config", "Delete a saved session config", "delete-config <name>", {"Removes the config from the settings INI. The current session is not affected.", "Example: delete-config crashhunt"}};
    return descriptor;
}

CommandResult DeleteConfigCommand::execute(std::string_view arguments)
{
    const std::string name = trim_text(arguments);
    if (name.empty())
    {
        return {false, "Usage: " + descriptor().usage};
    }

    if (_context.settings_store == nullptr)
    {
        return {false, "Settings are unavailable this run, configs cannot be deleted"};
    }

    const auto error = remove_session_config(*_context.settings_store, name);
    if (error.has_value())
    {
        return {false, *error};
    }

    return {true, "Deleted config '" + name + "'"};
}

} // namespace slayerlog
