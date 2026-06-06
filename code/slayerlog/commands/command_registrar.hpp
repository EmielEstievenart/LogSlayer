#pragma once

#include "command_context.hpp"
#include "command_support.hpp"

namespace slayerlog
{

class CommandManager;
class LogView2FindManager;

void register_commands(CommandManager& command_manager, CommandContext context);
void register_log_view2_commands(CommandManager& command_manager, CommandContext context, LogView2FindManager& find_manager);

} // namespace slayerlog
