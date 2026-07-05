#include "implementations/close_all_command.hpp"

#include <cctype>
#include <string>

#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

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

CloseAllCommand::CloseAllCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& CloseAllCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"close-all", "Close all open sources", "close-all", {"Closes every open source; filters and display settings are kept (use reset-session to clear everything)."}};
    return descriptor;
}

CommandResult CloseAllCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: close-all"};
    }

    const std::size_t closed_count = _context.tracked_sources.source_count();
    if (closed_count == 0)
    {
        return {false, "No open sources to close"};
    }

    while (_context.tracked_sources.source_count() > 0)
    {
        const auto error = _context.tracked_sources.close_source(_context.tracked_sources.source_count() - 1);
        if (error.has_value())
        {
            return {false, *error};
        }
    }

    _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
    return {true, "Closed " + std::to_string(closed_count) + " source(s)"};
}

} // namespace slayerlog
