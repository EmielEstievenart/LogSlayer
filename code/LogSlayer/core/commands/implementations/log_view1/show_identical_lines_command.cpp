#include "implementations/log_view1/show_identical_lines_command.hpp"

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

ShowIdenticalLinesCommand::ShowIdenticalLinesCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& ShowIdenticalLinesCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"show-identical-lines", "Show identical messages line-by-line", "show-identical-lines", {"Render every matching message row instead of collapsing identical rows."}};
    return descriptor;
}

CommandResult ShowIdenticalLinesCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty()) { return {false, "Usage: show-identical-lines"}; }
    _context.processed_sources.set_hide_identical_lines(false);
    _context.log_view.rebuild_view(_context.processed_sources);
    return {true, "Showing identical messages"};
}

} // namespace slayerlog
