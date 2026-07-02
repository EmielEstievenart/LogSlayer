#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "command_history.hpp"
#include "command_manager.hpp"
#include "command_palette_model.hpp"

namespace slayerlog
{

/// UI-agnostic command palette state machine. Owns every palette behavior that
/// must stay identical across UI frontends: open/close state, query text
/// editing, match refresh against the CommandManager, selection movement,
/// history mode (list, selection, copy-to-query), the picker/input modes with
/// their confirm handlers, autocomplete, the hidden-column preview, execute
/// dispatch per mode, and recording successful commands to the CommandHistory.
/// It mutates the shared CommandPaletteModel, which each UI renders read-only.
/// UI controllers translate their framework's events into calls on this class
/// and mirror the result list into their own widgets via the two callbacks:
/// the results-changed callback fires when the result-list content must be
/// rebuilt, the selection-changed callback when the selected entry moved and
/// should be scrolled into view.
class CommandPaletteSession
{
public:
    CommandPaletteSession(CommandPaletteModel& model, CommandManager& command_manager);
    CommandPaletteSession(CommandPaletteModel& model, CommandManager& command_manager, CommandHistory& command_history);

    void set_results_changed_callback(std::function<void()> callback);
    void set_selection_changed_callback(std::function<void()> callback);

    bool is_open() const;
    bool has_command_history() const;
    const CommandPaletteModel& model() const;

    void open();
    void open_with_query(std::string query);
    void open_history();
    void open_history_with_query(std::string query);
    void open_close_open_file_picker(std::vector<std::string> open_files, std::function<CommandResult(std::size_t selected_index)> on_confirm);
    void open_timestamp_source_picker(std::vector<std::string> sources, std::function<CommandResult(std::size_t selected_index)> on_confirm);
    void open_timestamp_format_picker(std::vector<std::string> formats, std::function<CommandResult(std::size_t selected_index)> on_confirm);
    void open_timestamp_offset_input(std::string source_label, std::function<CommandResult(std::string_view offset_text)> on_confirm);
    void open_delete_filters_picker(std::vector<CommandPaletteModel::FilterPickerEntry> filters, std::function<CommandResult(const std::vector<CommandPaletteModel::FilterPickerEntry>& selected_filters)> on_confirm);
    void close();

    void insert_text(std::string_view text);
    void erase_previous_character();
    void erase_next_character();
    void move_cursor_left();
    void move_cursor_right();
    void move_cursor_to_start();
    void move_cursor_to_end();

    std::size_t active_match_count() const;
    void move_selection(int delta);
    /// Sets the selected entry index without firing callbacks; used by UI
    /// mirrors to sync a widget-driven selection back into the model.
    void select_entry(int entry_index);

    bool toggle_history_mode();
    void toggle_selected_filter();
    void autocomplete_selected_command();
    bool copy_selected_history_entry_to_query();

    /// Executes the confirm action for the current mode (Return key) and
    /// applies the resulting CommandResult to the palette state.
    void submit();
    /// Applies a command result to the palette: status message, close on
    /// success, and clearing a finished active command. Also used by UI
    /// controllers for results produced by an active interactive command.
    void apply_command_result(const CommandResult& result);

    void refresh_matches();

private:
    void refresh_hidden_column_preview();
    void refresh_timestamp_offset_preview();
    void notify_results_changed();
    void notify_selection_changed();
    CommandResult execute_command_from_command_mode();
    CommandResult execute_command_from_history_mode();
    CommandResult execute_close_open_file_selection();
    CommandResult execute_timestamp_source_selection();
    CommandResult execute_timestamp_format_selection();
    CommandResult execute_timestamp_offset_input();
    CommandResult execute_delete_filters_selection();
    bool record_successful_command(std::string_view command_line);

    CommandPaletteModel& _model;
    CommandManager& _command_manager;
    CommandHistory* _command_history = nullptr;
    std::function<void()> _results_changed_callback;
    std::function<void()> _selection_changed_callback;
    std::function<CommandResult(std::size_t selected_index)> _close_open_file_selection_handler;
    std::function<CommandResult(std::size_t selected_index)> _timestamp_source_selection_handler;
    std::function<CommandResult(std::size_t selected_index)> _timestamp_format_selection_handler;
    std::function<CommandResult(std::string_view offset_text)> _timestamp_offset_input_handler;
    std::function<CommandResult(const std::vector<CommandPaletteModel::FilterPickerEntry>& selected_filters)> _delete_filters_selection_handler;
};

} // namespace slayerlog
