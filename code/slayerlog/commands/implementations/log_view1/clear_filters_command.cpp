#include "implementations/clear_filters_command.hpp"

#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

ClearFiltersCommand::ClearFiltersCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& ClearFiltersCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"clear-filters", "Alias for reset-filters", "clear-filters"};
    return descriptor;
}

CommandResult ClearFiltersCommand::execute(std::string_view arguments)
{
    if (!arguments.empty())
    {
        return {false, "Usage: clear-filters"};
    }

    _context.processed_sources.reset_filters();
    _context.log_controller.rebuild_view(_context.processed_sources);
    return {true, "Cleared all filters"};
}

} // namespace slayerlog
