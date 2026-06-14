#include "implementations/log_view1/filter_in_command.hpp"

#include <stdexcept>
#include <string>

#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

FilterInCommand::FilterInCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& FilterInCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"filter-in", "Show lines matching text or regex", "filter-in <text|re:regex>", {"Keep only matching lines visible.", "Use plain text for substring matching or prefix with re: for a regular expression.", "Example: filter-in auth", "Example: filter-in re:^(ERROR|WARN)"}};
    return descriptor;
}

CommandResult FilterInCommand::execute(std::string_view arguments)
{
    if (arguments.empty())
    {
        return {false, "Usage: filter-in <text|re:regex>"};
    }

    try
    {
        _context.processed_sources.add_include_filter(std::string(arguments));
    }
    catch (const std::invalid_argument& error)
    {
        return {false, "Invalid filter-in pattern: " + std::string(error.what())};
    }

    _context.log_view.rebuild_view(_context.processed_sources);
    return {true, "Added include filter: " + std::string(arguments)};
}

} // namespace slayerlog
