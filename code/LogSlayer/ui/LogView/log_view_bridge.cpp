#include "log_view_bridge.hpp"

#include <utility>

#include <ftxui/component/screen_interactive.hpp>

#include "command_support.hpp"
#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

LogViewBridge::LogViewBridge(LogController& controller, std::string& header_text, ftxui::ScreenInteractive& screen) : _controller(controller), _header_text(header_text), _screen(screen)
{
}

void LogViewBridge::rebuild_view(const AllProcessedSources& processed_sources)
{
    _controller.rebuild_view(processed_sources);
}

void LogViewBridge::reload(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources)
{
    reload_processed_sources(tracked_sources, _header_text, processed_sources, _controller, _screen);
}

bool LogViewBridge::go_to_line(const AllProcessedSources& processed_sources, int line_number)
{
    return _controller.go_to_line(processed_sources, line_number);
}

int LogViewBridge::first_visible_line() const
{
    return _controller.text_view_controller().first_visible_line();
}

int LogViewBridge::viewport_line_count() const
{
    return _controller.text_view_controller().viewport_line_count();
}

bool LogViewBridge::set_find_query(AllProcessedSources& processed_sources, std::string query)
{
    return _controller.set_find_query(processed_sources, std::move(query));
}

int LogViewBridge::total_find_match_count() const
{
    return _controller.total_find_match_count();
}

int LogViewBridge::visible_find_match_count(const AllProcessedSources& processed_sources) const
{
    return _controller.visible_find_match_count(processed_sources);
}

const std::string& LogViewBridge::find_query() const
{
    return _controller.find_query();
}

void LogViewBridge::start_time_alignment(TimeAlignmentApplyCallback apply)
{
    _controller.time_alignment_controller().start(_controller.text_view_controller().first_visible_line(), std::move(apply));
}

} // namespace slayerlog
