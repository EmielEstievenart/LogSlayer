#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>

#include <ftxui_components/text_view_controller.hpp>

#include "command_history.hpp"
#include "command_manager.hpp"
#include "command_palette_model.hpp"

namespace slayerlog
{

class CommandPaletteController
{
public:
    CommandPaletteController(CommandPaletteModel& model, CommandManager& command_manager);
    CommandPaletteController(CommandPaletteModel& model, CommandManager& command_manager, CommandHistory& command_history);

    bool is_open() const;
    const CommandPaletteModel& model() const;

    void open();
    void open_with_query(std::string query);
    void open_history();
    void open_close_open_file_picker(std::vector<std::string> open_files, std::function<CommandResult(std::size_t selected_index)> on_confirm);
    void open_timestamp_source_picker(std::vector<std::string> sources, std::function<CommandResult(std::size_t selected_index)> on_confirm);
    void open_timestamp_format_picker(std::vector<std::string> formats, std::function<CommandResult(std::size_t selected_index)> on_confirm);
    void open_delete_filters_picker(std::vector<CommandPaletteModel::FilterPickerEntry> filters, std::function<CommandResult(const std::vector<CommandPaletteModel::FilterPickerEntry>& selected_filters)> on_confirm);
    void close();
    bool handle_event(const ftxui::Event& event);

    TextViewController& result_text_view_controller();
    const TextViewController& result_text_view_controller() const;
    const std::vector<std::string>& result_lines() const;
    std::optional<std::pair<int, int>> selected_result_line_range() const;

private:
    void autocomplete_selected_command();
    void refresh_matches();
    void refresh_hidden_column_preview();
    void rebuild_result_lines();
    void ensure_selected_result_visible();
    std::size_t active_match_count() const;
    void move_selection(int delta);
    bool copy_selected_history_entry_to_query();
    CommandResult execute_command_from_command_mode();
    CommandResult execute_command_from_history_mode();
    CommandResult execute_close_open_file_selection();
    CommandResult execute_timestamp_source_selection();
    CommandResult execute_timestamp_format_selection();
    CommandResult execute_delete_filters_selection();
    bool record_successful_command(std::string_view command_line);

    CommandPaletteModel& _model;
    CommandManager& _command_manager;
    CommandHistory* _command_history = nullptr;
    std::function<CommandResult(std::size_t selected_index)> _close_open_file_selection_handler;
    std::function<CommandResult(std::size_t selected_index)> _timestamp_source_selection_handler;
    std::function<CommandResult(std::size_t selected_index)> _timestamp_format_selection_handler;
    std::function<CommandResult(const std::vector<CommandPaletteModel::FilterPickerEntry>& selected_filters)> _delete_filters_selection_handler;
    TextViewController _result_text_view_controller;
    std::vector<std::string> _result_lines;
    std::vector<int> _result_line_to_entry_index;
};

} // namespace slayerlog
