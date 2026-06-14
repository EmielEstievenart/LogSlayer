#pragma once

#include <optional>
#include <string>

#include "log_types.hpp"
#include "search_pattern.hpp"

namespace slayerlog
{

class AllProcessedSources;
struct LogEntry;

/// Core, UI-agnostic find/search engine over the processed log entries. Owns the
/// compiled query, the sorted set of matching entry indices, and the active
/// match, and computes match navigation purely in terms of entry/visible
/// indices. The terminal UI's LogController delegates all find logic here and
/// only adds viewport centring on top; a different UI reuses this unchanged.
class FindState
{
public:
    /// Compile and apply a query, rebuilding matches and focusing the first
    /// visible one. Returns true when a visible match was focused.
    bool set_query(const AllProcessedSources& processed_sources, std::string query);
    void clear();

    [[nodiscard]] bool active() const;
    [[nodiscard]] const std::string& query() const;
    [[nodiscard]] int total_match_count() const;
    [[nodiscard]] int visible_match_count(const AllProcessedSources& processed_sources) const;
    [[nodiscard]] bool visible_line_matches(const AllProcessedSources& processed_sources, int visible_index) const;

    /// Advance / retreat the active match to the next visible one (wrapping).
    /// Returns true when the active match moved.
    bool go_to_next_match(const AllProcessedSources& processed_sources);
    bool go_to_previous_match(const AllProcessedSources& processed_sources);

    [[nodiscard]] std::optional<AllLineIndex> active_entry_index() const;
    [[nodiscard]] std::optional<VisibleLineIndex> active_visible_index(const AllProcessedSources& processed_sources) const;

    /// Recompute the full match set (after a filter/model change).
    void rebuild_matches(const AllProcessedSources& processed_sources);
    /// Append matches for entries appended since the last rebuild (streaming).
    void expand_matches(const AllProcessedSources& processed_sources, AllLineIndex first_new_entry_index);

private:
    [[nodiscard]] bool entry_matches_query(const LogEntry& entry) const;

    std::string _query;
    std::optional<SearchPattern> _pattern;
    IndexedVector<AllLineIndex, FindResultIndex> _match_entry_indices;
    std::optional<AllLineIndex> _active_entry_index;
};

} // namespace slayerlog
