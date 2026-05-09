#pragma once

#include <cstddef>
#include <string>

#include <ftxui/component/event.hpp>

namespace slayerlog
{

class EditableText
{
public:
    const std::string& text() const;
    std::size_t cursor_position() const;

    bool handle_event(const ftxui::Event& event);

    void set_text(std::string text);
    void clear();

private:
    std::string _text;
    std::size_t _cursor_position = 0;
};

} // namespace slayerlog
