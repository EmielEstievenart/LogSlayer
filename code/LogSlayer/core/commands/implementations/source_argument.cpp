#include "implementations/source_argument.hpp"

#include "commands/command_arguments.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/source_resolver.hpp"

namespace slayerlog
{

std::optional<CommandResult> resolve_source_argument(const AllTrackedSources& tracked_sources, std::string_view arguments, std::string_view usage, ResolvedSourceArgument& resolved)
{
    const auto split = split_first_command_argument(arguments);
    if (!split.has_value() || split->value.empty())
    {
        return CommandResult {false, "Usage: " + std::string(usage)};
    }

    if (tracked_sources.empty())
    {
        return CommandResult {false, "No open sources"};
    }

    const auto source_index = resolve_source_index(tracked_sources, split->value);
    if (!source_index.has_value())
    {
        return CommandResult {false, "Unknown source: " + split->value + " (use the path or mnemonic of an open source; quote paths containing spaces)"};
    }

    resolved.source_index = *source_index;
    resolved.label        = tracked_sources.source_labels()[*source_index];
    resolved.remainder    = split->remainder;
    return std::nullopt;
}

} // namespace slayerlog
