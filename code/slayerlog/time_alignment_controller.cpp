#include "time_alignment_controller.hpp"

#include <algorithm>
#include <utility>

namespace slayerlog
{

void TimeAlignmentController::start(int first_visible_line, ApplyCallback apply_callback)
{
    _apply_callback = std::move(apply_callback);
    _phase          = Phase::SelectSource;
    _source.reset();
    _selected_line = std::max(0, first_visible_line);
    set_status("Select source line");
}

void TimeAlignmentController::cancel()
{
    reset();
}

bool TimeAlignmentController::active() const
{
    return _phase != Phase::Inactive;
}

std::optional<int> TimeAlignmentController::selected_line() const
{
    return _selected_line;
}

const std::string& TimeAlignmentController::status_text() const
{
    return _status;
}

bool TimeAlignmentController::status_is_error() const
{
    return _status_is_error;
}

bool TimeAlignmentController::handle_event(AllProcessedSources& processed_sources, TextViewController& text_view_controller, ftxui::Event event,
                                           const std::function<std::optional<TextViewPosition>(int, int)>& text_position_at, const FindNavigation& find_navigation)
{
    if (event == ftxui::Event::Escape)
    {
        cancel();
        return true;
    }

    if (event == ftxui::Event::Return)
    {
        return confirm_selection(processed_sources);
    }

    if (find_navigation.next && find_navigation.active_visible_index && event == ftxui::Event::ArrowRight)
    {
        const bool moved = find_navigation.next();
        const auto active_visible_index = find_navigation.active_visible_index();
        if (active_visible_index.has_value())
        {
            set_selected_line(processed_sources, text_view_controller, active_visible_index->value, true);
        }
        return moved;
    }

    if (find_navigation.previous && find_navigation.active_visible_index && event == ftxui::Event::ArrowLeft)
    {
        const bool moved = find_navigation.previous();
        const auto active_visible_index = find_navigation.active_visible_index();
        if (active_visible_index.has_value())
        {
            set_selected_line(processed_sources, text_view_controller, active_visible_index->value, true);
        }
        return moved;
    }

    const int page_step = std::max(1, text_view_controller.viewport_line_count() - 1);
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k'))
    {
        move_selection(processed_sources, text_view_controller, -1);
        return true;
    }

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j'))
    {
        move_selection(processed_sources, text_view_controller, 1);
        return true;
    }

    if (event == ftxui::Event::PageUp)
    {
        move_selection(processed_sources, text_view_controller, -page_step);
        return true;
    }

    if (event == ftxui::Event::PageDown)
    {
        move_selection(processed_sources, text_view_controller, page_step);
        return true;
    }

    if (event == ftxui::Event::Home)
    {
        set_selected_line(processed_sources, text_view_controller, 0, true);
        return true;
    }

    if (event == ftxui::Event::End)
    {
        set_selected_line(processed_sources, text_view_controller, processed_sources.line_count() - 1, true);
        return true;
    }

    if (event.is_mouse())
    {
        if (event.mouse().button == ftxui::Mouse::WheelUp)
        {
            move_selection(processed_sources, text_view_controller, -1);
            return true;
        }

        if (event.mouse().button == ftxui::Mouse::WheelDown)
        {
            move_selection(processed_sources, text_view_controller, 1);
            return true;
        }

        if (text_position_at && event.mouse().button == ftxui::Mouse::Left && event.mouse().motion == ftxui::Mouse::Pressed)
        {
            const auto position = text_position_at(event.mouse().x, event.mouse().y);
            if (position.has_value())
            {
                set_selected_line(processed_sources, text_view_controller, position->line_index, true);
                return true;
            }
        }
    }

    const int fast_horizontal_step = std::max(1, (text_view_controller.viewport_col_count() - 1) / 2);
    if (event == ftxui::Event::ArrowLeft)
    {
        text_view_controller.scroll_left(1);
        return true;
    }

    if (event == ftxui::Event::ArrowLeftCtrl)
    {
        text_view_controller.scroll_left(fast_horizontal_step);
        return true;
    }

    if (event == ftxui::Event::ArrowRight)
    {
        text_view_controller.scroll_right(1);
        return true;
    }

    if (event == ftxui::Event::ArrowRightCtrl)
    {
        text_view_controller.scroll_right(fast_horizontal_step);
        return true;
    }

    if (event == ftxui::Event::C)
    {
        return text_view_controller.copy_selection_to_clipboard();
    }

    if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Right && event.mouse().motion == ftxui::Mouse::Pressed)
    {
        return text_view_controller.copy_selection_to_clipboard();
    }

    return false;
}

void TimeAlignmentController::reset()
{
    _phase = Phase::Inactive;
    _selected_line.reset();
    _source.reset();
    _status.clear();
    _status_is_error = false;
    _apply_callback  = {};
}

void TimeAlignmentController::set_status(std::string message, bool is_error)
{
    _status          = std::move(message);
    _status_is_error = is_error;
}

void TimeAlignmentController::set_selected_line(const AllProcessedSources& processed_sources, TextViewController& text_view_controller, int visible_line_index, bool keep_visible)
{
    if (processed_sources.line_count() <= 0)
    {
        _selected_line.reset();
        return;
    }

    _selected_line = std::clamp(visible_line_index, 0, processed_sources.line_count() - 1);
    if (keep_visible)
    {
        text_view_controller.center_on_line(*_selected_line);
    }
}

void TimeAlignmentController::move_selection(const AllProcessedSources& processed_sources, TextViewController& text_view_controller, int delta)
{
    const int current_line = _selected_line.value_or(text_view_controller.first_visible_line());
    set_selected_line(processed_sources, text_view_controller, current_line + delta, true);
}

bool TimeAlignmentController::confirm_selection(AllProcessedSources& processed_sources)
{
    const LogEntry* entry = selected_entry(processed_sources);
    if (entry == nullptr)
    {
        set_status("Select a visible log entry, not a collapsed summary row", true);
        return true;
    }

    if (_phase == Phase::SelectSource)
    {
        if (!entry->metadata.timestamp.has_value())
        {
            set_status("Source line has no original timestamp", true);
            return true;
        }

        _source = SourceSelection {
            entry->metadata.source_index,
            entry->metadata.source_label,
            *entry->metadata.timestamp,
        };
        _phase = Phase::SelectDestination;
        set_status("Source " + entry->metadata.source_label + " selected, select destination line");
        return true;
    }

    if (!_source.has_value())
    {
        set_status("No source line selected", true);
        _phase = Phase::SelectSource;
        return true;
    }

    const auto destination_timestamp = effective_timestamp(entry->metadata);
    if (!destination_timestamp.has_value())
    {
        set_status("Destination line has no timestamp", true);
        return true;
    }

    if (entry->metadata.source_index == _source->source_index)
    {
        set_status("Destination must be from a different source", true);
        return true;
    }

    if (!_apply_callback)
    {
        set_status("Time alignment handler is unavailable", true);
        return true;
    }

    LogEntry source_entry;
    source_entry.metadata.timestamp    = _source->timestamp;
    source_entry.metadata.source_index = _source->source_index;
    source_entry.metadata.source_label = _source->source_label;

    const TimeAlignmentApplyResult result = _apply_callback(source_entry, *entry);
    if (!result.success)
    {
        set_status(result.message, true);
        return true;
    }

    reset();
    return true;
}

const LogEntry* TimeAlignmentController::selected_entry(const AllProcessedSources& processed_sources) const
{
    if (!_selected_line.has_value())
    {
        return nullptr;
    }

    const auto entry_index = processed_sources.entry_index_for_visible_line(VisibleLineIndex {*_selected_line});
    if (!entry_index.has_value())
    {
        return nullptr;
    }

    return &processed_sources.entry_at(*entry_index);
}

} // namespace slayerlog
