#include "implementations/clear_offset_command.hpp"

#include <string>

#include "implementations/source_argument.hpp"
#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

ClearOffsetCommand::ClearOffsetCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& ClearOffsetCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"clear-offset",
                                               "Clear the timestamp offset of one source",
                                               "clear-offset <source>",
                                               {"Removes the timestamp offset applied to one source.", "The source is a path, ssh:// URI, or mnemonic of an open source; quote paths containing spaces.", "Example: clear-offset app.log"}};
    return descriptor;
}

CommandResult ClearOffsetCommand::execute(std::string_view arguments)
{
    ResolvedSourceArgument resolved;
    const auto argument_error = resolve_source_argument(_context.tracked_sources, arguments, descriptor().usage, resolved);
    if (argument_error.has_value())
    {
        return *argument_error;
    }

    if (!resolved.remainder.empty())
    {
        return {false, "Usage: " + descriptor().usage};
    }

    const auto error = _context.tracked_sources.clear_source_timestamp_offset(resolved.source_index);
    if (error.has_value())
    {
        return {false, *error};
    }

    _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
    return {true, "Cleared time offset for " + resolved.label};
}

} // namespace slayerlog
