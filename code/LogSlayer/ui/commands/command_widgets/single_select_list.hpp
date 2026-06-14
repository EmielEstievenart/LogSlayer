#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

namespace slayerlog
{

class SingleSelectList
{
public:
    explicit SingleSelectList(std::vector<std::string> labels);

    bool handle_event(const ftxui::Event& event);
    ftxui::Element render();

    std::optional<std::size_t> selected_index() const;
    void set_viewport_size(int width, int height);

private:
    void ensure_selection_visible();

    std::vector<std::string> _labels;
    std::size_t _selected_index = 0;
    std::size_t _first_visible_index = 0;
    int _viewport_width = 1;
    int _viewport_height = 1;
};

} // namespace slayerlog
