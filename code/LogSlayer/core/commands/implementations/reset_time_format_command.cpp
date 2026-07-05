#include "implementations/reset_time_format_command.hpp"

#include <string>

#include "implementations/source_argument.hpp"
#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

ResetTimeFormatCommand::ResetTimeFormatCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& ResetTimeFormatCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"reset-time-format",
                                               "Restore timestamp auto-detection for one source",
                                               "reset-time-format <source>",
                                               {"Clears a pinned timestamp format so the source detects across all configured formats again.",
                                                "The source is a path, ssh:// URI, or mnemonic of an open source; quote paths containing spaces.", "Equivalent to set-time-format <source> auto.", "Example: reset-time-format app.log"}};
    return descriptor;
}

CommandResult ResetTimeFormatCommand::execute(std::string_view arguments)
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

    const auto error = _context.tracked_sources.reset_source_timestamp_format(resolved.source_index);
    if (error.has_value())
    {
        return {false, *error};
    }

    _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
    return {true, "Restored automatic timestamp detection for " + resolved.label};
}

} // namespace slayerlog
