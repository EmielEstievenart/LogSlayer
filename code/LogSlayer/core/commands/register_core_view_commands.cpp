#include "register_core_view_commands.hpp"

#include <memory>

#include "command_manager.hpp"
#include "implementations/log_view1/clear_column_filters_command.hpp"
#include "implementations/log_view1/clear_filters_command.hpp"
#include "implementations/log_view1/copy_settings_path_command.hpp"
#include "implementations/log_view1/export_visible_text_command.hpp"
#include "implementations/log_view1/filter_in_command.hpp"
#include "implementations/log_view1/filter_out_command.hpp"
#include "implementations/log_view1/go_to_line_command.hpp"
#include "implementations/log_view1/hide_before_line_command.hpp"
#include "implementations/log_view1/hide_columns_command.hpp"
#include "implementations/log_view1/hide_identical_lines_command.hpp"
#include "implementations/log_view1/hide_original_time_command.hpp"
#include "implementations/log_view1/hide_shown_lines_command.hpp"
#include "implementations/log_view1/open_file_command.hpp"
#include "implementations/log_view1/open_folder_command.hpp"
#include "implementations/log_view1/reset_column_filter_command.hpp"
#include "implementations/log_view1/reset_filters_command.hpp"
#include "implementations/log_view1/show_identical_lines_command.hpp"
#include "implementations/log_view1/show_original_time_command.hpp"

namespace slayerlog
{

namespace
{

void register_picker(CommandManager& command_manager, const ViewPickerCommandFactory& picker_factory, ViewPickerCommandSlot slot)
{
    if (!picker_factory)
    {
        return;
    }

    if (auto command = picker_factory(slot))
    {
        command_manager.register_command(std::move(command));
    }
}

} // namespace

void register_core_view_commands(CommandManager& command_manager, const CommandContext& context, const ViewPickerCommandFactory& picker_factory)
{
    command_manager.register_command(std::make_unique<FilterInCommand>(context));
    command_manager.register_command(std::make_unique<FilterOutCommand>(context));
    command_manager.register_command(std::make_unique<ResetFiltersCommand>(context));
    register_picker(command_manager, picker_factory, ViewPickerCommandSlot::DeleteFilters);
    command_manager.register_command(std::make_unique<ClearFiltersCommand>(context));

    command_manager.register_command(std::make_unique<HideColumnsCommand>(context));
    command_manager.register_command(std::make_unique<ResetColumnFilterCommand>(context));
    command_manager.register_command(std::make_unique<ClearColumnFiltersCommand>(context));
    command_manager.register_command(std::make_unique<ShowOriginalTimeCommand>(context));
    command_manager.register_command(std::make_unique<HideOriginalTimeCommand>(context));
    command_manager.register_command(std::make_unique<ShowIdenticalLinesCommand>(context));
    command_manager.register_command(std::make_unique<HideIdenticalLinesCommand>(context));

    command_manager.register_command(std::make_unique<OpenFileCommand>(context));
    command_manager.register_command(std::make_unique<OpenFolderCommand>(context));
    register_picker(command_manager, picker_factory, ViewPickerCommandSlot::CloseOpenFile);

    register_picker(command_manager, picker_factory, ViewPickerCommandSlot::SetTimeFormat);
    register_picker(command_manager, picker_factory, ViewPickerCommandSlot::AdjustTimeOffset);
    register_picker(command_manager, picker_factory, ViewPickerCommandSlot::ClearTimeOffset);

    command_manager.register_command(std::make_unique<GoToLineCommand>(context));
    command_manager.register_command(std::make_unique<HideBeforeLineCommand>(context));
    command_manager.register_command(std::make_unique<HideShownLinesCommand>(context));

    command_manager.register_command(std::make_unique<ExportVisibleTextCommand>(context));
    command_manager.register_command(std::make_unique<CopySettingsPathCommand>(context));
}

} // namespace slayerlog
