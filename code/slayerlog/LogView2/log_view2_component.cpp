#include "log_view2_component.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include <ftxui/dom/canvas.hpp>

#include "clipboard.hpp"
#include "view_theme.hpp"

namespace slayerlog
{

namespace
{

// Apply selection decorations onto the already-drawn canvas, mapping each
// decoration's model-space columns into the visible viewport.
void draw_selection(ftxui::Canvas& canvas, const std::vector<TextViewRangeDecoration>& decorations, int first_line, int line_count, int first_col, int col_count)
{
    for (const auto& decoration : decorations)
    {
        const int row = decoration.line_index - first_line;
        if (row < 0 || row >= line_count)
        {
            continue;
        }

        const int start = std::max(0, decoration.col_start - first_col);
        const int end   = std::min(col_count, decoration.col_end - first_col);
        for (int col = start; col < end; ++col)
        {
            canvas.Style(col * 2, row * 4, [](ftxui::Cell& cell) { cell.inverted = true; });
        }
    }
}

void color_line(ftxui::Canvas& canvas, int row, int col_start, int col_end, ftxui::Color foreground, ftxui::Color background)
{
    for (int col = col_start; col < col_end; ++col)
    {
        canvas.Style(col * 2, row * 4,
                     [foreground, background](ftxui::Cell& cell)
                     {
                         cell.foreground_color = foreground;
                         cell.background_color = background;
                     });
    }
}

} // namespace

LogView2Component::LogView2Component(std::string title, std::shared_ptr<LogView2Data> data, CommandPaletteController& command_palette_controller)
    : _title(std::move(title)), _data(std::move(data)), _find_manager(_data), _command_palette_controller(command_palette_controller)
{
    TextViewComponentOption option;
    option.draw_content = [this](ftxui::Canvas& canvas, int first_line, int line_count, int first_col, int col_count)
    {
        if (_data == nullptr)
        {
            return;
        }

        auto lock                    = _data->lock();
        const int visible_line_count = std::max(0, std::min(line_count, static_cast<int>(_data->size()) - first_line));
        for (int row = 0; row < visible_line_count; ++row)
        {
            const auto line       = _data->to_string(static_cast<std::size_t>(first_line + row));
            const auto line_index = static_cast<std::size_t>(first_line + row);
            if (first_col >= static_cast<int>(line.size()))
            {
                continue;
            }

            const auto count = static_cast<std::size_t>(std::min(col_count, static_cast<int>(line.size()) - first_col));
            canvas.DrawText(0, row * 4, line.substr(static_cast<std::size_t>(first_col), count));
            if (_find_manager.line_matches(line_index))
            {
                const bool is_active = _find_manager.line_is_active_match(line_index);
                color_line(canvas, row, 0, static_cast<int>(count), is_active ? theme::find_active_fg : theme::find_match_fg, is_active ? theme::find_active_bg : theme::find_match_bg);
            }
        }

        draw_selection(canvas, _selection.decorations(*_data), first_line, visible_line_count, first_col, col_count);
    };

    _text_view = std::make_shared<TextViewComponent>(std::move(option));
}

LogView2FindManager& LogView2Component::find_manager()
{
    return _find_manager;
}

ftxui::Element LogView2Component::OnRender()
{
    if (_data != nullptr)
    {
        auto lock = _data->lock();
        _text_view->update_content_size(static_cast<int>(_data->size()), _data->widest_line_width());
        const auto pending_focus_line = _find_manager.consume_pending_focus_line();
        if (pending_focus_line.has_value())
        {
            _text_view->controller().center_on_line(static_cast<int>(*pending_focus_line));
        }
    }

    const bool focused  = Focused();
    ftxui::Element body = _text_view->Render() | ftxui::flex;

    ftxui::Element title           = focused ? (ftxui::text(_title) | ftxui::bold | ftxui::color(theme::active_view_fg)) : ftxui::text(_title);
    const ftxui::BorderStyle frame = focused ? ftxui::DOUBLE : ftxui::LIGHT;

    ftxui::Element panel = ftxui::window(std::move(title), std::move(body), frame);
    if (!focused)
    {
        panel = std::move(panel) | ftxui::dim;
    }

    return std::move(panel) | ftxui::flex | ftxui::reflect(_box);
}

bool LogView2Component::OnEvent(ftxui::Event event)
{
    if (event.is_mouse())
    {
        return handle_mouse(event.mouse());
    }

    // Let the container handle focus navigation; otherwise capture keyboard input.
    if (event == ftxui::Event::Tab || event == ftxui::Event::TabReverse)
    {
        return false;
    }

    if (event == ftxui::Event::C)
    {
        copy_selection();
        return true;
    }

    if (event == ftxui::Event::CtrlP)
    {
        _command_palette_controller.open();
        return true;
    }

    if (event == ftxui::Event::CtrlR)
    {
        _command_palette_controller.open_history();
        return true;
    }

    if (event == ftxui::Event::CtrlO)
    {
        _command_palette_controller.open_history_with_query("open");
        return true;
    }

    if (_data != nullptr && event == ftxui::Event::Escape)
    {
        auto lock = _data->lock();
        if (!_find_manager.active())
        {
            return true;
        }

        _find_manager.clear();
        return true;
    }

    if (_data != nullptr && event == ftxui::Event::ArrowRight)
    {
        auto lock = _data->lock();
        if (_find_manager.active())
        {
            return _find_manager.go_to_next_match();
        }
    }

    if (_data != nullptr && event == ftxui::Event::ArrowLeft)
    {
        auto lock = _data->lock();
        if (_find_manager.active())
        {
            return _find_manager.go_to_previous_match();
        }
    }

    const int fast_horizontal_step = std::max(1, (_text_view->controller().viewport_col_count() - 1) / 2);

    if (event == ftxui::Event::ArrowUp)
    {
        _text_view->user_scroll_up();
    }
    else if (event == ftxui::Event::ArrowDown)
    {
        _text_view->user_scroll_down();
    }
    else if (event == ftxui::Event::PageUp)
    {
        _text_view->user_page_up();
    }
    else if (event == ftxui::Event::PageDown)
    {
        _text_view->user_page_down();
    }
    else if (event == ftxui::Event::Home)
    {
        _text_view->user_scroll_to_top();
    }
    else if (event == ftxui::Event::End)
    {
        _text_view->user_scroll_to_bottom();
    }
    else if (event == ftxui::Event::ArrowLeft)
    {
        _text_view->user_scroll_left();
    }
    else if (event == ftxui::Event::ArrowRight)
    {
        _text_view->user_scroll_right();
    }
    else if (event == ftxui::Event::ArrowLeftCtrl)
    {
        _text_view->user_scroll_left(fast_horizontal_step);
    }
    else if (event == ftxui::Event::ArrowRightCtrl)
    {
        _text_view->user_scroll_right(fast_horizontal_step);
    }

    return true;
}

bool LogView2Component::Focusable() const
{
    return true;
}

bool LogView2Component::handle_mouse(const ftxui::Mouse& mouse)
{
    // Mouse events reach every sibling: claim only the ones inside our panel and
    // take focus on a click, letting clicks elsewhere fall through to the left view.
    if (!_box.Contain(mouse.x, mouse.y))
    {
        return false;
    }

    if (mouse.motion == ftxui::Mouse::Pressed)
    {
        TakeFocus();
    }

    if (mouse.button == ftxui::Mouse::WheelUp)
    {
        _text_view->user_scroll_up();
        return true;
    }

    if (mouse.button == ftxui::Mouse::WheelDown)
    {
        _text_view->user_scroll_down();
        return true;
    }

    if (mouse.button == ftxui::Mouse::Left)
    {
        return handle_left_button(mouse);
    }

    if (mouse.button == ftxui::Mouse::Right && mouse.motion == ftxui::Mouse::Pressed)
    {
        copy_selection();
        return true;
    }

    return true;
}

bool LogView2Component::handle_left_button(const ftxui::Mouse& mouse)
{
    if (_data == nullptr)
    {
        return true;
    }

    const auto position = _text_view->text_position_at(mouse.x, mouse.y);
    auto lock           = _data->lock();

    if (mouse.motion == ftxui::Mouse::Pressed)
    {
        if (position.has_value())
        {
            _selection.begin(*position, *_data);
        }
        else
        {
            _selection.clear();
        }
    }
    else if (mouse.motion == ftxui::Mouse::Moved && _selection.in_progress())
    {
        if (position.has_value())
        {
            _selection.update(*position, *_data);
        }
    }
    else if (mouse.motion == ftxui::Mouse::Released)
    {
        _selection.end(position, *_data);
    }

    return true;
}

void LogView2Component::copy_selection()
{
    if (_data == nullptr)
    {
        return;
    }

    auto lock = _data->lock();
    CopyTextToClipboard(_selection.text(*_data));
}

} // namespace slayerlog
