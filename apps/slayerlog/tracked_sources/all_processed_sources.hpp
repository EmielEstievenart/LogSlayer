#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "log_batch.hpp"
#include "log_types.hpp"
#include "search_pattern.hpp"

namespace slayerlog
{

class AllTrackedSources;

class AllProcessedSources
{
public:
    struct HiddenIdenticalRun
    {
        AllLineIndex first_hidden_entry_index;
        AllLineIndex last_hidden_entry_index;
        int hidden_count = 0;
    };

    struct FilterSelection
    {
        bool include      = true;
        std::size_t index = 0;
        std::string text;
    };

    void reset();

    void append_lines(const std::vector<std::shared_ptr<LogEntry>>& lines);
    void append_lines(const std::vector<LogEntry>& lines);
    void append_batch(const std::vector<std::shared_ptr<LogEntry>>& batch);
    void append_batch(const std::vector<LogEntry>& batch);
    void replace_batch(const std::vector<std::shared_ptr<LogEntry>>& batch);
    void replace_batch(const std::vector<LogEntry>& batch);

    void append_from_sources(const AllTrackedSources& tracked_sources, AllLineIndex first_new_entry_index);
    void replace_from_sources(const AllTrackedSources& tracked_sources, AllLineIndex first_changed_entry_index);
    void rebuild_from_sources(const AllTrackedSources& tracked_sources);

    void toggle_pause();
    bool updates_paused() const;
    void set_show_source_labels(bool show_source_labels);
    bool show_source_labels() const;
    void set_show_original_time(bool show_original_time);
    bool show_original_time() const;
    void set_hide_identical_lines(bool hide_identical_lines);
    bool hide_identical_lines() const;
    void add_include_filter(std::string filter_text);
    void add_exclude_filter(std::string filter_text);
    void reset_filters();
    bool remove_filters(const std::vector<FilterSelection>& filters);
    const std::vector<std::string>& include_filters() const;
    const std::vector<std::string>& exclude_filters() const;
    std::vector<FilterSelection> all_filters() const;
    void hide_before_line_number(int line_number);
    std::optional<int> hidden_before_line_number() const;
    void hide_columns(int start_column, int end_column);
    void reset_hidden_columns();
    std::optional<HiddenColumnRange> hidden_columns() const;

    const LogEntry& entry_at(AllLineIndex entry_index) const;
    std::optional<AllLineIndex> entry_index_for_visible_line(VisibleLineIndex visible_line_index) const;
    std::optional<VisibleLineIndex> visible_line_index_for_entry(AllLineIndex entry_index) const;
    std::optional<int> line_number_for_visible_line(VisibleLineIndex visible_line_index) const;
    std::optional<VisibleLineIndex> visible_line_index_for_line_number(int line_number) const;
    bool entry_index_is_visible(AllLineIndex entry_index) const;

    int line_count() const;
    int total_line_count() const;
    int line_number_column_width() const;
    int timestamp_column_width() const;
    int source_number_column_width() const;
    int source_number_column_start() const;
    bool consume_column_width_growth();
    std::string rendered_line(int index) const;
    std::vector<std::string> rendered_lines(int first_index, int count) const;
    int max_rendered_line_width() const;

private:
    struct VisibleRow
    {
        std::optional<AllLineIndex> entry_index;
        std::optional<HiddenIdenticalRun> hidden_identical_run;
    };

    struct PendingSourceReplacement
    {
        AllLineIndex first_changed_entry_index;
        std::vector<std::shared_ptr<LogEntry>> replacement_entries;
    };

    std::string render_entry(AllLineIndex entry_index) const;
    std::string render_hidden_identical_row(const HiddenIdenticalRun& hidden_identical_run) const;
    std::string entry_deduplication_text(const LogEntry& entry) const;
    void append_lines_immediately(const std::vector<std::shared_ptr<LogEntry>>& lines);
    void apply_source_replacement(AllLineIndex first_changed_entry_index, const std::vector<std::shared_ptr<LogEntry>>& replacement_entries);
    void flush_paused_updates();
    void rebuild_visible_entries();
    void expand_visible_entries(AllLineIndex first_new_entry_index);
    void reset_column_width_cache();
    void observe_entry_widths(AllLineIndex entry_index, const LogEntry& entry);
    std::string render_timestamp_text(const LogEntry& entry) const;
    std::string render_message_text(const LogEntry& entry) const;
    bool entry_matches_filters(const std::shared_ptr<LogEntry>& entry) const;
    std::string apply_hidden_columns(std::string text) const;

    IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex> _all_entries;
    IndexedVector<VisibleRow, VisibleLineIndex> _visible_rows;
    std::vector<std::shared_ptr<LogEntry>> _paused_updates;
    std::optional<PendingSourceReplacement> _pending_source_replacement;

    std::vector<std::string> _include_filters;
    std::vector<std::string> _exclude_filters;
    std::vector<SearchPattern> _include_filter_patterns;
    std::vector<SearchPattern> _exclude_filter_patterns;

    std::optional<int> _hidden_before_line_number;
    std::optional<HiddenColumnRange> _hidden_columns;

    int _line_number_column_width   = 1;
    int _timestamp_column_width     = 0;
    int _source_number_column_width = 2;
    bool _column_width_grew         = false;

    bool _updates_paused     = false;
    bool _show_source_labels = false;
    bool _show_original_time = false;
    bool _hide_identical_lines = true;
};

} // namespace slayerlog
