#include "command_widgets/text_input_panel.hpp"

#include <algorithm>
#include <utility>

#include "view_theme.hpp"

namespace slayerlog
{

namespace
{

ftxui::Element render_editable_input(const EditableText& input)
{
    const std::size_t cursor_position = std::min(input.cursor_position(), input.text().size());
    const std::string prefix          = input.text().substr(0, cursor_position);
    const bool cursor_at_end          = cursor_position >= input.text().size();
    const std::string cursor_text     = cursor_at_end ? " " : input.text().substr(cursor_position, 1);
    const std::string suffix          = cursor_at_end ? std::string() : input.text().substr(cursor_position + 1);

    ftxui::Elements row;
    row.push_back(ftxui::text("> ") | ftxui::bold);
    if (!prefix.empty())
    {
        row.push_back(ftxui::text(prefix));
    }
    row.push_back(ftxui::text(cursor_text) | ftxui::inverted);
    if (!suffix.empty())
    {
        row.push_back(ftxui::text(suffix));
    }

    return ftxui::hbox(std::move(row));
}

} // namespace

bool TextInputPanel::handle_event(const ftxui::Event& event)
{
    return _input.handle_event(event);
}

ftxui::Element TextInputPanel::render()
{
    ftxui::Elements rows;
    rows.push_back(render_editable_input(_input));
    if (!_preview.empty())
    {
        rows.push_back(ftxui::text(_preview) | ftxui::color(_preview_is_error ? theme::error_fg : theme::success_fg));
    }

    return ftxui::vbox(std::move(rows));
}

const std::string& TextInputPanel::text() const
{
    return _input.text();
}

void TextInputPanel::set_preview(std::string preview, bool is_error)
{
    _preview          = std::move(preview);
    _preview_is_error = is_error;
}

} // namespace slayerlog
