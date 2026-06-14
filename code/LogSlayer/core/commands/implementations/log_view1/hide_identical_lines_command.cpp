#include "implementations/log_view1/hide_identical_lines_command.hpp"

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

HideIdenticalLinesCommand::HideIdenticalLinesCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& HideIdenticalLinesCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"hide-identical-lines", "Hide repeated identical messages", "hide-identical-lines", {"Keep the first occurrence and show a summary row for identical messages above."}};
    return descriptor;
}

CommandResult HideIdenticalLinesCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty()) { return {false, "Usage: hide-identical-lines"}; }
    _context.processed_sources.set_hide_identical_lines(true);
    _context.log_view.rebuild_view(_context.processed_sources);
    return {true, "Hiding identical messages"};
}

} // namespace slayerlog
