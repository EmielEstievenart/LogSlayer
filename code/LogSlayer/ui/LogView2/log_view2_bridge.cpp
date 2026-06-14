#include "log_view2_bridge.hpp"

#include <utility>

#include <ftxui/component/event.hpp>

#include "command_support.hpp"
#include "debug_log.hpp"
#include "log_view2_component.hpp"
#include "log_view2_find_manager.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

LogView2Bridge::LogView2Bridge(LogView2Component& view, std::string& header_text, ftxui::ScreenInteractive& screen) : _view(view), _header_text(header_text), _screen(screen)
{
}

void LogView2Bridge::rebuild_view(const AllProcessedSources& /*processed_sources*/)
{
    // LogView2 renders straight from the processed sources every frame, so a
    // refresh is just a redraw request.
    _screen.PostEvent(ftxui::Event::Custom);
}

void LogView2Bridge::reload(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources)
{
    reload_processed_sources(tracked_sources, _header_text, processed_sources, _screen);
}

bool LogView2Bridge::go_to_line(const AllProcessedSources& processed_sources, int line_number)
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

int LogView2Bridge::first_visible_line() const
{
    return _view.text_view_controller().first_visible_line();
}

int LogView2Bridge::viewport_line_count() const
{
    return _view.text_view_controller().viewport_line_count();
}

bool LogView2Bridge::set_find_query(AllProcessedSources& /*processed_sources*/, std::string query)
{
    const bool focused = _view.find_manager().set_query(std::move(query));
    _screen.PostEvent(ftxui::Event::Custom);
    return focused;
}

int LogView2Bridge::total_find_match_count() const
{
    return static_cast<int>(_view.find_manager().match_count());
}

int LogView2Bridge::visible_find_match_count(const AllProcessedSources& /*processed_sources*/) const
{
    // LogView2 owns the only processed-sources model, so every match is visible.
    return static_cast<int>(_view.find_manager().match_count());
}

const std::string& LogView2Bridge::find_query() const
{
    return _view.find_manager().query();
}

void LogView2Bridge::start_time_alignment(TimeAlignmentApplyCallback /*apply*/)
{
    // Not yet ported to LogView2; align-time is intentionally not registered for
    // this view, so this is never invoked in practice.
    SLAYERLOG_LOG_WARNING("start_time_alignment is not yet implemented for LogView2");
}

} // namespace slayerlog
