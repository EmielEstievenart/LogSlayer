#include "implementations/log_view1/find_command.hpp"

#include <cctype>
#include <stdexcept>
#include <string>

#include "log_controller.hpp"
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

FindCommand::FindCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& FindCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"find", "Find lines matching text or regex", "find <text|re:regex>", {"Highlights matching lines and focuses the first visible match when possible.", "Use plain text for substring matching or prefix with re: for a regular expression.", "After find is active, use Right/Left to move between matches and Esc to clear it.", "Example: find timeout", "Example: find re:request_id=[0-9]+"}};
    return descriptor;
}

CommandResult FindCommand::execute(std::string_view arguments)
{
    const std::string query = trim_text(arguments);
    if (query.empty()) { return {false, "Usage: find <text|re:regex>"}; }

    bool focused_visible_match = false;
    try { focused_visible_match = _context.log_controller.set_find_query(_context.processed_sources, query); }
    catch (const std::invalid_argument& error) { return {false, "Invalid find pattern: " + std::string(error.what())}; }

    const int visible_matches = _context.log_controller.visible_find_match_count(_context.processed_sources);
    const int total_matches = _context.log_controller.total_find_match_count();
    if (focused_visible_match)
    {
        return {true, "Find active: " + _context.log_controller.find_query() + " (" + std::to_string(visible_matches) + " visible / " + std::to_string(total_matches) + " total)"};
    }

    return {true, "Find active: " + _context.log_controller.find_query() + " (0 visible / " + std::to_string(total_matches) + " total)"};
}

} // namespace slayerlog
