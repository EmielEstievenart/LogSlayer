#include "implementations/list_configs_command.hpp"

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

ListConfigsCommand::ListConfigsCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& ListConfigsCommand::descriptor() const
{
    static const CommandDescriptor descriptor {
        "list-configs", "List saved session configs", "list-configs", {"Shows the configs stored in the settings INI (the __last slot is the auto-saved previous session).", "Load one with load-config <name>."}};
    return descriptor;
}

CommandResult ListConfigsCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: list-configs"};
    }

    if (_context.settings_store == nullptr)
    {
        return {false, "Settings are unavailable this run, configs cannot be listed"};
    }

    const auto names = list_session_configs(*_context.settings_store);
    if (names.empty())
    {
        return {true, "No saved configs (create one with save-config <name>)", false};
    }

    std::string joined;
    for (const auto& name : names)
    {
        if (!joined.empty())
        {
            joined += ", ";
        }
        joined += name;
    }

    return {true, "Saved configs: " + joined, false};
}

} // namespace slayerlog
