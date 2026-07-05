#include "implementations/set_time_format_action.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "implementations/source_argument.hpp"
#include "log_view_service.hpp"
#include "timestamp/timestamp_format_catalog.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

namespace
{

std::string lowercase(std::string_view text)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered;
}

} // namespace

CommandResult set_time_format_from_arguments(const CommandContext& context, std::string_view arguments)
{
    ResolvedSourceArgument resolved;
    const auto argument_error = resolve_source_argument(context.tracked_sources, arguments, set_time_format_usage, resolved);
    if (argument_error.has_value())
    {
        return *argument_error;
    }

    if (resolved.remainder.empty())
    {
        return {false, "Usage: " + std::string(set_time_format_usage)};
    }

    if (lowercase(resolved.remainder) == "auto")
    {
        const auto error = context.tracked_sources.reset_source_timestamp_format(resolved.source_index);
        if (error.has_value())
        {
            return {false, *error};
        }

        context.log_view.reload(context.tracked_sources, context.processed_sources);
        return {true, "Restored automatic timestamp detection for " + resolved.label};
    }

    const TimestampFormatCatalog validation_catalog({resolved.remainder});
    if (!validation_catalog.rejected_formats().empty())
    {
        return {false, "Invalid timestamp format \"" + resolved.remainder + "\": " + validation_catalog.rejected_formats().front().error};
    }

    const auto error = context.tracked_sources.set_source_timestamp_format(resolved.source_index, resolved.remainder);
    if (error.has_value())
    {
        return {false, *error};
    }

    context.log_view.reload(context.tracked_sources, context.processed_sources);
    return {true, "Set timestamp format for " + resolved.label + ": " + resolved.remainder};
}

} // namespace slayerlog
