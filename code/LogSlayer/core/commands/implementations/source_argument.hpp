#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "commands/core_command.hpp"

namespace slayerlog
{

class AllTrackedSources;

/// A source reference resolved from command arguments.
struct ResolvedSourceArgument
{
    std::size_t source_index = 0;
    std::string label;
    std::string remainder;
};

/**
 * @brief Extracts and resolves the leading source reference of @p arguments.
 *
 * The reference is the first argument (quote it when the path contains
 * spaces); the rest of the line is returned in ResolvedSourceArgument::remainder.
 * Returns std::nullopt on success, or the CommandResult error the command
 * should return (usage help or unknown-source message).
 */
std::optional<CommandResult> resolve_source_argument(const AllTrackedSources& tracked_sources, std::string_view arguments, std::string_view usage, ResolvedSourceArgument& resolved);

} // namespace slayerlog
