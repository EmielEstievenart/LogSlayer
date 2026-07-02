#pragma once

#include <cstddef>
#include <string>

#include "log_view_service.hpp"
#include "redraw_scheduler.hpp"

namespace slayerlog
{

class WxLogView;

/// Wx implementation of the core LogViewService, mirroring the TUI's
/// LogView2Bridge over the pull-based WxLogView: a refresh is just a redraw
/// request (the view repaints straight from the processed sources), go-to-line
/// scrolls the wx viewport, and find delegates to the view's
/// LogView2FindManager. Time alignment is a logged no-op until the wx
/// align-time milestone (docs/wx-ui-plan.md, M6).
class WxLogViewService final : public LogViewService
{
public:
    WxLogViewService(WxLogView& view, RedrawScheduler& redraw_scheduler);

    void rebuild_view(const AllProcessedSources& processed_sources) override;
    void reload(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources) override;
    bool go_to_line(const AllProcessedSources& processed_sources, int line_number) override;
    int first_visible_line() const override;
    int viewport_line_count() const override;
    bool set_find_query(AllProcessedSources& processed_sources, std::string query) override;
    int total_find_match_count() const override;
    int visible_find_match_count(const AllProcessedSources& processed_sources) const override;
    const std::string& find_query() const override;
    void start_time_alignment(TimeAlignmentApplyCallback apply) override;
    void begin_time_alignment(std::size_t aligning_source_index) override;

private:
    WxLogView& _view;
    RedrawScheduler& _redraw_scheduler;
};

} // namespace slayerlog
