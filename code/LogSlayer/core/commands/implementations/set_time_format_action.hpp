#pragma once

#include <string_view>

#include "command_context.hpp"
#include "commands/core_command.hpp"

namespace slayerlog
{

/// Usage line for the textual set-time-format form, shared between the core
/// action and the interactive command's descriptor.
inline constexpr std::string_view set_time_format_usage = "set-time-format [<source> <format|auto>]";

/**
 * @brief Applies "set-time-format <source> <format>" textual arguments.
 *
 * The source is a path/URI/mnemonic (quoted when it contains spaces); the rest
 * of the line is the timestamp format to pin, or the word "auto" to restore
 * automatic detection. The format is validated before it is applied. This is
 * the non-interactive counterpart of the set-time-format picker; it lives in
 * the core so scripts and configs can drive it without a UI.
 */
CommandResult set_time_format_from_arguments(const CommandContext& context, std::string_view arguments);

} // namespace slayerlog
