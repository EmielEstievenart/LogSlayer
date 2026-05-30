#include "log_view2_component.hpp"

#include <algorithm>
#include <utility>

#include <ftxui/dom/canvas.hpp>

#include "view_theme.hpp"

namespace slayerlog
{

LogView2Component::LogView2Component() : _lines({"dummy line 1: startup complete", "dummy line 2: connected to source", "dummy line 3: received event", "dummy line 4: parsed payload", "dummy line 5: waiting for input"})
{
    int max_line_width = 0;
    for (const auto& line : _lines)
    {
        max_line_width = std::max(max_line_width, static_cast<int>(line.size()));
    }

    TextViewComponentOption option;
    option.total_line_count  = static_cast<int>(_lines.size());
    option.widest_line_width = max_line_width;
    option.draw_content      = [this](ftxui::Canvas& canvas, int first_line, int line_count, int first_col, int col_count)
    {
        const int visible_line_count = std::max(0, std::min(line_count, static_cast<int>(_lines.size()) - first_line));
        for (int row = 0; row < visible_line_count; ++row)
        {
            const auto& line = _lines[static_cast<std::size_t>(first_line + row)];
            if (first_col >= static_cast<int>(line.size()))
            {
                continue;
            }

            const auto count = static_cast<std::size_t>(std::min(col_count, static_cast<int>(line.size()) - first_col));
            canvas.DrawText(0, row * 4, line.substr(static_cast<std::size_t>(first_col), count));
        }
    };

    _text_view = std::make_shared<TextViewComponent>(std::move(option));
}

ftxui::Element LogView2Component::OnRender()
{
    const bool focused  = Focused();
    ftxui::Element body = _text_view->Render() | ftxui::flex;

    ftxui::Element title           = focused ? (ftxui::text("LogView2") | ftxui::bold | ftxui::color(theme::active_view_fg)) : ftxui::text("LogView2");
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
    // Mouse events reach every sibling: claim only the ones inside our panel and
    // take focus on a click, letting clicks elsewhere fall through to the left view.
    if (event.is_mouse())
    {
        if (!_box.Contain(event.mouse().x, event.mouse().y))
        {
            return false;
        }

        if (event.mouse().motion == ftxui::Mouse::Pressed)
        {
            TakeFocus();
        }

        return true;
    }

    // Let the container handle focus navigation; otherwise capture keyboard input.
    if (event == ftxui::Event::Tab || event == ftxui::Event::TabReverse)
    {
        return false;
    }

    return true;
}

bool LogView2Component::Focusable() const
{
    return true;
}

} // namespace slayerlog
