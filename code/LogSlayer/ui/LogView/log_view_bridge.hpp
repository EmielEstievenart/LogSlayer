#pragma once

#include <string>

#include <ftxui/component/screen_interactive.hpp>

#include "log_view_service.hpp"

namespace slayerlog
{

class LogController;
class AllProcessedSources;
class AllTrackedSources;

/// Terminal-UI implementation of LogViewService. Bridges the UI-agnostic facade
/// that commands depend on to the concrete FTXUI LogController plus the screen
/// (for redraw requests) and the shared header text. A different UI toolkit would
/// provide its own LogViewService instead of this class.
class LogViewBridge final : public LogViewService
{
public:
    LogViewBridge(LogController& controller, std::string& header_text, ftxui::ScreenInteractive& screen);

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

private:
    LogController& _controller;
    std::string& _header_text;
    ftxui::ScreenInteractive& _screen;
};

} // namespace slayerlog
