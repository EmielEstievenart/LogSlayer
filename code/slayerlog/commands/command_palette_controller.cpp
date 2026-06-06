#include "command_palette_controller.hpp"
#include <ftxui/component/event.hpp>
#include <ftxui/dom/canvas.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "timestamp/log_timestamp.hpp"

namespace slayerlog
{

namespace
{

bool is_utf8_continuation_byte(unsigned char value)
{
    return (value & 0xC0U) == 0x80U;
}

std::size_t previous_codepoint_start(const std::string& text, std::size_t cursor_position)
{
    if (cursor_position == 0)
    {
        return 0;
    }

    std::size_t position = cursor_position - 1;
    while (position > 0 && is_utf8_continuation_byte(static_cast<unsigned char>(text[position])))
    {
        --position;
    }

    return position;
}

std::size_t next_codepoint_end(const std::string& text, std::size_t cursor_position)
{
    if (cursor_position >= text.size())
    {
        return text.size();
    }

    std::size_t position = cursor_position + 1;
    while (position < text.size() && is_utf8_continuation_byte(static_cast<unsigned char>(text[position])))
    {
        ++position;
    }

    return position;
}

std::string command_arguments_from_query(std::string_view query)
{
    const std::size_t separator_index = query.find_first_of(" \t\r\n");
    if (separator_index == std::string_view::npos)
    {
        return {};
    }

    std::string arguments(query.substr(separator_index + 1));
    const std::size_t first_argument = arguments.find_first_not_of(" \t\r\n");
    if (first_argument == std::string::npos)
    {
        return {};
    }

    arguments.erase(0, first_argument);
    return arguments;
}

std::pair<std::size_t, std::size_t> command_name_range(std::string_view query)
{
    const std::size_t command_start = query.find_first_not_of(" \t\r\n");
    if (command_start == std::string_view::npos)
    {
        return {query.size(), query.size()};
    }

    const std::size_t command_end = query.find_first_of(" \t\r\n", command_start);
    if (command_end == std::string_view::npos)
    {
        return {command_start, query.size()};
    }

    return {command_start, command_end};
}

bool is_result_scroll_event(ftxui::Event event)
{
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown || event == ftxui::Event::ArrowLeftCtrl || event == ftxui::Event::ArrowRightCtrl || event == ftxui::Event::PageUp || event == ftxui::Event::PageDown)
    {
        return true;
    }

    if (!event.is_mouse())
    {
        return false;
    }

    return event.mouse().button == ftxui::Mouse::WheelUp || event.mouse().button == ftxui::Mouse::WheelDown || event.mouse().button == ftxui::Mouse::Left;
}

} // namespace

CommandPaletteController::CommandPaletteController(CommandPaletteModel& model, CommandManager& command_manager) : _model(model), _command_manager(command_manager)
{
    initialize_result_text_view();
    refresh_matches();
}

CommandPaletteController::CommandPaletteController(CommandPaletteModel& model, CommandManager& command_manager, CommandHistory& command_history) : _model(model), _command_manager(command_manager), _command_history(&command_history)
{
    initialize_result_text_view();
    refresh_matches();
}

bool CommandPaletteController::is_open() const
{
    return _model.open;
}

const CommandPaletteModel& CommandPaletteController::model() const
{
    return _model;
}

Command* CommandPaletteController::active_command()
{
    return _command_manager.active_command();
}

const Command* CommandPaletteController::active_command() const
{
    return _command_manager.active_command();
}

void CommandPaletteController::open()
{
    _command_manager.cancel_active_command();
    _model.open = true;
    _model.mode = CommandPaletteMode::Commands;
    _model.query.clear();
    _model.open_files.clear();
    _model.timestamp_formats.clear();
    _model.filter_picker_entries.clear();
    _model.timestamp_offset_source_label.clear();
    _model.timestamp_offset_preview.clear();
    _model.timestamp_offset_preview_is_error = false;
    _close_open_file_selection_handler       = {};
    _timestamp_source_selection_handler      = {};
    _timestamp_format_selection_handler      = {};
    _timestamp_offset_input_handler          = {};
    _delete_filters_selection_handler        = {};
    _model.cursor_position                   = 0;
    _model.selected_index                    = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
}

void CommandPaletteController::open_with_query(std::string query)
{
    open();
    _model.query           = std::move(query);
    _model.cursor_position = _model.query.size();
    refresh_matches();
}

void CommandPaletteController::open_history()
{
    _command_manager.cancel_active_command();
    _model.open = true;
    _model.mode = CommandPaletteMode::History;
    _model.query.clear();
    _model.open_files.clear();
    _model.timestamp_formats.clear();
    _model.filter_picker_entries.clear();
    _model.timestamp_offset_source_label.clear();
    _model.timestamp_offset_preview.clear();
    _model.timestamp_offset_preview_is_error = false;
    _close_open_file_selection_handler       = {};
    _timestamp_source_selection_handler      = {};
    _timestamp_format_selection_handler      = {};
    _timestamp_offset_input_handler          = {};
    _delete_filters_selection_handler        = {};
    _model.cursor_position                   = 0;
    _model.selected_index                    = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
}

void CommandPaletteController::open_history_with_query(std::string query)
{
    open_history();
    _model.query           = std::move(query);
    _model.cursor_position = _model.query.size();
    refresh_matches();
}

void CommandPaletteController::open_close_open_file_picker(std::vector<std::string> open_files, std::function<CommandResult(std::size_t selected_index)> on_confirm)
{
    _model.open = true;
    _model.mode = CommandPaletteMode::CloseOpenFile;
    _model.query.clear();
    _model.open_files = std::move(open_files);
    _model.timestamp_formats.clear();
    _model.filter_picker_entries.clear();
    _model.timestamp_offset_source_label.clear();
    _model.timestamp_offset_preview.clear();
    _model.timestamp_offset_preview_is_error = false;
    _close_open_file_selection_handler       = std::move(on_confirm);
    _timestamp_source_selection_handler      = {};
    _timestamp_format_selection_handler      = {};
    _timestamp_offset_input_handler          = {};
    _delete_filters_selection_handler        = {};
    _model.cursor_position                   = 0;
    _model.selected_index                    = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
}

void CommandPaletteController::open_timestamp_source_picker(std::vector<std::string> sources, std::function<CommandResult(std::size_t selected_index)> on_confirm)
{
    _model.open = true;
    _model.mode = CommandPaletteMode::SelectTimestampSource;
    _model.query.clear();
    _model.open_files = std::move(sources);
    _model.timestamp_formats.clear();
    _model.filter_picker_entries.clear();
    _model.timestamp_offset_source_label.clear();
    _model.timestamp_offset_preview.clear();
    _model.timestamp_offset_preview_is_error = false;
    _close_open_file_selection_handler       = {};
    _timestamp_source_selection_handler      = std::move(on_confirm);
    _timestamp_format_selection_handler      = {};
    _timestamp_offset_input_handler          = {};
    _delete_filters_selection_handler        = {};
    _model.cursor_position                   = 0;
    _model.selected_index                    = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
}

void CommandPaletteController::open_timestamp_format_picker(std::vector<std::string> formats, std::function<CommandResult(std::size_t selected_index)> on_confirm)
{
    _model.open = true;
    _model.mode = CommandPaletteMode::SelectTimestampFormat;
    _model.query.clear();
    _model.open_files.clear();
    _model.timestamp_formats = std::move(formats);
    _model.filter_picker_entries.clear();
    _model.timestamp_offset_source_label.clear();
    _model.timestamp_offset_preview.clear();
    _model.timestamp_offset_preview_is_error = false;
    _close_open_file_selection_handler       = {};
    _timestamp_source_selection_handler      = {};
    _timestamp_format_selection_handler      = std::move(on_confirm);
    _timestamp_offset_input_handler          = {};
    _delete_filters_selection_handler        = {};
    _model.cursor_position                   = 0;
    _model.selected_index                    = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
}

void CommandPaletteController::open_timestamp_offset_input(std::string source_label, std::function<CommandResult(std::string_view offset_text)> on_confirm)
{
    _model.open = true;
    _model.mode = CommandPaletteMode::EnterTimestampOffset;
    _model.query.clear();
    _model.open_files.clear();
    _model.timestamp_formats.clear();
    _model.filter_picker_entries.clear();
    _model.timestamp_offset_source_label = std::move(source_label);
    _close_open_file_selection_handler   = {};
    _timestamp_source_selection_handler  = {};
    _timestamp_format_selection_handler  = {};
    _timestamp_offset_input_handler      = std::move(on_confirm);
    _delete_filters_selection_handler    = {};
    _model.cursor_position               = 0;
    _model.selected_index                = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_timestamp_offset_preview();
    refresh_matches();
}

void CommandPaletteController::open_delete_filters_picker(std::vector<CommandPaletteModel::FilterPickerEntry> filters, std::function<CommandResult(const std::vector<CommandPaletteModel::FilterPickerEntry>& selected_filters)> on_confirm)
{
    _model.open = true;
    _model.mode = CommandPaletteMode::DeleteFilters;
    _model.query.clear();
    _model.open_files.clear();
    _model.timestamp_formats.clear();
    _model.filter_picker_entries = std::move(filters);
    _model.timestamp_offset_source_label.clear();
    _model.timestamp_offset_preview.clear();
    _model.timestamp_offset_preview_is_error = false;
    _close_open_file_selection_handler       = {};
    _timestamp_source_selection_handler      = {};
    _timestamp_format_selection_handler      = {};
    _timestamp_offset_input_handler          = {};
    _delete_filters_selection_handler        = std::move(on_confirm);
    _model.cursor_position                   = 0;
    _model.selected_index                    = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
}

void CommandPaletteController::close()
{
    _command_manager.cancel_active_command();
    _model.open = false;
    _model.mode = CommandPaletteMode::Commands;
    _model.query.clear();
    _model.open_files.clear();
    _model.timestamp_formats.clear();
    _model.filter_picker_entries.clear();
    _model.timestamp_offset_source_label.clear();
    _model.timestamp_offset_preview.clear();
    _model.timestamp_offset_preview_is_error = false;
    _close_open_file_selection_handler       = {};
    _timestamp_source_selection_handler      = {};
    _timestamp_format_selection_handler      = {};
    _timestamp_offset_input_handler          = {};
    _delete_filters_selection_handler        = {};
    _model.cursor_position                   = 0;
    _model.selected_index                    = 0;
    refresh_matches();
}

bool CommandPaletteController::handle_event(const ftxui::Event& event)
{
    if (Command* active_command = _command_manager.active_command())
    {
        const CommandEventResult event_result = active_command->handle_event(event);
        if (event_result.result.has_value())
        {
            apply_command_result(*event_result.result);
        }

        if (event_result.handled)
        {
            return true;
        }

        if (event == ftxui::Event::Escape)
        {
            _command_manager.cancel_active_command();
            close();
            return true;
        }

        return true;
    }

    if (event == ftxui::Event::Escape)
    {
        close();
        return true;
    }

    if (is_result_scroll_event(event))
    {
        if (handle_result_text_view_event(event))
        {
            return true;
        }
    }

    const bool close_open_file_mode  = _model.mode == CommandPaletteMode::CloseOpenFile;
    const bool timestamp_source_mode = _model.mode == CommandPaletteMode::SelectTimestampSource;
    const bool timestamp_format_mode = _model.mode == CommandPaletteMode::SelectTimestampFormat;
    const bool single_selection_mode = close_open_file_mode || timestamp_source_mode || timestamp_format_mode;
    const bool delete_filters_mode   = _model.mode == CommandPaletteMode::DeleteFilters;
    const bool timestamp_offset_mode = _model.mode == CommandPaletteMode::EnterTimestampOffset;

    if (_command_history != nullptr && event == ftxui::Event::CtrlR && !single_selection_mode && !delete_filters_mode && !timestamp_offset_mode)
    {
        _model.mode           = _model.mode == CommandPaletteMode::Commands ? CommandPaletteMode::History : CommandPaletteMode::Commands;
        _model.selected_index = 0;
        _model.status_message.clear();
        _model.status_is_error = false;
        refresh_matches();
        return true;
    }

    if (delete_filters_mode && event == ftxui::Event::Character(" "))
    {
        if (_model.selected_index >= 0 && static_cast<std::size_t>(_model.selected_index) < _model.filter_picker_entries.size())
        {
            auto& entry    = _model.filter_picker_entries[static_cast<std::size_t>(_model.selected_index)];
            entry.selected = !entry.selected;
            _model.status_message.clear();
            _model.status_is_error = false;
            rebuild_result_lines();
            ensure_selected_result_visible();
        }

        return true;
    }

    if ((single_selection_mode || delete_filters_mode) && event != ftxui::Event::Return)
    {
        return true;
    }

    if (event == ftxui::Event::ArrowLeft)
    {
        _model.cursor_position = previous_codepoint_start(_model.query, _model.cursor_position);
        return true;
    }

    if (event == ftxui::Event::ArrowRight)
    {
        _model.cursor_position = next_codepoint_end(_model.query, _model.cursor_position);
        return true;
    }

    if (event == ftxui::Event::Home)
    {
        _model.cursor_position = 0;
        return true;
    }

    if (event == ftxui::Event::End)
    {
        _model.cursor_position = _model.query.size();
        return true;
    }

    if (event == ftxui::Event::Backspace)
    {
        const std::size_t erase_start = previous_codepoint_start(_model.query, _model.cursor_position);
        if (erase_start != _model.cursor_position)
        {
            _model.query.erase(erase_start, _model.cursor_position - erase_start);
            _model.cursor_position = erase_start;
            _model.status_message.clear();
            _model.status_is_error = false;
            refresh_matches();
        }

        return true;
    }

    if (event == ftxui::Event::Delete)
    {
        const std::size_t erase_end = next_codepoint_end(_model.query, _model.cursor_position);
        if (erase_end != _model.cursor_position)
        {
            _model.query.erase(_model.cursor_position, erase_end - _model.cursor_position);
            _model.status_message.clear();
            _model.status_is_error = false;
            refresh_matches();
        }

        return true;
    }

    if (event == ftxui::Event::Return)
    {
        CommandResult result;
        if (_model.mode == CommandPaletteMode::History)
        {
            result = execute_command_from_history_mode();
        }
        else if (_model.mode == CommandPaletteMode::CloseOpenFile)
        {
            result = execute_close_open_file_selection();
        }
        else if (_model.mode == CommandPaletteMode::SelectTimestampSource)
        {
            result = execute_timestamp_source_selection();
        }
        else if (_model.mode == CommandPaletteMode::SelectTimestampFormat)
        {
            result = execute_timestamp_format_selection();
        }
        else if (_model.mode == CommandPaletteMode::EnterTimestampOffset)
        {
            result = execute_timestamp_offset_input();
        }
        else if (_model.mode == CommandPaletteMode::DeleteFilters)
        {
            result = execute_delete_filters_selection();
        }
        else
        {
            result = execute_command_from_command_mode();
        }

        apply_command_result(result);

        return true;
    }

    if (event == ftxui::Event::Tab)
    {
        if (_model.mode == CommandPaletteMode::EnterTimestampOffset)
        {
            return true;
        }

        if (_model.mode == CommandPaletteMode::History)
        {
            copy_selected_history_entry_to_query();
        }
        else if (_model.mode == CommandPaletteMode::Commands)
        {
            autocomplete_selected_command();
        }

        return true;
    }

    if (event.is_character())
    {
        const std::string typed_text = event.character();
        _model.query.insert(_model.cursor_position, typed_text);
        _model.cursor_position += typed_text.size();
        _model.status_message.clear();
        _model.status_is_error = false;
        refresh_matches();
        return true;
    }

    return true;
}

bool CommandPaletteController::handle_result_text_view_event(ftxui::Event event)
{
    if (_result_text_view == nullptr)
    {
        return false;
    }

    const bool selectable          = result_selectable();
    const int fast_horizontal_step = std::max(1, (_result_text_view->controller().viewport_col_count() - 1) / 2);

    if (event == ftxui::Event::ArrowUp)
    {
        if (selectable)
        {
            _result_text_view->user_select_previous();
        }
        else
        {
            _result_text_view->user_scroll_up();
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown)
    {
        if (selectable)
        {
            _result_text_view->user_select_next();
        }
        else
        {
            _result_text_view->user_scroll_down();
        }
        return true;
    }

    if (event == ftxui::Event::PageUp)
    {
        if (selectable)
        {
            _result_text_view->user_page_selected_up();
        }
        else
        {
            _result_text_view->user_page_up();
        }
        return true;
    }

    if (event == ftxui::Event::PageDown)
    {
        if (selectable)
        {
            _result_text_view->user_page_selected_down();
        }
        else
        {
            _result_text_view->user_page_down();
        }
        return true;
    }

    if (event == ftxui::Event::ArrowLeftCtrl)
    {
        _result_text_view->user_scroll_left(fast_horizontal_step);
        return true;
    }

    if (event == ftxui::Event::ArrowRightCtrl)
    {
        _result_text_view->user_scroll_right(fast_horizontal_step);
        return true;
    }

    if (!event.is_mouse())
    {
        return false;
    }

    const auto mouse = event.mouse();
    if (mouse.button == ftxui::Mouse::WheelUp)
    {
        if (selectable)
        {
            _result_text_view->user_select_previous();
        }
        else
        {
            _result_text_view->user_scroll_up();
        }
        return true;
    }

    if (mouse.button == ftxui::Mouse::WheelDown)
    {
        if (selectable)
        {
            _result_text_view->user_select_next();
        }
        else
        {
            _result_text_view->user_scroll_down();
        }
        return true;
    }

    if (selectable && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed)
    {
        const auto position = _result_text_view->text_position_at(mouse.x, mouse.y);
        if (position.has_value())
        {
            _result_text_view->user_select_line_at(*position);
            return true;
        }
    }

    return false;
}

void CommandPaletteController::autocomplete_selected_command()
{
    if (_model.mode != CommandPaletteMode::Commands)
    {
        return;
    }

    if (_model.selected_index < 0 || static_cast<std::size_t>(_model.selected_index) >= _model.matching_commands.size())
    {
        return;
    }

    const auto& selected_command            = _model.matching_commands[static_cast<std::size_t>(_model.selected_index)];
    const auto [command_start, command_end] = command_name_range(_model.query);
    _model.query.replace(command_start, command_end - command_start, selected_command.name);
    _model.cursor_position = command_start + selected_command.name.size();
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
}

void CommandPaletteController::initialize_result_text_view()
{
    TextViewComponentOption option;
    option.draw_content = [this](ftxui::Canvas& canvas, int first_line, int line_count, int first_col, int col_count)
    {
        int selected_entry_index = -1;
        if (_result_text_view != nullptr)
        {
            const auto selected_line = _result_text_view->selected_line();
            if (selected_line.has_value() && *selected_line >= 0 && static_cast<std::size_t>(*selected_line) < _result_line_to_entry_index.size())
            {
                selected_entry_index = _result_line_to_entry_index[static_cast<std::size_t>(*selected_line)];
            }
        }

        const int max_line_count = std::max(0, std::min(line_count, static_cast<int>(_result_lines.size()) - first_line));
        for (int row = 0; row < max_line_count; ++row)
        {
            const int line_index = first_line + row;
            if (line_index < 0 || line_index >= static_cast<int>(_result_lines.size()))
            {
                continue;
            }

            const auto& line = _result_lines[static_cast<std::size_t>(line_index)];
            if (first_col >= static_cast<int>(line.size()))
            {
                continue;
            }

            const int entry_index = static_cast<std::size_t>(line_index) < _result_line_to_entry_index.size() ? _result_line_to_entry_index[static_cast<std::size_t>(line_index)] : -1;
            const bool selected   = selected_entry_index >= 0 && entry_index == selected_entry_index;
            const auto count      = static_cast<std::size_t>(std::min(col_count, static_cast<int>(line.size()) - first_col));

            if (selected)
            {
                canvas.DrawText(0, row * 4, line.substr(static_cast<std::size_t>(first_col), count), [](ftxui::Cell& cell) { cell.inverted = true; });
            }
            else
            {
                canvas.DrawText(0, row * 4, line.substr(static_cast<std::size_t>(first_col), count));
            }
        }
    };
    option.on_selected_line_changed = [this](int line_index) { sync_selected_index_from_result_line(line_index); };
    _result_text_view               = std::make_shared<TextViewComponent>(std::move(option));
    _result_text_view->set_selectable(true);
}

void CommandPaletteController::refresh_matches()
{
    if (_model.mode == CommandPaletteMode::History)
    {
        _model.matching_commands.clear();
        _model.open_files.clear();
        _model.timestamp_formats.clear();
        _model.filter_picker_entries.clear();
        _model.timestamp_offset_source_label.clear();
        _model.timestamp_offset_preview.clear();
        _model.timestamp_offset_preview_is_error = false;
        if (_command_history != nullptr)
        {
            _model.matching_history_entries = _command_history->matching_entries(_model.query);
        }
        else
        {
            _model.matching_history_entries.clear();
        }
    }
    else if (_model.mode == CommandPaletteMode::Commands)
    {
        _model.matching_history_entries.clear();
        _model.open_files.clear();
        _model.timestamp_formats.clear();
        _model.filter_picker_entries.clear();
        _model.timestamp_offset_source_label.clear();
        _model.timestamp_offset_preview.clear();
        _model.timestamp_offset_preview_is_error = false;
        _model.matching_commands                 = _command_manager.matching_commands(_model.query);
    }
    else if (_model.mode == CommandPaletteMode::CloseOpenFile)
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.timestamp_formats.clear();
        _model.filter_picker_entries.clear();
        _model.timestamp_offset_source_label.clear();
        _model.timestamp_offset_preview.clear();
        _model.timestamp_offset_preview_is_error = false;
    }
    else if (_model.mode == CommandPaletteMode::SelectTimestampSource)
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.timestamp_formats.clear();
        _model.filter_picker_entries.clear();
        _model.timestamp_offset_source_label.clear();
        _model.timestamp_offset_preview.clear();
        _model.timestamp_offset_preview_is_error = false;
    }
    else if (_model.mode == CommandPaletteMode::SelectTimestampFormat)
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.open_files.clear();
        _model.filter_picker_entries.clear();
        _model.timestamp_offset_source_label.clear();
        _model.timestamp_offset_preview.clear();
        _model.timestamp_offset_preview_is_error = false;
    }
    else if (_model.mode == CommandPaletteMode::EnterTimestampOffset)
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.open_files.clear();
        _model.timestamp_formats.clear();
        _model.filter_picker_entries.clear();
        refresh_timestamp_offset_preview();
    }
    else if (_model.mode == CommandPaletteMode::DeleteFilters)
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.open_files.clear();
        _model.timestamp_formats.clear();
        _model.timestamp_offset_source_label.clear();
        _model.timestamp_offset_preview.clear();
        _model.timestamp_offset_preview_is_error = false;
    }
    else
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.open_files.clear();
        _model.timestamp_formats.clear();
        _model.timestamp_offset_source_label.clear();
        _model.timestamp_offset_preview.clear();
        _model.timestamp_offset_preview_is_error = false;
    }

    if (active_match_count() == 0)
    {
        _model.selected_index = 0;
        refresh_hidden_column_preview();
        rebuild_result_lines();
        return;
    }

    _model.selected_index = std::clamp(_model.selected_index, 0, static_cast<int>(active_match_count()) - 1);
    refresh_hidden_column_preview();
    rebuild_result_lines();
    ensure_selected_result_visible();
}

TextViewController& CommandPaletteController::result_text_view_controller()
{
    return _result_text_view->controller();
}

const TextViewController& CommandPaletteController::result_text_view_controller() const
{
    return _result_text_view->controller();
}

TextViewComponent& CommandPaletteController::result_text_view_component()
{
    return *_result_text_view;
}

const TextViewComponent& CommandPaletteController::result_text_view_component() const
{
    return *_result_text_view;
}

const std::vector<std::string>& CommandPaletteController::result_lines() const
{
    return _result_lines;
}

std::optional<std::pair<int, int>> CommandPaletteController::selected_result_line_range() const
{
    if (_model.selected_index < 0)
    {
        return std::nullopt;
    }

    int first_line = -1;
    int last_line  = -1;

    for (std::size_t line_index = 0; line_index < _result_line_to_entry_index.size(); ++line_index)
    {
        if (_result_line_to_entry_index[line_index] != _model.selected_index)
        {
            continue;
        }

        if (first_line < 0)
        {
            first_line = static_cast<int>(line_index);
        }

        last_line = static_cast<int>(line_index) + 1;
    }

    if (first_line < 0 || last_line <= first_line)
    {
        return std::nullopt;
    }

    return std::pair<int, int> {first_line, last_line};
}

void CommandPaletteController::refresh_hidden_column_preview()
{
    _model.hidden_column_preview.reset();
    if (_model.mode != CommandPaletteMode::Commands)
    {
        return;
    }

    _model.hidden_column_preview = _command_manager.hidden_column_preview(_model.query);
}

void CommandPaletteController::apply_command_result(const CommandResult& result)
{
    _model.status_message  = result.message;
    _model.status_is_error = !result.success;

    if (result.success && result.close_palette_on_success)
    {
        close();
        return;
    }

    if (result.success)
    {
        if (Command* active_command = _command_manager.active_command(); active_command != nullptr && !active_command->has_active_interaction())
        {
            _command_manager.clear_active_command();
        }
    }
}

std::size_t CommandPaletteController::active_match_count() const
{
    if (_model.mode == CommandPaletteMode::History)
    {
        return _model.matching_history_entries.size();
    }

    if (_model.mode == CommandPaletteMode::CloseOpenFile)
    {
        return _model.open_files.size();
    }

    if (_model.mode == CommandPaletteMode::SelectTimestampSource)
    {
        return _model.open_files.size();
    }

    if (_model.mode == CommandPaletteMode::SelectTimestampFormat)
    {
        return _model.timestamp_formats.size();
    }

    if (_model.mode == CommandPaletteMode::DeleteFilters)
    {
        return _model.filter_picker_entries.size();
    }

    return _model.matching_commands.size();
}

void CommandPaletteController::move_selection(int delta)
{
    const std::size_t match_count = active_match_count();
    if (match_count == 0)
    {
        return;
    }

    const int last_index  = static_cast<int>(match_count) - 1;
    _model.selected_index = std::clamp(_model.selected_index + delta, 0, last_index);
    ensure_selected_result_visible();
}

void CommandPaletteController::rebuild_result_lines()
{
    _result_lines.clear();
    _result_line_to_entry_index.clear();

    auto push_line = [this](std::string line, int entry_index)
    {
        _result_lines.push_back(std::move(line));
        _result_line_to_entry_index.push_back(entry_index);
    };

    if (_model.mode == CommandPaletteMode::History)
    {
        if (_model.matching_history_entries.empty())
        {
            const std::string empty_message = _model.query.empty() ? "No previously run commands" : "No matching history commands";
            push_line(empty_message, -1);
        }
        else
        {
            for (std::size_t index = 0; index < _model.matching_history_entries.size(); ++index)
            {
                push_line(_model.matching_history_entries[index], static_cast<int>(index));
            }
        }
    }
    else if (_model.mode == CommandPaletteMode::CloseOpenFile)
    {
        if (_model.open_files.empty())
        {
            push_line("No open files", -1);
        }
        else
        {
            for (std::size_t index = 0; index < _model.open_files.size(); ++index)
            {
                push_line(_model.open_files[index], static_cast<int>(index));
            }
        }
    }
    else if (_model.mode == CommandPaletteMode::SelectTimestampSource)
    {
        if (_model.open_files.empty())
        {
            push_line("No open sources", -1);
        }
        else
        {
            for (std::size_t index = 0; index < _model.open_files.size(); ++index)
            {
                push_line(_model.open_files[index], static_cast<int>(index));
            }
        }
    }
    else if (_model.mode == CommandPaletteMode::SelectTimestampFormat)
    {
        if (_model.timestamp_formats.empty())
        {
            push_line("No timestamp formats configured", -1);
        }
        else
        {
            for (std::size_t index = 0; index < _model.timestamp_formats.size(); ++index)
            {
                push_line(_model.timestamp_formats[index], static_cast<int>(index));
            }
        }
    }
    else if (_model.mode == CommandPaletteMode::EnterTimestampOffset)
    {
        push_line("Source: " + _model.timestamp_offset_source_label, -1);
        push_line("Expected: DD hh:mm:ss[.fraction]", -1);
        push_line("Example: 20 02:10:10.005", -1);
        if (_model.timestamp_offset_preview.empty())
        {
            push_line("Enter an offset", -1);
        }
        else
        {
            push_line(_model.timestamp_offset_preview, -1);
        }
    }
    else if (_model.mode == CommandPaletteMode::DeleteFilters)
    {
        if (_model.filter_picker_entries.empty())
        {
            push_line("No filters configured", -1);
        }
        else
        {
            for (std::size_t index = 0; index < _model.filter_picker_entries.size(); ++index)
            {
                const auto& entry        = _model.filter_picker_entries[index];
                const std::string prefix = entry.selected ? "[x] " : "[ ] ";
                const std::string tag    = entry.include ? "(in) " : "(out) ";
                push_line(prefix + tag + entry.label, static_cast<int>(index));
            }
        }
    }
    else
    {
        if (_model.matching_commands.empty())
        {
            push_line("No matching commands", -1);
        }
        else
        {
            for (std::size_t index = 0; index < _model.matching_commands.size(); ++index)
            {
                const auto& command = _model.matching_commands[index];
                push_line(command.name + " - " + command.summary, static_cast<int>(index));
                push_line(command.usage, static_cast<int>(index));
            }
        }
    }

    int max_line_width = 0;
    for (const auto& line : _result_lines)
    {
        max_line_width = std::max(max_line_width, static_cast<int>(line.size()));
    }

    const int selected_index = _model.selected_index;
    _result_text_view->set_selector_step(static_cast<int>(result_selector_step()));
    _result_text_view->set_selectable(result_selectable());
    _result_text_view->update_content_size(static_cast<int>(_result_lines.size()), max_line_width);
    _model.selected_index = selected_index;
    _result_text_view->controller().scroll_to_top();
    _result_text_view->controller().scroll_left((std::numeric_limits<int>::max)());
    sync_result_text_view_selection();
}

void CommandPaletteController::ensure_selected_result_visible()
{
    const auto selected_range = selected_result_line_range();
    if (!selected_range.has_value())
    {
        return;
    }

    auto& text_view_controller = _result_text_view->controller();
    const int viewport_lines   = std::max(1, text_view_controller.viewport_line_count());
    const int visible_first    = text_view_controller.first_visible_line();
    const int visible_last     = visible_first + viewport_lines - 1;
    const int selected_first   = selected_range->first;
    const int selected_last    = selected_range->second - 1;

    if (selected_first < visible_first)
    {
        text_view_controller.scroll_up(visible_first - selected_first);
        return;
    }

    if (selected_last > visible_last)
    {
        text_view_controller.scroll_down(selected_last - visible_last);
    }
}

void CommandPaletteController::sync_result_text_view_selection()
{
    if (_result_text_view == nullptr || !result_selectable())
    {
        return;
    }

    const auto selected_range = selected_result_line_range();
    if (selected_range.has_value())
    {
        _result_text_view->set_selected_line(selected_range->first, true);
    }
}

void CommandPaletteController::sync_selected_index_from_result_line(int line_index)
{
    if (line_index < 0 || static_cast<std::size_t>(line_index) >= _result_line_to_entry_index.size())
    {
        return;
    }

    const int entry_index = _result_line_to_entry_index[static_cast<std::size_t>(line_index)];
    if (entry_index < 0)
    {
        return;
    }

    _model.selected_index = entry_index;
}

std::size_t CommandPaletteController::result_selector_step() const
{
    return _model.mode == CommandPaletteMode::Commands ? 2U : 1U;
}

bool CommandPaletteController::result_selectable() const
{
    return _model.mode != CommandPaletteMode::EnterTimestampOffset && active_match_count() > 0;
}

bool CommandPaletteController::copy_selected_history_entry_to_query()
{
    if (_model.mode != CommandPaletteMode::History)
    {
        return false;
    }

    if (_model.selected_index < 0 || static_cast<std::size_t>(_model.selected_index) >= _model.matching_history_entries.size())
    {
        return false;
    }

    _model.query           = _model.matching_history_entries[static_cast<std::size_t>(_model.selected_index)];
    _model.cursor_position = _model.query.size();
    _model.mode            = CommandPaletteMode::Commands;
    _model.selected_index  = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
    return true;
}

CommandResult CommandPaletteController::execute_command_from_command_mode()
{
    std::string command_line;
    if (!_model.matching_commands.empty())
    {
        const auto& selected_command = _model.matching_commands[static_cast<std::size_t>(_model.selected_index)];
        const std::string arguments  = command_arguments_from_query(_model.query);
        command_line                 = arguments.empty() ? selected_command.name : selected_command.name + " " + arguments;
    }
    else
    {
        command_line = _model.query;
    }

    CommandResult result = _command_manager.execute(command_line);
    if (result.success && !record_successful_command(command_line))
    {
        result.message += " (failed to save history)";
    }

    return result;
}

CommandResult CommandPaletteController::execute_command_from_history_mode()
{
    if (_command_history == nullptr)
    {
        return {false, "Command history is not available."};
    }

    if (_model.matching_history_entries.empty())
    {
        CommandResult result = _command_manager.execute(_model.query);
        if (result.success && !record_successful_command(_model.query))
        {
            result.message += " (failed to save history)";
        }

        return result;
    }

    const std::string& command_line = _model.matching_history_entries[static_cast<std::size_t>(_model.selected_index)];
    CommandResult result            = _command_manager.execute(command_line);
    if (result.success && !record_successful_command(command_line))
    {
        result.message += " (failed to save history)";
    }

    return result;
}

CommandResult CommandPaletteController::execute_close_open_file_selection()
{
    if (_close_open_file_selection_handler == nullptr)
    {
        return {false, "No close-file handler is configured."};
    }

    if (_model.selected_index < 0 || static_cast<std::size_t>(_model.selected_index) >= _model.open_files.size())
    {
        return {false, "No open file is selected."};
    }

    return _close_open_file_selection_handler(static_cast<std::size_t>(_model.selected_index));
}

CommandResult CommandPaletteController::execute_timestamp_source_selection()
{
    if (_timestamp_source_selection_handler == nullptr)
    {
        return {false, "No timestamp source handler is configured."};
    }

    if (_model.selected_index < 0 || static_cast<std::size_t>(_model.selected_index) >= _model.open_files.size())
    {
        return {false, "No source is selected."};
    }

    auto handler = _timestamp_source_selection_handler;
    return handler(static_cast<std::size_t>(_model.selected_index));
}

CommandResult CommandPaletteController::execute_timestamp_format_selection()
{
    if (_timestamp_format_selection_handler == nullptr)
    {
        return {false, "No timestamp format handler is configured."};
    }

    if (_model.selected_index < 0 || static_cast<std::size_t>(_model.selected_index) >= _model.timestamp_formats.size())
    {
        return {false, "No timestamp format is selected."};
    }

    auto handler = _timestamp_format_selection_handler;
    return handler(static_cast<std::size_t>(_model.selected_index));
}

CommandResult CommandPaletteController::execute_timestamp_offset_input()
{
    if (_timestamp_offset_input_handler == nullptr)
    {
        return {false, "No timestamp offset handler is configured."};
    }

    if (!parse_log_timestamp_offset(_model.query).has_value())
    {
        refresh_timestamp_offset_preview();
        return {false, _model.timestamp_offset_preview.empty() ? "Invalid offset: expected DD hh:mm:ss[.fraction]" : _model.timestamp_offset_preview, false};
    }

    return _timestamp_offset_input_handler(_model.query);
}

void CommandPaletteController::refresh_timestamp_offset_preview()
{
    if (_model.mode != CommandPaletteMode::EnterTimestampOffset)
    {
        return;
    }

    if (_model.query.empty())
    {
        _model.timestamp_offset_preview          = "Enter offset as DD hh:mm:ss[.fraction]";
        _model.timestamp_offset_preview_is_error = false;
        return;
    }

    const auto offset = parse_log_timestamp_offset(_model.query);
    if (!offset.has_value())
    {
        _model.timestamp_offset_preview          = "Invalid offset: expected DD hh:mm:ss[.fraction]";
        _model.timestamp_offset_preview_is_error = true;
        return;
    }

    _model.timestamp_offset_preview          = "Applies offset: " + format_log_timestamp_offset(*offset);
    _model.timestamp_offset_preview_is_error = false;
}

CommandResult CommandPaletteController::execute_delete_filters_selection()
{
    if (_delete_filters_selection_handler == nullptr)
    {
        return {false, "No delete-filters handler is configured."};
    }

    std::vector<CommandPaletteModel::FilterPickerEntry> selected_filters;
    for (const auto& entry : _model.filter_picker_entries)
    {
        if (entry.selected)
        {
            selected_filters.push_back(entry);
        }
    }

    if (selected_filters.empty())
    {
        return {false, "No filters are marked for deletion."};
    }

    return _delete_filters_selection_handler(selected_filters);
}

bool CommandPaletteController::record_successful_command(std::string_view command_line)
{
    if (_command_history == nullptr)
    {
        return true;
    }

    std::string error_message;
    return _command_history->record_command(command_line, error_message);
}

} // namespace slayerlog
