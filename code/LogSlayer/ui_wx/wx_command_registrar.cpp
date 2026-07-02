#include "wx_command_registrar.hpp"

#include <memory>

#include "command_manager.hpp"
#include "implementations/log_view2/log_view2_find_command.hpp"
#include "register_core_view_commands.hpp"

namespace slayerlog
{

void register_wx_view_commands(CommandManager& command_manager, CommandContext context, LogView2FindManager& find_manager)
{
    register_core_view_commands(command_manager, context);
    command_manager.register_command(std::make_unique<LogView2FindCommand>(find_manager));
}

} // namespace slayerlog
