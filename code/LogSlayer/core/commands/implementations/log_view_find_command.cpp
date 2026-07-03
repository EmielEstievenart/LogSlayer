#include "implementations/log_view_find_command.hpp"

#include <stdexcept>
#include <string>

#include "log_view_find_manager.hpp"
#include "search_pattern.hpp"

namespace slayerlog
{

LogViewFindCommand::LogViewFindCommand(LogViewFindManager& find_manager) : _find_manager(find_manager)
{
}

const CommandDescriptor& LogViewFindCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"find",
                                               "Find lines matching text or regex",
                                               "find <text|re:regex>",
                                               {"Highlights matching LogView lines and focuses the first match when possible.", "Use plain text for substring matching or prefix with re: for a regular expression.",
                                                "After find is active, use Right/Left to move between matches and Esc to clear it.", "Example: find timeout", "Example: find re:request_id=[0-9]+"}};
    return descriptor;
}

CommandResult LogViewFindCommand::execute(std::string_view arguments)
{
    const std::string query = trim_search_text(arguments);
    if (query.empty())
    {
        return {false, "Usage: find <text|re:regex>"};
    }

    bool focused_match = false;
    try
    {
        focused_match = _find_manager.set_query(query);
    }
    catch (const std::invalid_argument& error)
    {
        return {false, "Invalid find pattern: " + std::string(error.what())};
    }

    const auto match_count = _find_manager.match_count();
    if (focused_match)
    {
        return {true, "Find active: " + _find_manager.query() + " (" + std::to_string(match_count) + " matches)"};
    }

    return {true, "Find active: " + _find_manager.query() + " (0 matches)"};
}

} // namespace slayerlog
