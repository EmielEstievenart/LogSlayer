#include "wx_log_view_service.hpp"

#include <utility>

#include "debug_log.hpp"
#include "log_view2_find_manager.hpp"
#include "model_refresh.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "wx_log_view.hpp"

namespace slayerlog
{

WxLogViewService::WxLogViewService(WxLogView& view, RedrawScheduler& redraw_scheduler) : _view(view), _redraw_scheduler(redraw_scheduler)
{
}

void WxLogViewService::rebuild_view(const AllProcessedSources& /*processed_sources*/)
{
    // The wx view paints straight from the processed sources every frame, so a
    // refresh is just a redraw request.
    _redraw_scheduler.request_redraw();
}

void WxLogViewService::reload(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources)
{
    reload_processed_sources(tracked_sources, processed_sources, _redraw_scheduler);
}

bool WxLogViewService::go_to_line(const AllProcessedSources& processed_sources, int line_number)
{
    const auto visible_line = processed_sources.visible_line_index_for_line_number(line_number);
    if (!visible_line.has_value())
    {
        return false;
    }

    // Commands run under model_mutex; scrolling synchronously here would force
    // a repaint that re-locks the same mutex. Defer to on_model_updated, which
    // the command-executed resync drives outside the lock.
    _view.request_center_on_row(static_cast<std::size_t>(visible_line->value));
    _redraw_scheduler.request_redraw();
    return true;
}

int WxLogViewService::first_visible_line() const
{
    return _view.first_visible_row();
}

int WxLogViewService::viewport_line_count() const
{
    return _view.viewport_row_count();
}

bool WxLogViewService::set_find_query(AllProcessedSources& /*processed_sources*/, std::string query)
{
    const bool focused = _view.find_manager().set_query(std::move(query));
    _redraw_scheduler.request_redraw();
    return focused;
}

int WxLogViewService::total_find_match_count() const
{
    return static_cast<int>(_view.find_manager().match_count());
}

int WxLogViewService::visible_find_match_count(const AllProcessedSources& /*processed_sources*/) const
{
    // The wx view renders the only processed-sources model, so every match is visible.
    return static_cast<int>(_view.find_manager().match_count());
}

const std::string& WxLogViewService::find_query() const
{
    return _view.find_manager().query();
}

void WxLogViewService::start_time_alignment(TimeAlignmentApplyCallback /*apply*/)
{
    SLAYERLOG_LOG_WARNING("start_time_alignment is not implemented for the wx UI yet (docs/wx-ui-plan.md, M6)");
}

void WxLogViewService::begin_time_alignment(std::size_t /*aligning_source_index*/)
{
    SLAYERLOG_LOG_WARNING("begin_time_alignment is not implemented for the wx UI yet (docs/wx-ui-plan.md, M6)");
}

} // namespace slayerlog
