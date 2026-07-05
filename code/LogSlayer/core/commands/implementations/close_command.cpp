#include "implementations/close_command.hpp"

#include <string>

#include "implementations/source_argument.hpp"
#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

CloseCommand::CloseCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& CloseCommand::descriptor() const
{
    static const CommandDescriptor descriptor {
        "close",
        "Close one source by path or mnemonic",
        "close <source>",
        {"Closes the given source without opening a picker (see close-open-file for the interactive form).", "The source is a path, ssh:// URI, or mnemonic of an open source; quote paths containing spaces.", "Example: close app.log"}};
    return descriptor;
}

CommandResult CloseCommand::execute(std::string_view arguments)
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

    std::string closed_label;
    const auto error = _context.tracked_sources.close_source(resolved.source_index, &closed_label);
    if (error.has_value())
    {
        return {false, *error};
    }

    _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
    return {true, "Closed source: " + closed_label};
}

} // namespace slayerlog
