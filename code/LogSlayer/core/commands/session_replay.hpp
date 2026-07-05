#pragma once

#include <string>
#include <vector>

#include "command_context.hpp"

namespace slayerlog
{

class CommandManager;

/// Outcome of replaying a command list: how many lines ran and the failures.
struct SessionReplayReport
{
    std::size_t executed_count = 0;
    std::vector<std::string> errors;
};

/**
 * @brief Executes snapshot command lines in order against the live model.
 *
 * Open commands are executed synchronously (bypassing the background-open
 * path) so later lines can reference the sources they create; every other
 * line goes through @p command_manager. A command that would start an
 * interactive palette session is cancelled and reported as an error. Failures
 * never stop the replay — the point of a config is to restore as much of the
 * session as still applies.
 *
 * The caller must hold the model mutex, matching normal command execution.
 */
SessionReplayReport replay_session_commands(const std::vector<std::string>& command_lines, CommandManager& command_manager, const CommandContext& context);

} // namespace slayerlog
