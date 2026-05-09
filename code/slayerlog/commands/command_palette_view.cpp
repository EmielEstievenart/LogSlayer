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

    if (mode == CommandPaletteMode::EnterTimestampOffset)
    {
        return ftxui::hbox({
            theme::key_hint("Enter", "apply"),
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

int result_view_height(int screen_height)
{
    const int available_height = std::max(12, screen_height - 8);
    return std::max(12, (available_height * 4) / 5);
}

ftxui::Element center_in_overlay(ftxui::Element content, int screen_height)
{
    auto horizontal_padding = []() { return ftxui::filler() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 2); };

    return ftxui::vbox({
               ftxui::filler(),
               ftxui::hbox({
                   ftxui::filler(),
                   horizontal_padding(),
                   std::move(content),
                   horizontal_padding(),
                   ftxui::filler(),
               }),
               ftxui::filler(),
           }) |
           ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, std::max(1, screen_height));
}

} // namespace

ftxui::Element CommandPaletteView::render(CommandPaletteController& command_palette_controller, int screen_height)
{
    const CommandPaletteModel& command_palette = command_palette_controller.model();

    if (Command* active_command = command_palette_controller.active_command())
    {
        ftxui::Element status = active_command->render_help();
        if (!command_palette.status_message.empty())
        {
            status = ftxui::text(command_palette.status_message) | ftxui::color(command_palette.status_is_error ? theme::error_fg : theme::success_fg);
        }

        auto palette = ftxui::clear_under(ftxui::window(ftxui::text(active_command->palette_title()),
                                                        ftxui::vbox({
                                                            active_command->render() |
                                                                ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, result_view_height(screen_height)) |
                                                                ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 116) | ftxui::flex,
                                                            ftxui::separator(),
                                                            std::move(status),
                                                        }))) |
                       ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 120) |
                       ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, std::max(12, screen_height - 4));
        return center_in_overlay(std::move(palette), screen_height);
    }

    auto results = command_palette_controller.result_text_view_component().Render() |
                   ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, result_view_height(screen_height)) |
                   ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 116) |
                   ftxui::flex;

    ftxui::Element status = build_palette_help(command_palette.mode);
    if (!command_palette.status_message.empty())
    {
        status = ftxui::text(command_palette.status_message) | ftxui::color(command_palette.status_is_error ? theme::error_fg : theme::success_fg);
    }
    else if (command_palette.mode == CommandPaletteMode::EnterTimestampOffset && !command_palette.timestamp_offset_preview.empty())
    {
        status = ftxui::text(command_palette.timestamp_offset_preview) | ftxui::color(command_palette.timestamp_offset_preview_is_error ? theme::error_fg : theme::success_fg);
    }

    const std::string title = command_palette.mode == CommandPaletteMode::History                 ? "Command History"
                              : command_palette.mode == CommandPaletteMode::CloseOpenFile         ? "Close Open File"
                              : command_palette.mode == CommandPaletteMode::SelectTimestampSource ? "Select Log Source"
                              : command_palette.mode == CommandPaletteMode::SelectTimestampFormat ? "Select Timestamp Format"
                              : command_palette.mode == CommandPaletteMode::EnterTimestampOffset  ? "Set Timestamp Offset"
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
    else if (command_palette.mode == CommandPaletteMode::EnterTimestampOffset)
    {
        body.push_back(render_command_palette_query(command_palette));
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

    auto palette = ftxui::clear_under(ftxui::window(ftxui::text(title), ftxui::vbox(std::move(body))) |
                                      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 120) |
                                      ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, std::max(12, screen_height - 4)));
    return center_in_overlay(std::move(palette), screen_height);
}

} // namespace slayerlog
