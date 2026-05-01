#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>

#include <ftxui_components/text_view_controller.hpp>

#include "tracked_sources/all_processed_sources.hpp"
#include "search_pattern.hpp"

namespace slayerlog
{

struct LogEventResult
{
    bool handled      = false;
    bool request_exit = false;
};

class LogController
{
public:
    LogController();

    void reset();

    // --- Content management ---

    // Full rebuild: renders all visible lines into inactive buffer, swaps to active.
    // Call after filter changes, model resets, replace_batch, hide_columns, etc.
    void rebuild_view(const AllProcessedSources& processed_sources);

    // Incremental update: appends new rendered lines to active buffer.
    // Call after append_batch / append_lines for streaming.
    void sync_view(const AllProcessedSources& processed_sources);

    // --- Domain-specific navigation ---

    bool go_to_line(const AllProcessedSources& processed_sources, int line_number);

    // --- Find ---

    bool set_find_query(AllProcessedSources& processed_sources, std::string query);
    void clear_find(AllProcessedSources& processed_sources);
    bool find_active() const;
    const std::string& find_query() const;
    int total_find_match_count() const;
    int visible_find_match_count(const AllProcessedSources& processed_sources) const;
    bool visible_line_matches_find(const AllProcessedSources& processed_sources, int visible_index) const;
    bool go_to_next_find_match(const AllProcessedSources& processed_sources);
    bool go_to_previous_find_match(const AllProcessedSources& processed_sources);
    std::optional<VisibleLineIndex> active_find_visible_index(const AllProcessedSources& processed_sources) const;

    // --- Event handling ---

    LogEventResult handle_event(AllProcessedSources& processed_sources, ftxui::Event event, const std::function<std::optional<TextViewPosition>(const ftxui::Mouse&)>& mouse_to_text_position);

    // --- Access to underlying text view ---

    TextViewController& text_view_controller();
    const TextViewController& text_view_controller() const;
    const std::string& line_at(int index) const;

private:
    std::vector<std::string>& active_buffer();
    const std::vector<std::string>& active_buffer() const;
    std::vector<std::string>& inactive_buffer();
    void rebuild_find_matches(const AllProcessedSources& processed_sources);
    void expand_find_matches(const AllProcessedSources& processed_sources, AllLineIndex first_new_entry_index);
    bool entry_matches_find_query(const LogEntry& entry) const;

    TextViewController _text_view_controller;

    std::vector<std::string> _buffer_a;
    std::vector<std::string> _buffer_b;
    bool _active_buffer_is_a = true;
    int _synced_line_count   = 0;
    int _max_line_width      = 0;

    std::string _find_query;
    std::optional<SearchPattern> _find_pattern;
    IndexedVector<AllLineIndex, FindResultIndex> _find_match_entry_indices;
    std::optional<AllLineIndex> _active_find_entry_index;
};

} // namespace slayerlog
