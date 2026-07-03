#pragma once

#include <cstddef>

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;

/// Core, UI-agnostic facade over the log view that commands drive. Every method
/// has a framework-free signature (primitives, core model types), so the command
/// actions that depend on it stay in the core library. The terminal UI provides
/// a concrete implementation (LogViewBridge over LogViewComponent + screen); a
/// future Qt/other UI supplies its own implementation without touching the commands.
class LogViewService
{
public:
    virtual ~LogViewService() = default;

    // --- Refresh ---

    /// Re-render the view from the current processed sources (after filter,
    /// column, dedup or time-display changes).
    virtual void rebuild_view(const AllProcessedSources& processed_sources) = 0;

    /// Full refresh after the set of sources changed: rebuild the processed
    /// model from the tracked sources, re-render, and request a redraw.
    virtual void reload(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources) = 0;

    // --- Navigation ---

    /// Scroll the view to the given 1-based line number. Returns false when the
    /// line is hidden by the current filters or line cutoff.
    virtual bool go_to_line(const AllProcessedSources& processed_sources, int line_number) = 0;

    virtual int first_visible_line() const  = 0;
    virtual int viewport_line_count() const = 0;

    // --- Time alignment ---

    /// Begin the dual-pane "align time" mode for the source at @p aligning_source_index.
    /// The view shows the other sources beside that source's lines and lets the user
    /// pick a line plus one or two references, then nudge a preview offset before
    /// committing it. Views that do not implement this mode leave it a no-op.
    virtual void begin_time_alignment(std::size_t aligning_source_index) { (void)aligning_source_index; }
};

} // namespace slayerlog
