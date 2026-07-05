#include "implementations/reset_session_command.hpp"

#include <cctype>
#include <string>

#include "commands/session_reset.hpp"

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

ResetSessionCommand::ResetSessionCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& ResetSessionCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"reset-session",
                                               "Close all sources and clear all view state",
                                               "reset-session",
                                               {"Closes every source and resets filters, hidden columns, the hide-before cutoff, and display toggles to their defaults.", "This is what load-config runs before replaying a saved config."}};
    return descriptor;
}

CommandResult ResetSessionCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: reset-session"};
    }

    const std::size_t closed_count = reset_session_state(_context);
    return {true, "Session reset: closed " + std::to_string(closed_count) + " source(s) and cleared all view state"};
}

} // namespace slayerlog
