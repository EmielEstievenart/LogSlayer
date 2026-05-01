#include "master_controller.hpp"

#include <cctype>
#include <string>
#include <string_view>

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
    std::string selection = trim_text(log_controller.text_view_controller().selection_text());
    if (selection.find_first_of("\r\n") != std::string::npos)
    {
        return {};
    }

    return selection;
}

} // namespace

MasterController::MasterController(AllProcessedSources& processed_sources, LogController& log_controller, LogView& log_view, ftxui::ScreenInteractive& screen, CommandPaletteController& command_palette_controller)
    : _processed_sources(processed_sources), _log_controller(log_controller), _log_view(log_view), _screen(screen), _command_palette_controller(command_palette_controller)
{
}

bool MasterController::handle_event(const ftxui::Event& event)
{
    _exit_requested = false;

    if (event == ftxui::Event::CtrlP)
    {
        _command_palette_controller.open();
        return true;
    }

    if (event == ftxui::Event::CtrlF)
    {
        std::string query           = "find ";
        const std::string selection = selected_find_text(_log_controller);
        if (!selection.empty())
        {
            query += selection;
        }

        _command_palette_controller.open_with_query(std::move(query));
        return true;
    }

    if (_command_palette_controller.is_open())
    {
        return _command_palette_controller.handle_event(event);
    }

    if (event == ftxui::Event::CtrlR)
    {
        _command_palette_controller.open_history();
        return true;
    }

    const auto result = _log_controller.handle_event(_processed_sources, event, [this](const ftxui::Mouse& mouse) { return _log_view.mouse_to_text_position(_log_controller, mouse); });

    if (result.request_exit)
    {
        _exit_requested = true;
        _screen.Exit();
    }

    return result.handled;
}

bool MasterController::exit_requested() const
{
    return _exit_requested;
}

} // namespace slayerlog
