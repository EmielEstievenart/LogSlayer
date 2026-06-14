#include "command_registrar.hpp"

#include <memory>

#include "command_manager.hpp"
#include "implementations/log_view1/align_time_command.hpp"
#include "implementations/log_view1/clear_column_filters_command.hpp"
#include "implementations/log_view1/clear_filters_command.hpp"
#include "implementations/log_view1/clear_time_offset_command.hpp"
#include "implementations/log_view1/close_open_file_command.hpp"
#include "implementations/log_view1/copy_settings_path_command.hpp"
#include "implementations/log_view1/delete_filters_command.hpp"
#include "implementations/log_view1/export_visible_text_command.hpp"
#include "implementations/log_view1/filter_in_command.hpp"
#include "implementations/log_view1/filter_out_command.hpp"
#include "implementations/log_view1/find_command.hpp"
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
#include "implementations/log_view1/adjust_time_offset_command.hpp"
#include "implementations/log_view1/set_time_format_command.hpp"
#include "implementations/log_view1/show_identical_lines_command.hpp"
#include "implementations/log_view1/show_original_time_command.hpp"
#include "implementations/log_view2/log_view2_find_command.hpp"
#include "implementations/log_view2/open_command.hpp"

namespace slayerlog
{

void register_commands(CommandManager& command_manager, CommandContext context)
{
    command_manager.register_command(std::make_unique<FilterInCommand>(context));
    command_manager.register_command(std::make_unique<FilterOutCommand>(context));
    command_manager.register_command(std::make_unique<ResetFiltersCommand>(context));
    command_manager.register_command(std::make_unique<DeleteFiltersCommand>(context));
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
    command_manager.register_command(std::make_unique<CloseOpenFileCommand>(context));

    command_manager.register_command(std::make_unique<SetTimeFormatCommand>(context));
    command_manager.register_command(std::make_unique<AdjustTimeOffsetCommand>(context));
    command_manager.register_command(std::make_unique<AlignTimeCommand>(context));
    command_manager.register_command(std::make_unique<ClearTimeOffsetCommand>(context));

    command_manager.register_command(std::make_unique<GoToLineCommand>(context));
    command_manager.register_command(std::make_unique<HideBeforeLineCommand>(context));
    command_manager.register_command(std::make_unique<HideShownLinesCommand>(context));

    command_manager.register_command(std::make_unique<ExportVisibleTextCommand>(context));
    command_manager.register_command(std::make_unique<CopySettingsPathCommand>(context));
    command_manager.register_command(std::make_unique<FindCommand>(context));
}

void register_log_view2_commands(CommandManager& command_manager, CommandContext context, LogView2FindManager& find_manager)
{
    command_manager.register_command(std::make_unique<OpenCommand>(context));
    command_manager.register_command(std::make_unique<LogView2FindCommand>(find_manager));
}

} // namespace slayerlog
