#include "log_view2_component.hpp"

#include <utility>

#include "view_theme.hpp"

namespace slayerlog
{

ftxui::Element LogView2Component::OnRender()
{
    const bool focused  = Focused();
    ftxui::Element body = ftxui::vbox({ftxui::filler()}) | ftxui::flex;

    ftxui::Element title          = focused ? (ftxui::text("LogView2") | ftxui::bold | ftxui::color(theme::active_view_fg)) : ftxui::text("LogView2");
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
