#pragma once

#include <functional>
#include <optional>
#include <string>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>

#include <ftxui_components/text_view_controller.hpp>

#include "log_types.hpp"
#include "timestamp/log_timestamp.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

struct TimeAlignmentApplyResult
{
    bool success = false;
    std::string message;
};

class TimeAlignmentController
{
public:
    using ApplyCallback = std::function<TimeAlignmentApplyResult(const LogEntry& source_entry, const LogEntry& destination_entry)>;

    struct FindNavigation
    {
        std::function<bool()> next;
        std::function<bool()> previous;
        std::function<std::optional<VisibleLineIndex>()> active_visible_index;
    };

    void start(int first_visible_line, ApplyCallback apply_callback);
    void cancel();
    bool active() const;

    std::optional<int> selected_line() const;
    const std::string& status_text() const;
    bool status_is_error() const;

    bool handle_event(AllProcessedSources& processed_sources, TextViewController& text_view_controller, ftxui::Event event,
                      const std::function<std::optional<TextViewPosition>(int, int)>& text_position_at, const FindNavigation& find_navigation);

private:
    enum class Phase
    {
        Inactive,
        SelectSource,
        SelectDestination,
    };

    struct SourceSelection
    {
        std::size_t source_index = 0;
        std::string source_label;
        LogTimestamp timestamp;
    };

    void reset();
    void set_status(std::string message, bool is_error = false);
    void set_selected_line(const AllProcessedSources& processed_sources, TextViewController& text_view_controller, int visible_line_index, bool keep_visible);
    void move_selection(const AllProcessedSources& processed_sources, TextViewController& text_view_controller, int delta);
    bool confirm_selection(AllProcessedSources& processed_sources);
    const LogEntry* selected_entry(const AllProcessedSources& processed_sources) const;

    Phase _phase = Phase::Inactive;
    std::optional<int> _selected_line;
    std::optional<SourceSelection> _source;
    std::string _status;
    bool _status_is_error = false;
    ApplyCallback _apply_callback;
};

} // namespace slayerlog
