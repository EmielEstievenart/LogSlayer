#include "implementations/reset_filters_command.hpp"

#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

ResetFiltersCommand::ResetFiltersCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& ResetFiltersCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"reset-filters", "Clear all active filters", "reset-filters"};
    return descriptor;
}

CommandResult ResetFiltersCommand::execute(std::string_view arguments)
{
    if (!arguments.empty())
    {
        return {false, "Usage: reset-filters"};
    }

    _context.processed_sources.reset_filters();
    _context.log_controller.rebuild_view(_context.processed_sources);
    return {true, "Cleared all filters"};
}

} // namespace slayerlog
