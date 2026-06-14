#pragma once

#include <string>

#include "time_alignment_apply.hpp"

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;

/// Core, UI-agnostic facade over the log view that commands drive. Every method
/// has a framework-free signature (primitives, core model types), so the command
/// actions that depend on it stay in the core library. The terminal UI provides
/// a concrete implementation (LogViewBridge over LogController + screen); a future
/// Qt/other UI supplies its own implementation without touching the commands.
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

    virtual int first_visible_line() const = 0;
    virtual int viewport_line_count() const = 0;

    // --- Find ---

    virtual bool set_find_query(AllProcessedSources& processed_sources, std::string query) = 0;
    virtual int total_find_match_count() const = 0;
    virtual int visible_find_match_count(const AllProcessedSources& processed_sources) const = 0;
    virtual const std::string& find_query() const = 0;

    // --- Time alignment ---

    /// Begin the interactive two-step time-alignment selection, invoking the
    /// (UI-agnostic) callback once a source and destination entry are chosen.
    virtual void start_time_alignment(TimeAlignmentApplyCallback apply) = 0;
};

} // namespace slayerlog
