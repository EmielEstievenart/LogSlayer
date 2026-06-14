#include "implementations/log_view1/hide_original_time_command.hpp"

#include <cctype>
#include <string>

#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{
namespace
{
std::string trim_text(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) { ++start; }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) { --end; }
    return std::string(text.substr(start, end - start));
}
}

HideOriginalTimeCommand::HideOriginalTimeCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& HideOriginalTimeCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"hide-original-time", "Hide detected timestamp in original text", "hide-original-time", {"When a timestamp is detected in a log line, remove it from the rendered message text."}};
    return descriptor;
}

CommandResult HideOriginalTimeCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty()) { return {false, "Usage: hide-original-time"}; }
    _context.processed_sources.set_show_original_time(false);
    _context.log_view.rebuild_view(_context.processed_sources);
    return {true, "Hiding original detected timestamps in messages"};
}

} // namespace slayerlog
