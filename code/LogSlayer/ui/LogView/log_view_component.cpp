#include "log_view_component.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/dom/canvas.hpp>

#include "clipboard.hpp"
#include "log_types.hpp"
#include "log_view_utils.hpp"
#include "view_theme.hpp"

namespace slayerlog
{

namespace
{

// A frame-local snapshot of the view-model state shown in the status bar,
// read once under the data lock so the status rows can be built afterwards.
struct StatusSnapshot
{
    bool paused = false;
    std::vector<std::string> include_filters;
    std::vector<std::string> exclude_filters;
    std::optional<int> hidden_before;
    std::optional<HiddenColumnRange> hidden_columns;

    bool find_active = false;
    std::string find_query;
    std::size_t find_match_count = 0;
    std::optional<std::size_t> find_active_line;
};

std::string join(const std::vector<std::string>& items)
{
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
        {
            result += ", ";
        }
        result += items[i];
    }
    return result;
}

ftxui::Element build_filter_status(const StatusSnapshot& status)
{
    ftxui::Elements parts;
    parts.push_back(theme::badge("FILTER", theme::label_filter_fg));

    if (status.include_filters.empty() && status.exclude_filters.empty() && !status.hidden_before.has_value() && !status.hidden_columns.has_value())
    {
        parts.push_back(ftxui::text(" none") | ftxui::color(theme::muted));
        return ftxui::hbox(std::move(parts));
    }

    if (!status.include_filters.empty())
    {
        parts.push_back(ftxui::text(" in(" + join(status.include_filters) + ")"));
    }

    if (!status.exclude_filters.empty())
    {
        parts.push_back(ftxui::text(" out(" + join(status.exclude_filters) + ")"));
    }

    if (status.hidden_before.has_value())
    {
        parts.push_back(ftxui::text(" | before line " + std::to_string(*status.hidden_before)) | ftxui::color(theme::muted));
    }

    if (status.hidden_columns.has_value())
    {
        parts.push_back(ftxui::text(" | columns " + std::to_string(status.hidden_columns->start) + "-" + std::to_string(status.hidden_columns->end)) | ftxui::color(theme::muted));
    }

    return ftxui::hbox(std::move(parts));
}

ftxui::Element build_find_status(const StatusSnapshot& status)
{
    ftxui::Elements parts;
    parts.push_back(theme::badge("FIND", theme::label_find_fg));

    if (!status.find_active)
    {
        parts.push_back(ftxui::text(" off") | ftxui::color(theme::muted));
        return ftxui::hbox(std::move(parts));
    }

    parts.push_back(ftxui::text(" \"" + status.find_query + "\""));
    parts.push_back(ftxui::text(" " + std::to_string(status.find_match_count) + " matches") | ftxui::color(theme::muted));

    if (status.find_active_line.has_value())
    {
        parts.push_back(ftxui::text(" | line " + std::to_string(*status.find_active_line + 1)) | ftxui::color(theme::muted));
    }

    return ftxui::hbox(std::move(parts));
}

ftxui::Element build_key_hints()
{
    auto sep = []() { return ftxui::text("  "); };
    return ftxui::hbox({
        theme::key_hint("Ctrl+P", "commands"),
        sep(),
        theme::key_hint("Ctrl+F", "find"),
        sep(),
        theme::key_hint("Ctrl+R", "history"),
        sep(),
        theme::key_hint("\xe2\x86\x92", "next"),
        sep(),
        theme::key_hint("\xe2\x86\x90", "prev"),
        sep(),
        theme::key_hint("Esc", "close find"),
        sep(),
        theme::key_hint("q", "quit"),
    });
}

} // namespace

LogViewComponent::LogViewComponent(std::string title, std::shared_ptr<LogViewData> data, CommandPaletteController& command_palette_controller, std::function<void()> on_exit)
    : _title(std::move(title)), _data(std::move(data)), _find_manager(_data), _command_palette_controller(command_palette_controller), _on_exit(std::move(on_exit))
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

LogViewFindManager& LogViewComponent::find_manager()
{
    return _find_manager;
}

TextViewController& LogViewComponent::text_view_controller()
{
    return _text_view->controller();
}

ftxui::Element LogViewComponent::OnRender()
{
    StatusSnapshot status;
    if (_data != nullptr)
    {
        auto lock = _data->lock();
        _text_view->update_content_size(static_cast<int>(_data->size()), _data->widest_line_width());
        const auto pending_focus_line = _find_manager.consume_pending_focus_line();
        if (pending_focus_line.has_value())
        {
            _text_view->controller().center_on_line(static_cast<int>(*pending_focus_line));
        }

        status.paused          = _data->updates_paused();
        status.include_filters = _data->include_filters();
        status.exclude_filters = _data->exclude_filters();
        status.hidden_before   = _data->hidden_before_line();
        status.hidden_columns  = _data->hidden_columns();
        status.find_active     = _find_manager.active();
        if (status.find_active)
        {
            status.find_query       = _find_manager.query();
            status.find_match_count = _find_manager.match_count();
            status.find_active_line = _find_manager.active_match_line();
        }
    }

    const bool focused  = Focused();
    ftxui::Element body = _text_view->Render() | ftxui::flex;

    ftxui::Element title_text      = focused ? (ftxui::text(_title) | ftxui::bold | ftxui::color(theme::active_view_fg)) : ftxui::text(_title);
    ftxui::Element title           = status.paused ? ftxui::hbox({std::move(title_text), ftxui::text(" "), theme::badge("PAUSED", theme::paused_fg)}) : std::move(title_text);
    const ftxui::BorderStyle frame = focused ? ftxui::DOUBLE : ftxui::LIGHT;

    ftxui::Element content = ftxui::vbox({
        std::move(body),
        ftxui::separator(),
        build_filter_status(status),
        build_find_status(status),
        build_key_hints(),
    });

    ftxui::Element panel = ftxui::window(std::move(title), std::move(content), frame);
    if (!focused)
    {
        panel = std::move(panel) | ftxui::dim;
    }

    return std::move(panel) | ftxui::flex | ftxui::reflect(_box);
}

bool LogViewComponent::OnEvent(ftxui::Event event)
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

    if (event == ftxui::Event::CtrlF)
    {
        std::string query = "find ";
        if (_data != nullptr)
        {
            auto lock             = _data->lock();
            const std::string sel = selected_find_text(_selection, *_data);
            if (!sel.empty())
            {
                query += sel;
            }
        }
        _command_palette_controller.open_with_query(std::move(query));
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

    if (event == ftxui::Event::Character("q"))
    {
        if (_on_exit)
        {
            _on_exit();
        }
        return true;
    }

    if (_data != nullptr && event == ftxui::Event::Escape)
    {
        {
            auto lock = _data->lock();
            if (_find_manager.active())
            {
                _find_manager.clear();
                return true;
            }
        }

        if (_on_exit)
        {
            _on_exit();
        }
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

bool LogViewComponent::Focusable() const
{
    return true;
}

bool LogViewComponent::handle_mouse(const ftxui::Mouse& mouse)
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

bool LogViewComponent::handle_left_button(const ftxui::Mouse& mouse)
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

void LogViewComponent::copy_selection()
{
    if (_data == nullptr)
    {
        return;
    }

    auto lock = _data->lock();
    CopyTextToClipboard(_selection.text(*_data));
}

} // namespace slayerlog
