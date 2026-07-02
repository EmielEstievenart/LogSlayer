#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>

#include <ftxui_components/text_view_component.hpp>

#include "command.hpp"
#include "command_history.hpp"
#include "command_manager.hpp"
#include "command_palette_model.hpp"
#include "command_palette_session.hpp"

namespace slayerlog
{

/// FTXUI adapter around the core CommandPaletteSession: translates terminal
/// events into session calls and mirrors the session's result list into a
/// TextViewComponent for rendering. All palette behavior (matching, history,
/// selection, picker flows, execute dispatch) lives in the core session.
class CommandPaletteController
{
public:
    CommandPaletteController(CommandPaletteModel& model, CommandManager& command_manager);
    CommandPaletteController(CommandPaletteModel& model, CommandManager& command_manager, CommandHistory& command_history);

    bool is_open() const;
    const CommandPaletteModel& model() const;
    Command* active_command();
    const Command* active_command() const;

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
    bool handle_event(const ftxui::Event& event);

    TextViewController& result_text_view_controller();
    const TextViewController& result_text_view_controller() const;
    TextViewComponent& result_text_view_component();
    const TextViewComponent& result_text_view_component() const;
    const std::vector<std::string>& result_lines() const;
    std::optional<std::pair<int, int>> selected_result_line_range() const;

private:
    void initialize_result_text_view();
    void rebuild_result_lines();
    bool handle_result_text_view_event(ftxui::Event event);
    void ensure_selected_result_visible();
    void sync_result_text_view_selection();
    void sync_selected_index_from_result_line(int line_index);
    std::size_t result_selector_step() const;
    bool result_selectable() const;

    CommandManager& _command_manager;
    CommandPaletteSession _session;
    std::shared_ptr<TextViewComponent> _result_text_view;
    std::vector<std::string> _result_lines;
    std::vector<int> _result_line_to_entry_index;
};

} // namespace slayerlog
