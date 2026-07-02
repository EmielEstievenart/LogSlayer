#pragma once

#include "command_context.hpp"

namespace slayerlog
{

class CommandManager;
class LogView2FindManager;

/// Registers the wx GUI's command set: the pure core view commands plus the
/// LogView2 find command over the wx view's find manager. The interactive
/// picker commands (delete-filters, close-open-file, set-time-format,
/// adjust-time-offset, clear-time-offset), the LogView2 open picker, and
/// align-time stay TUI-only until their interaction state moves into core
/// view-models (docs/wx-ui-plan.md, M4/M6).
void register_wx_view_commands(CommandManager& command_manager, CommandContext context, LogView2FindManager& find_manager);

} // namespace slayerlog
