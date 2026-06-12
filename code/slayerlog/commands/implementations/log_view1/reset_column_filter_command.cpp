#include "implementations/reset_column_filter_command.hpp"

#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

ResetColumnFilterCommand::ResetColumnFilterCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& ResetColumnFilterCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"reset-column-filter", "Clear the hidden column range", "reset-column-filter"};
    return descriptor;
}

CommandResult ResetColumnFilterCommand::execute(std::string_view arguments)
{
    if (!arguments.empty())
    {
        return {false, "Usage: reset-column-filter"};
    }

    _context.processed_sources.reset_hidden_columns();
    _context.log_controller.rebuild_view(_context.processed_sources);
    return {true, "Cleared hidden column filter"};
}

} // namespace slayerlog
