#include "command_registrar.hpp"

#include <memory>

#include "command_manager.hpp"
#include "implementations/log_view1/align_time_command.hpp"
#include "implementations/log_view1/find_command.hpp"
#include "implementations/log_view2/align_time_view_command.hpp"
#include "implementations/log_view2/log_view2_find_command.hpp"
#include "implementations/log_view2/open_command.hpp"
#include "register_core_view_commands.hpp"

namespace slayerlog
{

// The commands common to any log view (pure core actions and the interactive
// pickers, which now drive the core CommandPaletteSession) are registered by
// the core seam. Find and time alignment are view-specific and registered by
// the per-view entry points below.

void register_commands(CommandManager& command_manager, CommandContext context, CommandPaletteSession& palette_session)
{
    register_core_view_commands(command_manager, context, palette_session);
    command_manager.register_command(std::make_unique<AlignTimeCommand>(context));
    command_manager.register_command(std::make_unique<FindCommand>(context));
}

void register_log_view2_commands(CommandManager& command_manager, CommandContext context, LogView2FindManager& find_manager, CommandPaletteSession& palette_session)
{
    register_core_view_commands(command_manager, context, palette_session);
    command_manager.register_command(std::make_unique<OpenCommand>(context));
    command_manager.register_command(std::make_unique<AlignTimeViewCommand>(context));
    command_manager.register_command(std::make_unique<LogView2FindCommand>(find_manager));
}

} // namespace slayerlog
