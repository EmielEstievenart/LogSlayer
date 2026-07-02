#pragma once

#include "command_context.hpp"

namespace slayerlog
{

class CommandManager;
class CommandPaletteSession;

/// Registers the shared view command set common to every UI: the pure core
/// commands plus the interactive picker commands (delete-filters,
/// close-open-file, set-time-format, adjust-time-offset, clear-time-offset),
/// which drive the CommandPaletteSession's picker modes and are therefore
/// UI-agnostic too. The registration order is the palette order the TUI has
/// always shown. Find and time alignment are view-specific and registered by
/// the per-view entry points.
void register_core_view_commands(CommandManager& command_manager, const CommandContext& context, CommandPaletteSession& palette_session);

} // namespace slayerlog
