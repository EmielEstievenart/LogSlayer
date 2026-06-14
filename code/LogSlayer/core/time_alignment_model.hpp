#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "time_alignment_apply.hpp"
#include "timestamp/log_timestamp.hpp"

namespace slayerlog
{

class AllProcessedSources;
struct LogEntry;

/// Core, UI-agnostic state machine for the two-step time-alignment workflow:
/// pick a source entry, then a destination entry, then apply the resulting
/// offset through the (framework-free) apply callback. Owns the phase, the
/// chosen source, the selected visible line, and status text, and performs all
/// validation. The terminal UI's TimeAlignmentController wraps this and adds the
/// FTXUI event dispatch plus viewport centring; a different UI reuses this model.
class TimeAlignmentModel
{
public:
    using ApplyCallback = TimeAlignmentApplyCallback;

    void start(int first_selected_line, ApplyCallback apply_callback);
    void cancel();
    [[nodiscard]] bool active() const;

    [[nodiscard]] std::optional<int> selected_line() const;
    [[nodiscard]] const std::string& status_text() const;
    [[nodiscard]] bool status_is_error() const;

    /// Clamp and store the selected visible line (no viewport side effects).
    void set_selected_line(const AllProcessedSources& processed_sources, int visible_line_index);
    /// Move the selection by delta, using fallback_line when nothing is selected yet.
    void move_selection(const AllProcessedSources& processed_sources, int delta, int fallback_line);
    /// Confirm the current selection, advancing the phase or applying the offset.
    /// Always returns true (the event is considered handled).
    bool confirm_selection(const AllProcessedSources& processed_sources);

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
    [[nodiscard]] const LogEntry* selected_entry(const AllProcessedSources& processed_sources) const;

    Phase _phase = Phase::Inactive;
    std::optional<int> _selected_line;
    std::optional<SourceSelection> _source;
    std::string _status;
    bool _status_is_error = false;
    ApplyCallback _apply_callback;
};

} // namespace slayerlog
