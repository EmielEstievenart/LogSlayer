#include "implementations/hide_columns_command.hpp"

#include <string>

#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

HideColumnsCommand::HideColumnsCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& HideColumnsCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"hide-columns", "Hide displayed columns in a range", "hide-columns <xx-yy>", {"Hide character columns inclusively across every rendered line.", "Example: hide-columns 25-80"}};
    return descriptor;
}

CommandResult HideColumnsCommand::execute(std::string_view arguments)
{
    const auto hidden_columns = parse_hidden_column_range(arguments);
    if (!hidden_columns.has_value())
    {
        return {false, "Usage: hide-columns <xx-yy>"};
    }

    _context.processed_sources.hide_columns(hidden_columns->start, hidden_columns->end);
    _context.log_controller.rebuild_view(_context.processed_sources);
    return {true, "Hidden columns " + std::to_string(hidden_columns->start) + "-" + std::to_string(hidden_columns->end)};
}

std::optional<HiddenColumnRange> HideColumnsCommand::hidden_column_preview(std::string_view arguments) const
{
    return parse_hidden_column_range(arguments);
}

} // namespace slayerlog
