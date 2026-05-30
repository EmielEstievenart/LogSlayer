#include "log_view_component.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace slayerlog
{

namespace
{

std::string trim_text(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

std::string selected_find_text(const LogController& log_controller)
{
    std::string selection = trim_text(log_controller.selection_text());
    if (selection.find_first_of("\r\n") != std::string::npos)
    {
        return {};
    }

    return selection;
}

} // namespace

LogViewComponent::LogViewComponent(AllProcessedSources& processed_sources, LogController& controller, CommandPaletteController& command_palette_controller, ftxui::ScreenInteractive& screen, const std::string& header_text,
                                   std::mutex& model_mutex)
    : _processed_sources(processed_sources), _controller(controller), _command_palette_controller(command_palette_controller), _screen(screen), _header_text(header_text), _model_mutex(model_mutex)
{
}

ftxui::Element LogViewComponent::OnRender()
{
    std::lock_guard lock(_model_mutex);

    const bool focused   = Focused();
    ftxui::Element panel = _log_view.render(_processed_sources, _controller, _header_text, _screen.dimy(), _command_palette_controller.model().hidden_column_preview, focused);

    if (!focused)
    {
        panel = std::move(panel) | ftxui::dim;
    }

    return std::move(panel) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, std::max(1, _screen.dimx() / 2)) | ftxui::reflect(_box);
}

bool LogViewComponent::OnEvent(ftxui::Event event)
{
    std::lock_guard lock(_model_mutex);

    _exit_requested = false;

    // Mouse events are delivered to every sibling in order; only act on the ones
    // that land inside our panel and let others fall through to the right view.
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
    }

    if (event == ftxui::Event::CtrlP)
    {
        _command_palette_controller.open();
        return true;
    }

    if (event == ftxui::Event::CtrlF)
    {
        std::string query           = "find ";
        const std::string selection = selected_find_text(_controller);
        if (!selection.empty())
        {
            query += selection;
        }

        _command_palette_controller.open_with_query(std::move(query));
        return true;
    }

    if (event == ftxui::Event::CtrlR)
    {
        _command_palette_controller.open_history();
        return true;
    }

    const auto result = _log_view.handle_event(_processed_sources, _controller, event);

    if (result.request_exit)
    {
        _exit_requested = true;
        _screen.Exit();
    }

    return result.handled;
}

bool LogViewComponent::Focusable() const
{
    return true;
}

bool LogViewComponent::exit_requested() const
{
    return _exit_requested;
}

} // namespace slayerlog
