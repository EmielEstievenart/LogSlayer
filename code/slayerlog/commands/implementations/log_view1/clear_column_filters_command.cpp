#include "implementations/log_view1/clear_column_filters_command.hpp"

#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

ClearColumnFiltersCommand::ClearColumnFiltersCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& ClearColumnFiltersCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"clear-column-filters", "Alias for reset-column-filter", "clear-column-filters"};
    return descriptor;
}

CommandResult ClearColumnFiltersCommand::execute(std::string_view arguments)
{
    if (!arguments.empty())
    {
        return {false, "Usage: clear-column-filters"};
    }

    _context.processed_sources.reset_hidden_columns();
    _context.log_controller.rebuild_view(_context.processed_sources);
    return {true, "Cleared hidden column filter"};
}

} // namespace slayerlog
