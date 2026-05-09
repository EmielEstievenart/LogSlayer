#include "command_widgets/single_select_list.hpp"

#include <algorithm>
#include <utility>

#include "view_theme.hpp"

namespace slayerlog
{

SingleSelectList::SingleSelectList(std::vector<std::string> labels) : _labels(std::move(labels)) { }

bool SingleSelectList::handle_event(const ftxui::Event& event)
{
    if (_labels.empty())
    {
        return event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown || event == ftxui::Event::Return || event == ftxui::Event::Escape;
    }

    if (event == ftxui::Event::ArrowUp)
    {
        if (_selected_index > 0)
        {
            --_selected_index;
            ensure_selection_visible();
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown)
    {
        if (_selected_index + 1 < _labels.size())
        {
            ++_selected_index;
            ensure_selection_visible();
        }
        return true;
    }

    return event == ftxui::Event::Return || event == ftxui::Event::Escape;
}

ftxui::Element SingleSelectList::render()
{
    if (_labels.empty())
    {
        return ftxui::text("No entries") | ftxui::color(theme::muted);
    }

    ensure_selection_visible();

    ftxui::Elements rows;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(1, _viewport_height));
    const std::size_t end_index     = std::min(_labels.size(), _first_visible_index + visible_count);
    for (std::size_t index = _first_visible_index; index < end_index; ++index)
    {
        ftxui::Element row = ftxui::text(_labels[index]);
        if (index == _selected_index)
        {
            row = row | ftxui::inverted;
        }
        rows.push_back(std::move(row));
    }

    return ftxui::vbox(std::move(rows));
}

std::optional<std::size_t> SingleSelectList::selected_index() const
{
    if (_labels.empty())
    {
        return std::nullopt;
    }

    return _selected_index;
}

void SingleSelectList::set_viewport_size(int width, int height)
{
    _viewport_width  = std::max(1, width);
    _viewport_height = std::max(1, height);
    ensure_selection_visible();
}

void SingleSelectList::ensure_selection_visible()
{
    if (_labels.empty())
    {
        _first_visible_index = 0;
        return;
    }

    const std::size_t visible_count = static_cast<std::size_t>(std::max(1, _viewport_height));
    if (_selected_index < _first_visible_index)
    {
        _first_visible_index = _selected_index;
    }
    else if (_selected_index >= _first_visible_index + visible_count)
    {
        _first_visible_index = _selected_index - visible_count + 1;
    }
}

} // namespace slayerlog
