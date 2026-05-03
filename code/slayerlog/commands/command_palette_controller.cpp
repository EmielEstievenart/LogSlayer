#include "command_palette_controller.hpp"
#include <ftxui/component/event.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

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

std::string normalize_command_name(std::string_view name)
{
    std::string normalized;
    normalized.reserve(name.size());
    for (const char ch : name)
    {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    return normalized;
}

bool is_result_scroll_event(ftxui::Event event)
{
    if (event == ftxui::Event::ArrowLeftCtrl || event == ftxui::Event::ArrowRightCtrl || event == ftxui::Event::PageUp || event == ftxui::Event::PageDown)
    {
        return true;
    }

    if (!event.is_mouse())
    {
        return false;
    }

    return event.mouse().button == ftxui::Mouse::WheelUp || event.mouse().button == ftxui::Mouse::WheelDown;
}

} // namespace

CommandPaletteController::CommandPaletteController(CommandPaletteModel& model, CommandManager& command_manager) : _model(model), _command_manager(command_manager)
{
    refresh_matches();
}

CommandPaletteController::CommandPaletteController(CommandPaletteModel& model, CommandManager& command_manager, CommandHistory& command_history) : _model(model), _command_manager(command_manager), _command_history(&command_history)
{
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

void CommandPaletteController::open()
{
    _model.open = true;
    _model.mode = CommandPaletteMode::Commands;
    _model.query.clear();
    _model.open_files.clear();
    _model.timestamp_formats.clear();
    _model.filter_picker_entries.clear();
    _close_open_file_selection_handler  = {};
    _timestamp_source_selection_handler = {};
    _timestamp_format_selection_handler = {};
    _delete_filters_selection_handler   = {};
    _model.cursor_position              = 0;
    _model.selected_index               = 0;
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
    _model.open = true;
    _model.mode = CommandPaletteMode::History;
    _model.query.clear();
    _model.open_files.clear();
    _model.timestamp_formats.clear();
    _model.filter_picker_entries.clear();
    _close_open_file_selection_handler  = {};
    _timestamp_source_selection_handler = {};
    _timestamp_format_selection_handler = {};
    _delete_filters_selection_handler   = {};
    _model.cursor_position              = 0;
    _model.selected_index               = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
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
    _close_open_file_selection_handler  = std::move(on_confirm);
    _timestamp_source_selection_handler = {};
    _timestamp_format_selection_handler = {};
    _delete_filters_selection_handler   = {};
    _model.cursor_position              = 0;
    _model.selected_index               = 0;
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
    _close_open_file_selection_handler  = {};
    _timestamp_source_selection_handler = std::move(on_confirm);
    _timestamp_format_selection_handler = {};
    _delete_filters_selection_handler   = {};
    _model.cursor_position              = 0;
    _model.selected_index               = 0;
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
    _close_open_file_selection_handler  = {};
    _timestamp_source_selection_handler = {};
    _timestamp_format_selection_handler = std::move(on_confirm);
    _delete_filters_selection_handler   = {};
    _model.cursor_position              = 0;
    _model.selected_index               = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
}

void CommandPaletteController::open_delete_filters_picker(std::vector<CommandPaletteModel::FilterPickerEntry> filters, std::function<CommandResult(const std::vector<CommandPaletteModel::FilterPickerEntry>& selected_filters)> on_confirm)
{
    _model.open = true;
    _model.mode = CommandPaletteMode::DeleteFilters;
    _model.query.clear();
    _model.open_files.clear();
    _model.timestamp_formats.clear();
    _model.filter_picker_entries       = std::move(filters);
    _close_open_file_selection_handler  = {};
    _timestamp_source_selection_handler = {};
    _timestamp_format_selection_handler = {};
    _delete_filters_selection_handler   = std::move(on_confirm);
    _model.cursor_position              = 0;
    _model.selected_index               = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
}

void CommandPaletteController::close()
{
    _model.open = false;
    _model.mode = CommandPaletteMode::Commands;
    _model.query.clear();
    _model.open_files.clear();
    _model.timestamp_formats.clear();
    _model.filter_picker_entries.clear();
    _close_open_file_selection_handler  = {};
    _timestamp_source_selection_handler = {};
    _timestamp_format_selection_handler = {};
    _delete_filters_selection_handler   = {};
    _model.cursor_position              = 0;
    _model.selected_index               = 0;
    refresh_matches();
}

bool CommandPaletteController::handle_event(const ftxui::Event& event)
{
    if (event == ftxui::Event::Escape)
    {
        close();
        return true;
    }

    if (is_result_scroll_event(event))
    {
        const auto result = _result_text_view_controller.parse_event(event);
        if (result.handled)
        {
            return true;
        }
    }

    const bool close_open_file_mode       = _model.mode == CommandPaletteMode::CloseOpenFile;
    const bool timestamp_source_mode      = _model.mode == CommandPaletteMode::SelectTimestampSource;
    const bool timestamp_format_mode      = _model.mode == CommandPaletteMode::SelectTimestampFormat;
    const bool single_selection_mode      = close_open_file_mode || timestamp_source_mode || timestamp_format_mode;
    const bool delete_filters_mode        = _model.mode == CommandPaletteMode::DeleteFilters;

    if (_command_history != nullptr && event == ftxui::Event::CtrlR && !single_selection_mode && !delete_filters_mode)
    {
        _model.mode           = _model.mode == CommandPaletteMode::Commands ? CommandPaletteMode::History : CommandPaletteMode::Commands;
        _model.selected_index = 0;
        _model.status_message.clear();
        _model.status_is_error = false;
        refresh_matches();
        return true;
    }

    if (event == ftxui::Event::ArrowUp)
    {
        move_selection(-1);
        return true;
    }

    if (event == ftxui::Event::ArrowDown)
    {
        move_selection(1);
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
        else if (_model.mode == CommandPaletteMode::DeleteFilters)
        {
            result = execute_delete_filters_selection();
        }
        else
        {
            result = execute_command_from_command_mode();
        }

        _model.status_message  = result.message;
        _model.status_is_error = !result.success;
        if (result.success && result.close_palette_on_success)
        {
            close();
        }

        return true;
    }

    if (event == ftxui::Event::Tab)
    {
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

void CommandPaletteController::refresh_matches()
{
    if (_model.mode == CommandPaletteMode::History)
    {
        _model.matching_commands.clear();
        _model.open_files.clear();
        _model.timestamp_formats.clear();
        _model.filter_picker_entries.clear();
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
        _model.matching_commands = _command_manager.matching_commands(_model.query);
    }
    else if (_model.mode == CommandPaletteMode::CloseOpenFile)
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.timestamp_formats.clear();
        _model.filter_picker_entries.clear();
    }
    else if (_model.mode == CommandPaletteMode::SelectTimestampSource)
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.timestamp_formats.clear();
        _model.filter_picker_entries.clear();
    }
    else if (_model.mode == CommandPaletteMode::SelectTimestampFormat)
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.open_files.clear();
        _model.filter_picker_entries.clear();
    }
    else if (_model.mode == CommandPaletteMode::DeleteFilters)
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.open_files.clear();
        _model.timestamp_formats.clear();
    }
    else
    {
        _model.matching_history_entries.clear();
        _model.matching_commands.clear();
        _model.open_files.clear();
        _model.timestamp_formats.clear();
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
    return _result_text_view_controller;
}

const TextViewController& CommandPaletteController::result_text_view_controller() const
{
    return _result_text_view_controller;
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

    const auto [command_start, command_end] = command_name_range(_model.query);
    if (command_start >= _model.query.size())
    {
        return;
    }

    const std::string command_name = normalize_command_name(std::string_view(_model.query).substr(command_start, command_end - command_start));
    if (command_name != "hide-columns")
    {
        return;
    }

    _model.hidden_column_preview = parse_hidden_column_range(command_arguments_from_query(_model.query));
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

    _result_text_view_controller.set_content(static_cast<int>(_result_lines.size()), max_line_width,
                                             [this](int index) -> const std::string& { return _result_lines[static_cast<std::size_t>(index)]; });
    _result_text_view_controller.scroll_to_top();
    _result_text_view_controller.scroll_left((std::numeric_limits<int>::max)());
}

void CommandPaletteController::ensure_selected_result_visible()
{
    const auto selected_range = selected_result_line_range();
    if (!selected_range.has_value())
    {
        return;
    }

    const int viewport_lines = std::max(1, _result_text_view_controller.viewport_line_count());
    const int visible_first  = _result_text_view_controller.first_visible_line();
    const int visible_last   = visible_first + viewport_lines - 1;
    const int selected_first = selected_range->first;
    const int selected_last  = selected_range->second - 1;

    if (selected_first < visible_first)
    {
        _result_text_view_controller.scroll_up(visible_first - selected_first);
        return;
    }

    if (selected_last > visible_last)
    {
        _result_text_view_controller.scroll_down(selected_last - visible_last);
    }
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
