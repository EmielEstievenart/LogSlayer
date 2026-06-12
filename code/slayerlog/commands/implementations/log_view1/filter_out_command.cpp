#include "implementations/log_view1/filter_out_command.hpp"

#include <stdexcept>
#include <string>

#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

FilterOutCommand::FilterOutCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& FilterOutCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"filter-out", "Hide lines matching text or regex", "filter-out <text|re:regex>", {"Hide matching lines while keeping everything else visible.", "Use plain text for substring matching or prefix with re: for a regular expression.", "Example: filter-out heartbeat", "Example: filter-out re:^DEBUG"}};
    return descriptor;
}

CommandResult FilterOutCommand::execute(std::string_view arguments)
{
    if (arguments.empty())
    {
        return {false, "Usage: filter-out <text|re:regex>"};
    }

    try
    {
        _context.processed_sources.add_exclude_filter(std::string(arguments));
    }
    catch (const std::invalid_argument& error)
    {
        return {false, "Invalid filter-out pattern: " + std::string(error.what())};
    }

    _context.log_controller.rebuild_view(_context.processed_sources);
    return {true, "Added exclude filter: " + std::string(arguments)};
}

} // namespace slayerlog
