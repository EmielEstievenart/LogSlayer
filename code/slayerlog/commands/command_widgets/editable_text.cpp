#include "command_widgets/editable_text.hpp"

#include <algorithm>
#include <utility>

namespace slayerlog
{

namespace
{

bool is_utf8_continuation_byte(unsigned char value)
{
    return (value & 0xC0U) == 0x80U;
}

std::size_t previous_codepoint_start(const std::string& text, std::size_t cursor_position)
{
    if (cursor_position == 0)
    {
        return 0;
    }

    std::size_t position = cursor_position - 1;
    while (position > 0 && is_utf8_continuation_byte(static_cast<unsigned char>(text[position])))
    {
        --position;
    }

    return position;
}

std::size_t next_codepoint_end(const std::string& text, std::size_t cursor_position)
{
    if (cursor_position >= text.size())
    {
        return text.size();
    }

    std::size_t position = cursor_position + 1;
    while (position < text.size() && is_utf8_continuation_byte(static_cast<unsigned char>(text[position])))
    {
        ++position;
    }

    return position;
}

} // namespace

const std::string& EditableText::text() const
{
    return _text;
}

std::size_t EditableText::cursor_position() const
{
    return _cursor_position;
}

bool EditableText::handle_event(const ftxui::Event& event)
{
    if (event == ftxui::Event::ArrowLeft)
    {
        _cursor_position = previous_codepoint_start(_text, _cursor_position);
        return true;
    }

    if (event == ftxui::Event::ArrowRight)
    {
        _cursor_position = next_codepoint_end(_text, _cursor_position);
        return true;
    }

    if (event == ftxui::Event::Home)
    {
        _cursor_position = 0;
        return true;
    }

    if (event == ftxui::Event::End)
    {
        _cursor_position = _text.size();
        return true;
    }

    if (event == ftxui::Event::Backspace)
    {
        const std::size_t erase_start = previous_codepoint_start(_text, _cursor_position);
        if (erase_start != _cursor_position)
        {
            _text.erase(erase_start, _cursor_position - erase_start);
            _cursor_position = erase_start;
        }

        return true;
    }

    if (event == ftxui::Event::Delete)
    {
        const std::size_t erase_end = next_codepoint_end(_text, _cursor_position);
        if (erase_end != _cursor_position)
        {
            _text.erase(_cursor_position, erase_end - _cursor_position);
        }

        return true;
    }

    if (event.is_character())
    {
        const std::string typed_text = event.character();
        _text.insert(_cursor_position, typed_text);
        _cursor_position += typed_text.size();
        return true;
    }

    return false;
}

void EditableText::set_text(std::string text)
{
    _text            = std::move(text);
    _cursor_position = _text.size();
}

void EditableText::clear()
{
    _text.clear();
    _cursor_position = 0;
}

} // namespace slayerlog
