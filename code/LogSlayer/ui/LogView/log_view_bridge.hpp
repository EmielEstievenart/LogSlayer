#pragma once

#include <cstddef>
#include <string>

#include <ftxui/component/screen_interactive.hpp>

#include "log_view_service.hpp"

namespace slayerlog
{

class LogViewComponent;
class AllProcessedSources;
class AllTrackedSources;
class AlignTimeController;

/// Terminal-UI implementation of LogViewService backed by LogView, which is
/// thin and pull-based: a refresh is just a redraw request because the view
/// renders straight from the processed sources, and navigation drives the text
/// view controller.
class LogViewBridge final : public LogViewService
{
public:
    LogViewBridge(LogViewComponent& view, std::string& header_text, ftxui::ScreenInteractive& screen);

    /// Wire the dual-pane alignment controller that begin_time_alignment() drives.
    void set_align_controller(AlignTimeController* align_controller);

    void rebuild_view(const AllProcessedSources& processed_sources) override;
    void reload(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources) override;
    bool go_to_line(const AllProcessedSources& processed_sources, int line_number) override;
    int first_visible_line() const override;
    int viewport_line_count() const override;
    void begin_time_alignment(std::size_t aligning_source_index) override;

private:
    LogViewComponent& _view;
    std::string& _header_text;
    ftxui::ScreenInteractive& _screen;
    AlignTimeController* _align_controller = nullptr;
};

} // namespace slayerlog
