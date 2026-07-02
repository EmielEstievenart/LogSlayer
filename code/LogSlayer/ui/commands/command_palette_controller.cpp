#include "command_palette_controller.hpp"
#include <ftxui/component/event.hpp>
#include <ftxui/dom/canvas.hpp>

#include "command_palette_result_lines.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace slayerlog
{

namespace
{

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

CommandPaletteController::CommandPaletteController(CommandPaletteModel& model, CommandManager& command_manager) : _command_manager(command_manager), _session(model, command_manager)
{
    initialize_result_text_view();
    _session.set_results_changed_callback([this] { rebuild_result_lines(); });
    _session.set_selection_changed_callback([this] { ensure_selected_result_visible(); });
    _session.refresh_matches();
}

CommandPaletteController::CommandPaletteController(CommandPaletteModel& model, CommandManager& command_manager, CommandHistory& command_history) : _command_manager(command_manager), _session(model, command_manager, command_history)
{
    initialize_result_text_view();
    _session.set_results_changed_callback([this] { rebuild_result_lines(); });
    _session.set_selection_changed_callback([this] { ensure_selected_result_visible(); });
    _session.refresh_matches();
}

bool CommandPaletteController::is_open() const
{
    return _session.is_open();
}

const CommandPaletteModel& CommandPaletteController::model() const
{
    return _session.model();
}

Command* CommandPaletteController::active_command()
{
    return dynamic_cast<Command*>(_command_manager.active_command());
}

const Command* CommandPaletteController::active_command() const
{
    return dynamic_cast<const Command*>(_command_manager.active_command());
}

void CommandPaletteController::open()
{
    _session.open();
}

void CommandPaletteController::open_with_query(std::string query)
{
    _session.open_with_query(std::move(query));
}

void CommandPaletteController::open_history()
{
    _session.open_history();
}

void CommandPaletteController::open_history_with_query(std::string query)
{
    _session.open_history_with_query(std::move(query));
}

void CommandPaletteController::open_close_open_file_picker(std::vector<std::string> open_files, std::function<CommandResult(std::size_t selected_index)> on_confirm)
{
    _session.open_close_open_file_picker(std::move(open_files), std::move(on_confirm));
}

void CommandPaletteController::open_timestamp_source_picker(std::vector<std::string> sources, std::function<CommandResult(std::size_t selected_index)> on_confirm)
{
    _session.open_timestamp_source_picker(std::move(sources), std::move(on_confirm));
}

void CommandPaletteController::open_timestamp_format_picker(std::vector<std::string> formats, std::function<CommandResult(std::size_t selected_index)> on_confirm)
{
    _session.open_timestamp_format_picker(std::move(formats), std::move(on_confirm));
}

void CommandPaletteController::open_timestamp_offset_input(std::string source_label, std::function<CommandResult(std::string_view offset_text)> on_confirm)
{
    _session.open_timestamp_offset_input(std::move(source_label), std::move(on_confirm));
}

void CommandPaletteController::open_delete_filters_picker(std::vector<CommandPaletteModel::FilterPickerEntry> filters, std::function<CommandResult(const std::vector<CommandPaletteModel::FilterPickerEntry>& selected_filters)> on_confirm)
{
    _session.open_delete_filters_picker(std::move(filters), std::move(on_confirm));
}

void CommandPaletteController::close()
{
    _session.close();
}

bool CommandPaletteController::handle_event(const ftxui::Event& event)
{
    if (Command* active_command = dynamic_cast<Command*>(_command_manager.active_command()))
    {
        const CommandEventResult event_result = active_command->handle_event(event);
        if (event_result.result.has_value())
        {
            _session.apply_command_result(*event_result.result);
        }

        if (event_result.handled)
        {
            return true;
        }

        if (event == ftxui::Event::Escape)
        {
            _command_manager.cancel_active_command();
            _session.close();
            return true;
        }

        return true;
    }

    if (event == ftxui::Event::Escape)
    {
        _session.close();
        return true;
    }

    if (is_result_scroll_event(event))
    {
        if (handle_result_text_view_event(event))
        {
            return true;
        }
    }

    const CommandPaletteMode mode    = _session.model().mode;
    const bool close_open_file_mode  = mode == CommandPaletteMode::CloseOpenFile;
    const bool timestamp_source_mode = mode == CommandPaletteMode::SelectTimestampSource;
    const bool timestamp_format_mode = mode == CommandPaletteMode::SelectTimestampFormat;
    const bool single_selection_mode = close_open_file_mode || timestamp_source_mode || timestamp_format_mode;
    const bool delete_filters_mode   = mode == CommandPaletteMode::DeleteFilters;
    const bool timestamp_offset_mode = mode == CommandPaletteMode::EnterTimestampOffset;

    if (_session.has_command_history() && event == ftxui::Event::CtrlR && !single_selection_mode && !delete_filters_mode && !timestamp_offset_mode)
    {
        _session.toggle_history_mode();
        return true;
    }

    if (delete_filters_mode && event == ftxui::Event::Character(" "))
    {
        _session.toggle_selected_filter();
        return true;
    }

    if ((single_selection_mode || delete_filters_mode) && event != ftxui::Event::Return)
    {
        return true;
    }

    if (event == ftxui::Event::ArrowLeft)
    {
        _session.move_cursor_left();
        return true;
    }

    if (event == ftxui::Event::ArrowRight)
    {
        _session.move_cursor_right();
        return true;
    }

    if (event == ftxui::Event::Home)
    {
        _session.move_cursor_to_start();
        return true;
    }

    if (event == ftxui::Event::End)
    {
        _session.move_cursor_to_end();
        return true;
    }

    if (event == ftxui::Event::Backspace)
    {
        _session.erase_previous_character();
        return true;
    }

    if (event == ftxui::Event::Delete)
    {
        _session.erase_next_character();
        return true;
    }

    if (event == ftxui::Event::Return)
    {
        _session.submit();
        return true;
    }

    if (event == ftxui::Event::Tab)
    {
        if (mode == CommandPaletteMode::EnterTimestampOffset)
        {
            return true;
        }

        if (mode == CommandPaletteMode::History)
        {
            _session.copy_selected_history_entry_to_query();
        }
        else if (mode == CommandPaletteMode::Commands)
        {
            _session.autocomplete_selected_command();
        }

        return true;
    }

    if (event.is_character())
    {
        _session.insert_text(event.character());
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
    const int selected_index = _session.model().selected_index;
    if (selected_index < 0)
    {
        return std::nullopt;
    }

    int first_line = -1;
    int last_line  = -1;

    for (std::size_t line_index = 0; line_index < _result_line_to_entry_index.size(); ++line_index)
    {
        if (_result_line_to_entry_index[line_index] != selected_index)
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

void CommandPaletteController::rebuild_result_lines()
{
    const CommandPaletteModel& model      = _session.model();
    CommandPaletteResultLines result_list = build_command_palette_result_lines(model);
    _result_lines                         = std::move(result_list.lines);
    _result_line_to_entry_index           = std::move(result_list.entry_indices);

    int max_line_width = 0;
    for (const auto& line : _result_lines)
    {
        max_line_width = std::max(max_line_width, static_cast<int>(line.size()));
    }

    const int selected_index = model.selected_index;
    _result_text_view->set_selector_step(static_cast<int>(result_selector_step()));
    _result_text_view->set_selectable(result_selectable());
    _result_text_view->update_content_size(static_cast<int>(_result_lines.size()), max_line_width);
    _session.select_entry(selected_index);
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

    _session.select_entry(entry_index);
}

std::size_t CommandPaletteController::result_selector_step() const
{
    return _session.model().mode == CommandPaletteMode::Commands ? 2U : 1U;
}

bool CommandPaletteController::result_selectable() const
{
    return _session.model().mode != CommandPaletteMode::EnterTimestampOffset && _session.active_match_count() > 0;
}

} // namespace slayerlog
