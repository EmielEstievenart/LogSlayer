#include "command_palette_session.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

} // namespace

CommandPaletteSession::CommandPaletteSession(CommandPaletteModel& model, CommandManager& command_manager) : _model(model), _command_manager(command_manager)
{
}

CommandPaletteSession::CommandPaletteSession(CommandPaletteModel& model, CommandManager& command_manager, CommandHistory& command_history) : _model(model), _command_manager(command_manager), _command_history(&command_history)
{
}

void CommandPaletteSession::set_results_changed_callback(std::function<void()> callback)
{
    _results_changed_callback = std::move(callback);
}

void CommandPaletteSession::set_selection_changed_callback(std::function<void()> callback)
{
    _selection_changed_callback = std::move(callback);
}

bool CommandPaletteSession::is_open() const
{
    return _model.open;
}

bool CommandPaletteSession::has_command_history() const
{
    return _command_history != nullptr;
}

const CommandPaletteModel& CommandPaletteSession::model() const
{
    return _model;
}

void CommandPaletteSession::open()
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

void CommandPaletteSession::open_with_query(std::string query)
{
    open();
    _model.query           = std::move(query);
    _model.cursor_position = _model.query.size();
    refresh_matches();
}

void CommandPaletteSession::open_history()
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

void CommandPaletteSession::open_history_with_query(std::string query)
{
    open_history();
    _model.query           = std::move(query);
    _model.cursor_position = _model.query.size();
    refresh_matches();
}

void CommandPaletteSession::open_close_open_file_picker(std::vector<std::string> open_files, std::function<CommandResult(std::size_t selected_index)> on_confirm)
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

void CommandPaletteSession::open_timestamp_source_picker(std::vector<std::string> sources, std::function<CommandResult(std::size_t selected_index)> on_confirm)
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

void CommandPaletteSession::open_timestamp_format_picker(std::vector<std::string> formats, std::function<CommandResult(std::size_t selected_index)> on_confirm)
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

void CommandPaletteSession::open_timestamp_offset_input(std::string source_label, std::function<CommandResult(std::string_view offset_text)> on_confirm)
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

void CommandPaletteSession::open_delete_filters_picker(std::vector<CommandPaletteModel::FilterPickerEntry> filters, std::function<CommandResult(const std::vector<CommandPaletteModel::FilterPickerEntry>& selected_filters)> on_confirm)
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

void CommandPaletteSession::close()
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

void CommandPaletteSession::insert_text(std::string_view text)
{
    _model.query.insert(_model.cursor_position, std::string(text));
    _model.cursor_position += text.size();
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
}

void CommandPaletteSession::erase_previous_character()
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
}

void CommandPaletteSession::erase_next_character()
{
    const std::size_t erase_end = next_codepoint_end(_model.query, _model.cursor_position);
    if (erase_end != _model.cursor_position)
    {
        _model.query.erase(_model.cursor_position, erase_end - _model.cursor_position);
        _model.status_message.clear();
        _model.status_is_error = false;
        refresh_matches();
    }
}

void CommandPaletteSession::move_cursor_left()
{
    _model.cursor_position = previous_codepoint_start(_model.query, _model.cursor_position);
}

void CommandPaletteSession::move_cursor_right()
{
    _model.cursor_position = next_codepoint_end(_model.query, _model.cursor_position);
}

void CommandPaletteSession::move_cursor_to_start()
{
    _model.cursor_position = 0;
}

void CommandPaletteSession::move_cursor_to_end()
{
    _model.cursor_position = _model.query.size();
}

std::size_t CommandPaletteSession::active_match_count() const
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

void CommandPaletteSession::move_selection(int delta)
{
    const std::size_t match_count = active_match_count();
    if (match_count == 0)
    {
        return;
    }

    const int last_index  = static_cast<int>(match_count) - 1;
    _model.selected_index = std::clamp(_model.selected_index + delta, 0, last_index);
    notify_selection_changed();
}

void CommandPaletteSession::select_entry(int entry_index)
{
    _model.selected_index = entry_index;
}

bool CommandPaletteSession::toggle_history_mode()
{
    if (_command_history == nullptr)
    {
        return false;
    }

    if (_model.mode != CommandPaletteMode::Commands && _model.mode != CommandPaletteMode::History)
    {
        return false;
    }

    _model.mode           = _model.mode == CommandPaletteMode::Commands ? CommandPaletteMode::History : CommandPaletteMode::Commands;
    _model.selected_index = 0;
    _model.status_message.clear();
    _model.status_is_error = false;
    refresh_matches();
    return true;
}

void CommandPaletteSession::toggle_selected_filter()
{
    if (_model.mode != CommandPaletteMode::DeleteFilters)
    {
        return;
    }

    if (_model.selected_index >= 0 && static_cast<std::size_t>(_model.selected_index) < _model.filter_picker_entries.size())
    {
        auto& entry    = _model.filter_picker_entries[static_cast<std::size_t>(_model.selected_index)];
        entry.selected = !entry.selected;
        _model.status_message.clear();
        _model.status_is_error = false;
        notify_results_changed();
        notify_selection_changed();
    }
}

void CommandPaletteSession::autocomplete_selected_command()
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

bool CommandPaletteSession::copy_selected_history_entry_to_query()
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

void CommandPaletteSession::submit()
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
}

void CommandPaletteSession::apply_command_result(const CommandResult& result)
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
        if (CoreCommand* active_command = _command_manager.active_command(); active_command != nullptr && !active_command->has_active_interaction())
        {
            _command_manager.clear_active_command();
        }
    }
}

void CommandPaletteSession::refresh_matches()
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
        notify_results_changed();
        return;
    }

    _model.selected_index = std::clamp(_model.selected_index, 0, static_cast<int>(active_match_count()) - 1);
    refresh_hidden_column_preview();
    notify_results_changed();
    notify_selection_changed();
}

void CommandPaletteSession::refresh_hidden_column_preview()
{
    _model.hidden_column_preview.reset();
    if (_model.mode != CommandPaletteMode::Commands)
    {
        return;
    }

    _model.hidden_column_preview = _command_manager.hidden_column_preview(_model.query);
}

void CommandPaletteSession::refresh_timestamp_offset_preview()
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

void CommandPaletteSession::notify_results_changed()
{
    if (_results_changed_callback)
    {
        _results_changed_callback();
    }
}

void CommandPaletteSession::notify_selection_changed()
{
    if (_selection_changed_callback)
    {
        _selection_changed_callback();
    }
}

CommandResult CommandPaletteSession::execute_command_from_command_mode()
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

CommandResult CommandPaletteSession::execute_command_from_history_mode()
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

CommandResult CommandPaletteSession::execute_close_open_file_selection()
{
    if (_close_open_file_selection_handler == nullptr)
    {
        return {false, "No close-file handler is configured."};
    }

    if (_model.selected_index < 0 || static_cast<std::size_t>(_model.selected_index) >= _model.open_files.size())
    {
        return {false, "No open file is selected."};
    }

    auto handler = _close_open_file_selection_handler;
    return handler(static_cast<std::size_t>(_model.selected_index));
}

CommandResult CommandPaletteSession::execute_timestamp_source_selection()
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

CommandResult CommandPaletteSession::execute_timestamp_format_selection()
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

CommandResult CommandPaletteSession::execute_timestamp_offset_input()
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

    auto handler = _timestamp_offset_input_handler;
    return handler(_model.query);
}

CommandResult CommandPaletteSession::execute_delete_filters_selection()
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

    auto handler = _delete_filters_selection_handler;
    return handler(selected_filters);
}

bool CommandPaletteSession::record_successful_command(std::string_view command_line)
{
    if (_command_history == nullptr)
    {
        return true;
    }

    std::string error_message;
    return _command_history->record_command(command_line, error_message);
}

} // namespace slayerlog
