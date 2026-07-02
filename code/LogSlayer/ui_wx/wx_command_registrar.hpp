#pragma once

#include "command_context.hpp"

namespace slayerlog
{

class CommandManager;
class CommandPaletteSession;
class LogView2FindManager;

/// Registers the wx GUI's command set: the shared core view commands
/// (including the interactive picker commands, which drive the shared
/// CommandPaletteSession's picker modes), the LogView2 open command, and the
/// find command over the wx view's find manager. align-time stays TUI-only
/// until the wx align view lands (docs/wx-ui-plan.md, M6).
void register_wx_view_commands(CommandManager& command_manager, CommandContext context, LogView2FindManager& find_manager, CommandPaletteSession& palette_session);

} // namespace slayerlog
