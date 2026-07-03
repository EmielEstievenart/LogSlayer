#pragma once

#include "command_context.hpp"
#include "command_support.hpp"

namespace slayerlog
{

class CommandManager;
class LogViewFindManager;

void register_log_view_commands(CommandManager& command_manager, CommandContext context, LogViewFindManager& find_manager);

} // namespace slayerlog
