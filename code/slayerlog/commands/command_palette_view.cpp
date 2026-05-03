#include "command_palette_view.hpp"
#include "view_theme.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace slayerlog
{

namespace
{

ftxui::Element render_command_palette_query(const CommandPaletteModel& command_palette)
{
    const std::size_t cursor_position = std::min(command_palette.cursor_position, command_palette.query.size());
    const std::string prefix          = command_palette.query.substr(0, cursor_position);
    const bool cursor_at_end          = cursor_position >= command_palette.query.size();
    const std::string cursor_text     = cursor_at_end ? " " : command_palette.query.substr(cursor_position, 1);
    const std::string suffix          = cursor_at_end ? std::string() : command_palette.query.substr(cursor_position + 1);

    ftxui::Elements row;
    row.push_back(ftxui::text("> ") | ftxui::bold);
    if (command_palette.query.empty())
    {
        row.push_back(ftxui::text("Enter command") | ftxui::color(theme::muted));
        row.push_back(ftxui::text(" ") | ftxui::inverted);
        return ftxui::hbox(std::move(row));
    }

    if (!prefix.empty())
    {
        row.push_back(ftxui::text(prefix));
    }

    row.push_back(ftxui::text(cursor_text) | ftxui::inverted);

    if (!suffix.empty())
    {
        row.push_back(ftxui::text(suffix));
    }

    return ftxui::hbox(std::move(row)) | ftxui::focusPosition(static_cast<int>(cursor_position) + 2, 0) | ftxui::xframe;
}

ftxui::Element build_palette_help(CommandPaletteMode mode)
{
    auto sep = []() { return ftxui::text("  "); };

    if (mode == CommandPaletteMode::History)
    {
        return ftxui::hbox({
            theme::key_hint("Enter", "execute"),
            sep(),
            theme::key_hint("Tab", "copy to input"),
            sep(),
            theme::key_hint("Ctrl+R", "commands"),
            sep(),
            theme::key_hint("PgUp/PgDn", "scroll"),
            sep(),
            theme::key_hint("Ctrl+Left/Right", "h-scroll"),
            sep(),
            theme::key_hint("Esc", "close"),
        });
    }

    if (mode == CommandPaletteMode::CloseOpenFile || mode == CommandPaletteMode::SelectTimestampSource || mode == CommandPaletteMode::SelectTimestampFormat)
    {
        return ftxui::hbox({
            theme::key_hint("Up/Down", "select"),
            sep(),
            theme::key_hint("Enter", mode == CommandPaletteMode::CloseOpenFile ? "close file" : "confirm"),
            sep(),
            theme::key_hint("PgUp/PgDn", "scroll"),
            sep(),
            theme::key_hint("Ctrl+Left/Right", "h-scroll"),
            sep(),
            theme::key_hint("Esc", "cancel"),
        });
    }

    if (mode == CommandPaletteMode::DeleteFilters)
    {
        return ftxui::hbox({
            theme::key_hint("Up/Down", "select"),
            sep(),
            theme::key_hint("Space", "toggle"),
            sep(),
            theme::key_hint("Enter", "delete marked"),
            sep(),
            theme::key_hint("PgUp/PgDn", "scroll"),
            sep(),
            theme::key_hint("Ctrl+Left/Right", "h-scroll"),
            sep(),
            theme::key_hint("Esc", "cancel"),
        });
    }

    return ftxui::hbox({
        theme::key_hint("Tab", "complete"),
        sep(),
        theme::key_hint("Enter", "execute"),
        sep(),
        theme::key_hint("Ctrl+R", "history"),
        sep(),
        theme::key_hint("PgUp/PgDn", "scroll"),
        sep(),
        theme::key_hint("Ctrl+Left/Right", "h-scroll"),
        sep(),
        theme::key_hint("Esc", "close"),
    });
}

int estimate_result_viewport_col_count(int measured_col_count)
{
    if (measured_col_count > 1)
    {
        return measured_col_count;
    }

    return 68;
}

} // namespace

ftxui::Element CommandPaletteView::render(CommandPaletteController& command_palette_controller, int preferred_result_height)
{
    const CommandPaletteModel& command_palette = command_palette_controller.model();

    const int effective_height = std::max(1, preferred_result_height);
    const int effective_width  = estimate_result_viewport_col_count(_result_text_view.viewport_col_count());
    command_palette_controller.result_text_view_controller().update_viewport_line_count(effective_height);
    command_palette_controller.result_text_view_controller().update_viewport_col_count(effective_width);

    auto result_data                         = command_palette_controller.result_text_view_controller().render_data();
    const std::vector<std::string> result_lines = command_palette_controller.result_lines();

    if (const auto selected_range = command_palette_controller.selected_result_line_range(); selected_range.has_value())
    {
        for (int line_index = selected_range->first; line_index < selected_range->second; ++line_index)
        {
            if (line_index < 0 || line_index >= static_cast<int>(result_lines.size()))
            {
                continue;
            }

            TextViewRangeDecoration decoration;
            decoration.line_index     = line_index;
            decoration.col_start      = 0;
            decoration.col_end        = static_cast<int>(result_lines[static_cast<std::size_t>(line_index)].size());
            decoration.style.inverted = true;
            result_data.range_decorations.push_back(std::move(decoration));
        }
    }

    const auto draw_results = [result_lines](ftxui::Canvas& canvas, int first_line, int line_count, int first_col, int col_count)
    {
        const int max_line_count = std::max(0, std::min(line_count, static_cast<int>(result_lines.size()) - first_line));
        for (int row = 0; row < max_line_count; ++row)
        {
            const int line_index = first_line + row;
            if (line_index < 0 || line_index >= static_cast<int>(result_lines.size()))
            {
                continue;
            }

            const auto& line = result_lines[static_cast<std::size_t>(line_index)];
            if (first_col >= static_cast<int>(line.size()))
            {
                continue;
            }

            canvas.DrawText(0, row * 4, line.substr(static_cast<std::size_t>(first_col), static_cast<std::size_t>(col_count)));
        }
    };

    auto results = _result_text_view.render(result_data, draw_results) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, effective_height) | ftxui::flex;

    ftxui::Element status = build_palette_help(command_palette.mode);
    if (!command_palette.status_message.empty())
    {
        status = ftxui::text(command_palette.status_message) | ftxui::color(command_palette.status_is_error ? theme::error_fg : theme::success_fg);
    }

    const std::string title = command_palette.mode == CommandPaletteMode::History                 ? "Command History"
                              : command_palette.mode == CommandPaletteMode::CloseOpenFile         ? "Close Open File"
                              : command_palette.mode == CommandPaletteMode::SelectTimestampSource ? "Select Log Source"
                              : command_palette.mode == CommandPaletteMode::SelectTimestampFormat ? "Select Timestamp Format"
                              : command_palette.mode == CommandPaletteMode::DeleteFilters         ? "Delete Filters"
                                                                                                   : "Command Palette";

    ftxui::Elements body;
    if (command_palette.mode == CommandPaletteMode::CloseOpenFile)
    {
        body.push_back(ftxui::text("Select file to close") | ftxui::color(theme::muted));
    }
    else if (command_palette.mode == CommandPaletteMode::SelectTimestampSource)
    {
        body.push_back(ftxui::text("Select source to reparse") | ftxui::color(theme::muted));
    }
    else if (command_palette.mode == CommandPaletteMode::SelectTimestampFormat)
    {
        body.push_back(ftxui::text("Select timestamp format") | ftxui::color(theme::muted));
    }
    else if (command_palette.mode == CommandPaletteMode::DeleteFilters)
    {
        body.push_back(ftxui::text("Mark filters to delete") | ftxui::color(theme::muted));
    }
    else
    {
        body.push_back(render_command_palette_query(command_palette));
    }
    body.push_back(ftxui::separator());
    body.push_back(std::move(results));
    body.push_back(ftxui::separator());
    body.push_back(status);

    return ftxui::center(ftxui::clear_under(ftxui::window(ftxui::text(title), ftxui::vbox(std::move(body))) | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 80)));
}

} // namespace slayerlog
