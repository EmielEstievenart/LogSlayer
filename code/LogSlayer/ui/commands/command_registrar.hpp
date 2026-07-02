#pragma once

#include "command_context.hpp"

namespace slayerlog
{

class CommandManager;
class CommandPaletteSession;
class LogView2FindManager;

void register_commands(CommandManager& command_manager, CommandContext context, CommandPaletteSession& palette_session);
void register_log_view2_commands(CommandManager& command_manager, CommandContext context, LogView2FindManager& find_manager, CommandPaletteSession& palette_session);

} // namespace slayerlog
