#include "command_registrar.hpp"

#include <memory>

#include "command_manager.hpp"
#include "implementations/align_time_command.hpp"
#include "implementations/clear_column_filters_command.hpp"
#include "implementations/clear_filters_command.hpp"
#include "implementations/clear_time_offset_command.hpp"
#include "implementations/close_open_file_command.hpp"
#include "implementations/delete_filters_command.hpp"
#include "implementations/export_visible_text_command.hpp"
#include "implementations/filter_in_command.hpp"
#include "implementations/filter_out_command.hpp"
#include "implementations/find_command.hpp"
#include "implementations/go_to_line_command.hpp"
#include "implementations/hide_before_line_command.hpp"
#include "implementations/hide_columns_command.hpp"
#include "implementations/hide_identical_lines_command.hpp"
#include "implementations/hide_original_time_command.hpp"
#include "implementations/hide_shown_lines_command.hpp"
#include "implementations/open_file_command.hpp"
#include "implementations/open_folder_command.hpp"
#include "implementations/reset_column_filter_command.hpp"
#include "implementations/reset_filters_command.hpp"
#include "implementations/set_time_format_command.hpp"
#include "implementations/set_time_offset_command.hpp"
#include "implementations/show_identical_lines_command.hpp"
#include "implementations/show_original_time_command.hpp"

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
    command_manager.register_command(std::make_unique<SetTimeOffsetCommand>(context));
    command_manager.register_command(std::make_unique<AlignTimeCommand>(context));
    command_manager.register_command(std::make_unique<ClearTimeOffsetCommand>(context));

    command_manager.register_command(std::make_unique<GoToLineCommand>(context));
    command_manager.register_command(std::make_unique<HideBeforeLineCommand>(context));
    command_manager.register_command(std::make_unique<HideShownLinesCommand>(context));

    command_manager.register_command(std::make_unique<ExportVisibleTextCommand>(context));
    command_manager.register_command(std::make_unique<FindCommand>(context));
}

} // namespace slayerlog
