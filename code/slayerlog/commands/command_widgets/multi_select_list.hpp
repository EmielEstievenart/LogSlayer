#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

namespace slayerlog
{

class MultiSelectList
{
public:
    explicit MultiSelectList(std::vector<std::string> labels);

    bool handle_event(const ftxui::Event& event);
    ftxui::Element render();

    std::vector<std::size_t> selected_indices() const;
    void set_viewport_size(int width, int height);

private:
    void ensure_focus_visible();

    std::vector<std::string> _labels;
    std::set<std::size_t> _selected_indices;
    std::size_t _focused_index = 0;
    std::size_t _first_visible_index = 0;
    int _viewport_width = 1;
    int _viewport_height = 1;
};

} // namespace slayerlog
