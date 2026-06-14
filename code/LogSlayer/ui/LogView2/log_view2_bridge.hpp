#pragma once

#include <cstddef>
#include <string>

#include <ftxui/component/screen_interactive.hpp>

#include "log_view_service.hpp"

namespace slayerlog
{

class LogView2Component;
class AllProcessedSources;
class AllTrackedSources;
class AlignTimeController;

/// Terminal-UI implementation of LogViewService backed by LogView2. Mirrors
/// LogViewBridge (which drives the legacy LogController), but targets the thin
/// pull-based LogView2: a refresh is just a redraw request because the view
/// renders straight from the processed sources, navigation drives the text view
/// controller, and find delegates to the view's LogView2FindManager.
class LogView2Bridge final : public LogViewService
{
public:
    LogView2Bridge(LogView2Component& view, std::string& header_text, ftxui::ScreenInteractive& screen);

    /// Wire the dual-pane alignment controller that begin_time_alignment() drives.
    void set_align_controller(AlignTimeController* align_controller);

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
    LogView2Component& _view;
    std::string& _header_text;
    ftxui::ScreenInteractive& _screen;
    AlignTimeController* _align_controller = nullptr;
};

} // namespace slayerlog
