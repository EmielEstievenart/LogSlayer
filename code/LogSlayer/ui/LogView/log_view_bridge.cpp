#include "log_view_bridge.hpp"

#include <ftxui/component/event.hpp>

#include "LogView/align_time_controller.hpp"
#include "command_support.hpp"
#include "debug_log.hpp"
#include "log_view_component.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

LogViewBridge::LogViewBridge(LogViewComponent& view, std::string& header_text, ftxui::ScreenInteractive& screen) : _view(view), _header_text(header_text), _screen(screen)
{
}

void LogViewBridge::set_align_controller(AlignTimeController* align_controller)
{
    _align_controller = align_controller;
}

void LogViewBridge::rebuild_view(const AllProcessedSources& /*processed_sources*/)
{
    // LogView renders straight from the processed sources every frame, so a
    // refresh is just a redraw request.
    _screen.PostEvent(ftxui::Event::Custom);
}

void LogViewBridge::reload(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources)
{
    reload_processed_sources(tracked_sources, _header_text, processed_sources, _screen);
}

bool LogViewBridge::go_to_line(const AllProcessedSources& processed_sources, int line_number)
{
    const auto visible_line = processed_sources.visible_line_index_for_line_number(line_number);
    if (!visible_line.has_value())
    {
        return false;
    }

    _view.text_view_controller().center_on_line(visible_line->value);
    _screen.PostEvent(ftxui::Event::Custom);
    return true;
}

int LogViewBridge::first_visible_line() const
{
    return _view.text_view_controller().first_visible_line();
}

int LogViewBridge::viewport_line_count() const
{
    return _view.text_view_controller().viewport_line_count();
}

void LogViewBridge::begin_time_alignment(std::size_t aligning_source_index)
{
    if (_align_controller == nullptr)
    {
        SLAYERLOG_LOG_WARNING("begin_time_alignment called before the align controller was wired");
        return;
    }

    _align_controller->begin(aligning_source_index);
}

} // namespace slayerlog
