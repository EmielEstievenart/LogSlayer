#include "command_widgets/multi_select_list.hpp"

#include <algorithm>
#include <utility>

#include "view_theme.hpp"

namespace slayerlog
{

MultiSelectList::MultiSelectList(std::vector<std::string> labels) : _labels(std::move(labels)) { }

bool MultiSelectList::handle_event(const ftxui::Event& event)
{
    if (_labels.empty())
    {
        return event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown || event == ftxui::Event::Character(" ") || event == ftxui::Event::Return || event == ftxui::Event::Escape;
    }

    if (event == ftxui::Event::ArrowUp)
    {
        if (_focused_index > 0)
        {
            --_focused_index;
            ensure_focus_visible();
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown)
    {
        if (_focused_index + 1 < _labels.size())
        {
            ++_focused_index;
            ensure_focus_visible();
        }
        return true;
    }

    if (event == ftxui::Event::Character(" "))
    {
        if (_selected_indices.erase(_focused_index) == 0)
        {
            _selected_indices.insert(_focused_index);
        }
        return true;
    }

    return event == ftxui::Event::Return || event == ftxui::Event::Escape;
}

ftxui::Element MultiSelectList::render()
{
    if (_labels.empty())
    {
        return ftxui::text("No entries") | ftxui::color(theme::muted);
    }

    ensure_focus_visible();

    ftxui::Elements rows;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(1, _viewport_height));
    const std::size_t end_index     = std::min(_labels.size(), _first_visible_index + visible_count);
    for (std::size_t index = _first_visible_index; index < end_index; ++index)
    {
        const std::string prefix = _selected_indices.count(index) == 0 ? "[ ] " : "[x] ";
        ftxui::Element row       = ftxui::text(prefix + _labels[index]);
        if (index == _focused_index)
        {
            row = row | ftxui::inverted;
        }
        rows.push_back(std::move(row));
    }

    return ftxui::vbox(std::move(rows));
}

std::vector<std::size_t> MultiSelectList::selected_indices() const
{
    return {_selected_indices.begin(), _selected_indices.end()};
}

void MultiSelectList::set_viewport_size(int width, int height)
{
    _viewport_width  = std::max(1, width);
    _viewport_height = std::max(1, height);
    ensure_focus_visible();
}

void MultiSelectList::ensure_focus_visible()
{
    if (_labels.empty())
    {
        _first_visible_index = 0;
        return;
    }

    const std::size_t visible_count = static_cast<std::size_t>(std::max(1, _viewport_height));
    if (_focused_index < _first_visible_index)
    {
        _first_visible_index = _focused_index;
    }
    else if (_focused_index >= _first_visible_index + visible_count)
    {
        _first_visible_index = _focused_index - visible_count + 1;
    }
}

} // namespace slayerlog
