#include "time_alignment_controller.hpp"

#include <algorithm>
#include <utility>

namespace slayerlog
{

void TimeAlignmentController::start(int first_visible_line, ApplyCallback apply_callback)
{
    _model.start(first_visible_line, std::move(apply_callback));
}

void TimeAlignmentController::cancel()
{
    _model.cancel();
}

bool TimeAlignmentController::active() const
{
    return _model.active();
}

std::optional<int> TimeAlignmentController::selected_line() const
{
    return _model.selected_line();
}

const std::string& TimeAlignmentController::status_text() const
{
    return _model.status_text();
}

bool TimeAlignmentController::status_is_error() const
{
    return _model.status_is_error();
}

bool TimeAlignmentController::handle_event(AllProcessedSources& processed_sources, TextViewController& text_view_controller, ftxui::Event event,
                                           const std::function<std::optional<TextViewPosition>(int, int)>& text_position_at, const FindNavigation& find_navigation,
                                           const std::function<bool()>& copy_selection_to_clipboard)
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
        return copy_selection_to_clipboard ? copy_selection_to_clipboard() : false;
    }

    if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Right && event.mouse().motion == ftxui::Mouse::Pressed)
    {
        return copy_selection_to_clipboard ? copy_selection_to_clipboard() : false;
    }

    return false;
}

void TimeAlignmentController::set_selected_line(const AllProcessedSources& processed_sources, TextViewController& text_view_controller, int visible_line_index, bool keep_visible)
{
    _model.set_selected_line(processed_sources, visible_line_index);
    if (keep_visible)
    {
        if (const auto line = _model.selected_line(); line.has_value())
        {
            text_view_controller.center_on_line(*line);
        }
    }
}

void TimeAlignmentController::move_selection(const AllProcessedSources& processed_sources, TextViewController& text_view_controller, int delta)
{
    _model.move_selection(processed_sources, delta, text_view_controller.first_visible_line());
    if (const auto line = _model.selected_line(); line.has_value())
    {
        text_view_controller.center_on_line(*line);
    }
}

bool TimeAlignmentController::confirm_selection(AllProcessedSources& processed_sources)
{
    return _model.confirm_selection(processed_sources);
}

} // namespace slayerlog
