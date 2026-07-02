#include "command_registrar.hpp"

#include <memory>

#include "command_manager.hpp"
#include "implementations/log_view1/align_time_command.hpp"
#include "implementations/log_view1/clear_time_offset_command.hpp"
#include "implementations/log_view1/close_open_file_command.hpp"
#include "implementations/log_view1/delete_filters_command.hpp"
#include "implementations/log_view1/find_command.hpp"
#include "implementations/log_view1/adjust_time_offset_command.hpp"
#include "implementations/log_view1/set_time_format_command.hpp"
#include "implementations/log_view2/align_time_view_command.hpp"
#include "implementations/log_view2/log_view2_find_command.hpp"
#include "implementations/log_view2/open_command.hpp"
#include "register_core_view_commands.hpp"

namespace slayerlog
{
namespace
{

// The commands common to any log view: the pure core set (registered by the
// core seam) with the FTXUI interactive pickers interleaved at their
// historical palette positions. Find and time alignment are view-specific and
// registered by the per-view entry points.
void register_shared_view_commands(CommandManager& command_manager, const CommandContext& context)
{
    register_core_view_commands(command_manager, context,
                                [&context](ViewPickerCommandSlot slot) -> std::unique_ptr<CoreCommand>
                                {
                                    switch (slot)
                                    {
                                    case ViewPickerCommandSlot::DeleteFilters:
                                        return std::make_unique<DeleteFiltersCommand>(context);
                                    case ViewPickerCommandSlot::CloseOpenFile:
                                        return std::make_unique<CloseOpenFileCommand>(context);
                                    case ViewPickerCommandSlot::SetTimeFormat:
                                        return std::make_unique<SetTimeFormatCommand>(context);
                                    case ViewPickerCommandSlot::AdjustTimeOffset:
                                        return std::make_unique<AdjustTimeOffsetCommand>(context);
                                    case ViewPickerCommandSlot::ClearTimeOffset:
                                        return std::make_unique<ClearTimeOffsetCommand>(context);
                                    }

                                    return nullptr;
                                });
}

} // namespace

void register_commands(CommandManager& command_manager, CommandContext context)
{
    register_shared_view_commands(command_manager, context);
    command_manager.register_command(std::make_unique<AlignTimeCommand>(context));
    command_manager.register_command(std::make_unique<FindCommand>(context));
}

void register_log_view2_commands(CommandManager& command_manager, CommandContext context, LogView2FindManager& find_manager)
{
    register_shared_view_commands(command_manager, context);
    command_manager.register_command(std::make_unique<OpenCommand>(context));
    command_manager.register_command(std::make_unique<AlignTimeViewCommand>(context));
    command_manager.register_command(std::make_unique<LogView2FindCommand>(find_manager));
}

} // namespace slayerlog
