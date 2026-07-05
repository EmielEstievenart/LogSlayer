#include "implementations/set_offset_command.hpp"

#include <string>

#include "implementations/source_argument.hpp"
#include "log_view_service.hpp"
#include "timestamp/log_timestamp.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

SetOffsetCommand::SetOffsetCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& SetOffsetCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"set-offset",
                                               "Set the timestamp offset of one source",
                                               "set-offset <source> <offset>",
                                               {"Replaces the source's timestamp offset (unlike adjust-time-offset, which adds on top interactively).",
                                                "The source is a path, ssh:// URI, or mnemonic of an open source; quote paths containing spaces.", "Offset format is DD hh:mm:ss[.fraction] with an optional leading + or - sign.",
                                                "Example: set-offset app.log -00 00:00:10.005", "Example: set-offset \"C:\\my logs\\app.log\" +00 01:00:00"}};
    return descriptor;
}

CommandResult SetOffsetCommand::execute(std::string_view arguments)
{
    ResolvedSourceArgument resolved;
    const auto argument_error = resolve_source_argument(_context.tracked_sources, arguments, descriptor().usage, resolved);
    if (argument_error.has_value())
    {
        return *argument_error;
    }

    if (resolved.remainder.empty())
    {
        return {false, "Usage: " + descriptor().usage};
    }

    const auto offset = parse_log_timestamp_offset(resolved.remainder);
    if (!offset.has_value())
    {
        return {false, "Invalid offset: expected DD hh:mm:ss[.fraction] with an optional leading + or - sign"};
    }

    const auto error = _context.tracked_sources.set_source_timestamp_offset(resolved.source_index, *offset);
    if (error.has_value())
    {
        return {false, *error};
    }

    _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
    return {true, "Set time offset for " + resolved.label + " to " + format_log_timestamp_offset(*offset)};
}

} // namespace slayerlog
