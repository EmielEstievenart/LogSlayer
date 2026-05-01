#include "all_processed_sources.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <system_error>

#include "all_tracked_sources.hpp"
#include "search_pattern.hpp"

namespace slayerlog
{

namespace
{

constexpr int minimum_source_number_column_width = 2;

int decimal_width(std::size_t value)
{
    int width = 1;
    while (value >= 10)
    {
        value /= 10;
        ++width;
    }

    return width;
}

int rendered_timestamp_width(const LogEntry& entry)
{
    if (entry.metadata.parsed_time_text.empty())
    {
        return 0;
    }

    return static_cast<int>(entry.metadata.parsed_time_text.size()) + 2;
}

std::string trim_text(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

std::optional<int> parse_non_negative_integer(std::string_view text)
{
    const std::string trimmed = trim_text(text);
    if (trimmed.empty())
    {
        return std::nullopt;
    }

    int value               = 0;
    const auto [end, error] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
    if (error != std::errc() || end != trimmed.data() + trimmed.size() || value < 0)
    {
        return std::nullopt;
    }

    return value;
}

std::vector<std::shared_ptr<LogEntry>> make_shared_entries(const std::vector<LogEntry>& lines)
{
    std::vector<std::shared_ptr<LogEntry>> shared_lines;
    shared_lines.reserve(lines.size());
    for (const auto& line : lines)
    {
        shared_lines.push_back(std::make_shared<LogEntry>(line));
    }

    return shared_lines;
}

std::vector<std::shared_ptr<LogEntry>> source_entries_from_index(const AllTrackedSources& tracked_sources, AllLineIndex first_entry_index)
{
    const auto& lines = tracked_sources.all_lines();
    const int clamped_start = std::clamp(first_entry_index.value, 0, static_cast<int>(lines.size()));

    std::vector<std::shared_ptr<LogEntry>> entries;
    entries.reserve(lines.size() - static_cast<std::size_t>(clamped_start));
    for (int index = clamped_start; index < static_cast<int>(lines.size()); ++index)
    {
        entries.push_back(lines[AllLineIndex {index}]);
    }

    return entries;
}

std::string message_text_without_extracted_timestamp(const LogEntry& entry)
{
    if (!entry.metadata.extracted_time_start.has_value() || !entry.metadata.extracted_time_end.has_value())
    {
        return entry.text;
    }

    const std::size_t start = *entry.metadata.extracted_time_start;
    const std::size_t end   = *entry.metadata.extracted_time_end;
    if (start >= end || end > entry.text.size())
    {
        return entry.text;
    }

    std::string deduplication_text = entry.text;
    deduplication_text.erase(start, end - start);
    return deduplication_text;
}

} // namespace

std::optional<HiddenColumnRange> parse_hidden_column_range(std::string_view text)
{
    const std::string trimmed         = trim_text(text);
    const std::size_t separator_index = trimmed.find('-');
    if (separator_index == std::string::npos || trimmed.find('-', separator_index + 1) != std::string::npos)
    {
        return std::nullopt;
    }

    const auto start = parse_non_negative_integer(std::string_view(trimmed).substr(0, separator_index));
    const auto end   = parse_non_negative_integer(std::string_view(trimmed).substr(separator_index + 1));
    if (!start.has_value() || !end.has_value() || *end <= *start)
    {
        return std::nullopt;
    }

    return HiddenColumnRange {*start, *end};
}

void AllProcessedSources::reset()
{
    _all_entries.clear();
    _visible_rows.clear();
    _paused_updates.clear();
    _pending_source_replacement.reset();

    _include_filters.clear();
    _exclude_filters.clear();
    _include_filter_patterns.clear();
    _exclude_filter_patterns.clear();

    _hidden_before_line_number.reset();
    _hidden_columns.reset();

    reset_column_width_cache();

    _updates_paused     = false;
    _show_source_labels = false;
    _show_original_time = false;
    _hide_identical_lines = true;
}

void AllProcessedSources::append_lines(const std::vector<std::shared_ptr<LogEntry>>& lines)
{
    if (_updates_paused)
    {
        _paused_updates.insert(_paused_updates.end(), lines.begin(), lines.end());
    }
    else
    {
        append_lines_immediately(lines);
    }
}

void AllProcessedSources::append_lines(const std::vector<LogEntry>& lines)
{
    append_lines(make_shared_entries(lines));
}

void AllProcessedSources::append_batch(const std::vector<std::shared_ptr<LogEntry>>& batch)
{
    append_lines(merge_log_batch(batch));
}

void AllProcessedSources::append_batch(const std::vector<LogEntry>& batch)
{
    append_batch(make_shared_entries(batch));
}

void AllProcessedSources::replace_batch(const std::vector<std::shared_ptr<LogEntry>>& batch)
{
    const auto merged_lines = merge_log_batch(batch);

    _all_entries.clear();
    _visible_rows.clear();
    _paused_updates.clear();
    _pending_source_replacement.reset();

    _all_entries.reserve(merged_lines.size());
    for (const auto& line : merged_lines)
    {
        _all_entries.push_back(line);
        observe_entry_widths(AllLineIndex {static_cast<int>(_all_entries.size() - 1)}, *line);
    }

    if (_all_entries.empty())
    {
        reset_column_width_cache();
    }

    rebuild_visible_entries();
}

void AllProcessedSources::replace_batch(const std::vector<LogEntry>& batch)
{
    replace_batch(make_shared_entries(batch));
}

void AllProcessedSources::append_from_sources(const AllTrackedSources& tracked_sources, AllLineIndex first_new_entry_index)
{
    const auto& lines = tracked_sources.all_lines();
    if (first_new_entry_index.value < 0 || first_new_entry_index.value >= static_cast<int>(lines.size()))
    {
        return;
    }

    std::vector<std::shared_ptr<LogEntry>> appended_lines;
    appended_lines.reserve(lines.size() - static_cast<std::size_t>(first_new_entry_index.value));
    for (int index = first_new_entry_index.value; index < static_cast<int>(lines.size()); ++index)
    {
        appended_lines.push_back(lines[AllLineIndex {index}]);
    }

    append_lines(appended_lines);
}

void AllProcessedSources::replace_from_sources(const AllTrackedSources& tracked_sources, AllLineIndex first_changed_entry_index)
{
    int replacement_start = first_changed_entry_index.value;
    if (_pending_source_replacement.has_value())
    {
        replacement_start = std::min(replacement_start, _pending_source_replacement->first_changed_entry_index.value);
    }

    const AllLineIndex effective_start {replacement_start};
    const auto replacement_entries = source_entries_from_index(tracked_sources, effective_start);

    if (_updates_paused)
    {
        _pending_source_replacement = PendingSourceReplacement {
            effective_start,
            replacement_entries,
        };
        _paused_updates.clear();
        return;
    }

    apply_source_replacement(effective_start, replacement_entries);
}

void AllProcessedSources::rebuild_from_sources(const AllTrackedSources& tracked_sources)
{
    _all_entries.clear();
    _visible_rows.clear();
    _paused_updates.clear();
    _pending_source_replacement.reset();

    const auto& lines = tracked_sources.all_lines();
    _all_entries.reserve(lines.size());
    for (const auto& line : lines)
    {
        _all_entries.push_back(line);
        observe_entry_widths(AllLineIndex {static_cast<int>(_all_entries.size() - 1)}, *line);
    }

    if (_all_entries.empty())
    {
        reset_column_width_cache();
    }

    rebuild_visible_entries();
}

void AllProcessedSources::toggle_pause()
{
    _updates_paused = !_updates_paused;
    if (!_updates_paused)
    {
        flush_paused_updates();
    }
}

bool AllProcessedSources::updates_paused() const
{
    return _updates_paused;
}

void AllProcessedSources::set_show_source_labels(bool show_source_labels)
{
    _show_source_labels = show_source_labels;
}

bool AllProcessedSources::show_source_labels() const
{
    return _show_source_labels;
}

void AllProcessedSources::set_show_original_time(bool show_original_time)
{
    _show_original_time = show_original_time;
}

bool AllProcessedSources::show_original_time() const
{
    return _show_original_time;
}

void AllProcessedSources::set_hide_identical_lines(bool hide_identical_lines)
{
    if (_hide_identical_lines == hide_identical_lines)
    {
        return;
    }

    _hide_identical_lines = hide_identical_lines;
    rebuild_visible_entries();
}

bool AllProcessedSources::hide_identical_lines() const
{
    return _hide_identical_lines;
}

int AllProcessedSources::line_number_column_width() const
{
    return _line_number_column_width;
}

int AllProcessedSources::timestamp_column_width() const
{
    return _timestamp_column_width;
}

int AllProcessedSources::source_number_column_width() const
{
    return _source_number_column_width;
}

int AllProcessedSources::source_number_column_start() const
{
    int column_start = _line_number_column_width + 1;
    if (_timestamp_column_width > 0)
    {
        column_start += _timestamp_column_width + 1;
    }

    return column_start;
}

bool AllProcessedSources::consume_column_width_growth()
{
    const bool grew = _column_width_grew;
    _column_width_grew = false;
    return grew;
}

void AllProcessedSources::add_include_filter(std::string filter_text)
{
    filter_text = trim_search_text(filter_text);
    if (filter_text.empty())
    {
        return;
    }

    const SearchPattern pattern = compile_search_pattern(filter_text);

    _include_filters.push_back(pattern.raw_text);
    _include_filter_patterns.push_back(pattern);
    rebuild_visible_entries();
}

void AllProcessedSources::add_exclude_filter(std::string filter_text)
{
    filter_text = trim_search_text(filter_text);
    if (filter_text.empty())
    {
        return;
    }

    const SearchPattern pattern = compile_search_pattern(filter_text);

    _exclude_filters.push_back(pattern.raw_text);
    _exclude_filter_patterns.push_back(pattern);
    rebuild_visible_entries();
}

void AllProcessedSources::reset_filters()
{
    _include_filters.clear();
    _exclude_filters.clear();
    _include_filter_patterns.clear();
    _exclude_filter_patterns.clear();
    rebuild_visible_entries();
}

bool AllProcessedSources::remove_filters(const std::vector<FilterSelection>& filters)
{
    if (filters.empty())
    {
        return false;
    }

    std::vector<std::size_t> include_indices;
    std::vector<std::size_t> exclude_indices;
    include_indices.reserve(filters.size());
    exclude_indices.reserve(filters.size());

    for (const auto& filter : filters)
    {
        if (filter.include)
        {
            if (filter.index >= _include_filters.size())
            {
                return false;
            }

            include_indices.push_back(filter.index);
        }
        else
        {
            if (filter.index >= _exclude_filters.size())
            {
                return false;
            }

            exclude_indices.push_back(filter.index);
        }
    }

    std::sort(include_indices.begin(), include_indices.end());
    include_indices.erase(std::unique(include_indices.begin(), include_indices.end()), include_indices.end());
    std::sort(exclude_indices.begin(), exclude_indices.end());
    exclude_indices.erase(std::unique(exclude_indices.begin(), exclude_indices.end()), exclude_indices.end());

    auto erase_selected = [](auto& values, const std::vector<std::size_t>& selected_indices)
    {
        if (selected_indices.empty())
        {
            return;
        }

        std::size_t next_selected = 0;
        std::size_t write_index   = 0;
        for (std::size_t read_index = 0; read_index < values.size(); ++read_index)
        {
            if (next_selected < selected_indices.size() && read_index == selected_indices[next_selected])
            {
                ++next_selected;
                continue;
            }

            if (write_index != read_index)
            {
                values[write_index] = std::move(values[read_index]);
            }

            ++write_index;
        }

        values.resize(write_index);
    };

    erase_selected(_include_filters, include_indices);
    erase_selected(_include_filter_patterns, include_indices);
    erase_selected(_exclude_filters, exclude_indices);
    erase_selected(_exclude_filter_patterns, exclude_indices);
    rebuild_visible_entries();
    return true;
}

const std::vector<std::string>& AllProcessedSources::include_filters() const
{
    return _include_filters;
}

const std::vector<std::string>& AllProcessedSources::exclude_filters() const
{
    return _exclude_filters;
}

std::vector<AllProcessedSources::FilterSelection> AllProcessedSources::all_filters() const
{
    std::vector<FilterSelection> filters;
    filters.reserve(_include_filters.size() + _exclude_filters.size());

    for (std::size_t index = 0; index < _include_filters.size(); ++index)
    {
        filters.push_back(FilterSelection {true, index, _include_filters[index]});
    }

    for (std::size_t index = 0; index < _exclude_filters.size(); ++index)
    {
        filters.push_back(FilterSelection {false, index, _exclude_filters[index]});
    }

    return filters;
}

void AllProcessedSources::hide_before_line_number(int line_number)
{
    _hidden_before_line_number = line_number > 1 ? std::optional<int>(line_number) : std::nullopt;
    rebuild_visible_entries();
}

std::optional<int> AllProcessedSources::hidden_before_line_number() const
{
    return _hidden_before_line_number;
}

void AllProcessedSources::hide_columns(int start_column, int end_column)
{
    if (start_column < 0 || end_column <= start_column)
    {
        _hidden_columns.reset();
        return;
    }

    _hidden_columns = HiddenColumnRange {start_column, end_column};
}

void AllProcessedSources::reset_hidden_columns()
{
    _hidden_columns.reset();
}

std::optional<HiddenColumnRange> AllProcessedSources::hidden_columns() const
{
    return _hidden_columns;
}

const LogEntry& AllProcessedSources::entry_at(AllLineIndex entry_index) const
{
    return *_all_entries[entry_index];
}

std::optional<AllLineIndex> AllProcessedSources::entry_index_for_visible_line(VisibleLineIndex visible_line_index) const
{
    if (visible_line_index.value < 0 || visible_line_index.value >= static_cast<int>(_visible_rows.size()))
    {
        return std::nullopt;
    }

    return _visible_rows[visible_line_index].entry_index;
}

std::optional<VisibleLineIndex> AllProcessedSources::visible_line_index_for_entry(AllLineIndex entry_index) const
{
    for (int index = 0; index < static_cast<int>(_visible_rows.size()); ++index)
    {
        const VisibleLineIndex visible_line_index {index};
        const auto& visible_row = _visible_rows[visible_line_index];
        if (visible_row.entry_index.has_value() && *visible_row.entry_index == entry_index)
        {
            return visible_line_index;
        }

        if (!visible_row.hidden_identical_run.has_value())
        {
            continue;
        }

        const auto& hidden_identical_run = *visible_row.hidden_identical_run;
        if (entry_index.value >= hidden_identical_run.first_hidden_entry_index.value && entry_index.value <= hidden_identical_run.last_hidden_entry_index.value)
        {
            return visible_line_index;
        }
    }

    return std::nullopt;
}

std::optional<int> AllProcessedSources::line_number_for_visible_line(VisibleLineIndex visible_line_index) const
{
    if (visible_line_index.value < 0 || visible_line_index.value >= static_cast<int>(_visible_rows.size()))
    {
        return std::nullopt;
    }

    const auto& visible_row = _visible_rows[visible_line_index];
    if (visible_row.entry_index.has_value())
    {
        return visible_row.entry_index->value + 1;
    }

    if (visible_row.hidden_identical_run.has_value())
    {
        return visible_row.hidden_identical_run->last_hidden_entry_index.value + 1;
    }

    return std::nullopt;
}

std::optional<VisibleLineIndex> AllProcessedSources::visible_line_index_for_line_number(int line_number) const
{
    if (line_number <= 0)
    {
        return std::nullopt;
    }

    return visible_line_index_for_entry(AllLineIndex {line_number - 1});
}

bool AllProcessedSources::entry_index_is_visible(AllLineIndex entry_index) const
{
    const auto visible_line_index = visible_line_index_for_entry(entry_index);
    if (!visible_line_index.has_value())
    {
        return false;
    }

    const auto& visible_row = _visible_rows[*visible_line_index];
    return visible_row.entry_index.has_value() && *visible_row.entry_index == entry_index;
}

int AllProcessedSources::line_count() const
{
    return static_cast<int>(_visible_rows.size());
}

int AllProcessedSources::total_line_count() const
{
    return static_cast<int>(_all_entries.size());
}

std::string AllProcessedSources::rendered_line(int index) const
{
    const VisibleLineIndex visible_line_index {index};
    const auto& visible_row = _visible_rows[visible_line_index];
    if (visible_row.entry_index.has_value())
    {
        return render_entry(*visible_row.entry_index);
    }

    if (visible_row.hidden_identical_run.has_value())
    {
        return render_hidden_identical_row(*visible_row.hidden_identical_run);
    }

    return {};
}

std::vector<std::string> AllProcessedSources::rendered_lines(int first_index, int count) const
{
    if (count <= 0)
    {
        return {};
    }

    const int clamped_first = std::max(0, first_index);
    if (clamped_first >= static_cast<int>(_visible_rows.size()))
    {
        return {};
    }

    const int last_index = std::min(static_cast<int>(_visible_rows.size()), clamped_first + count);
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(last_index - clamped_first));
    for (int index = clamped_first; index < last_index; ++index)
    {
        lines.push_back(rendered_line(index));
    }

    return lines;
}

int AllProcessedSources::max_rendered_line_width() const
{
    int width = 0;
    for (int index = 0; index < static_cast<int>(_visible_rows.size()); ++index)
    {
        width = std::max(width, static_cast<int>(rendered_line(index).size()));
    }

    return width;
}

std::string AllProcessedSources::render_entry(AllLineIndex entry_index) const
{
    std::ostringstream output;
    const auto& entry = *_all_entries[entry_index];
    output << std::setw(_line_number_column_width) << std::right << (entry_index.value + 1) << " ";
    if (_timestamp_column_width > 0)
    {
        output << std::left << std::setw(_timestamp_column_width) << render_timestamp_text(entry) << std::right << " ";
    }

    if (_show_source_labels)
    {
        output << std::setw(_source_number_column_width) << std::right << (entry.metadata.source_index + 1) << " ";
    }

    output << render_message_text(entry);
    return apply_hidden_columns(output.str());
}

std::string AllProcessedSources::render_hidden_identical_row(const HiddenIdenticalRun& hidden_identical_run) const
{
    std::string rendered_text(static_cast<std::size_t>(_line_number_column_width), ' ');
    rendered_text += ' ';

    if (_timestamp_column_width > 0)
    {
        rendered_text += std::string(static_cast<std::size_t>(_timestamp_column_width), ' ');
        rendered_text += ' ';
    }

    if (_show_source_labels)
    {
        rendered_text += std::string(static_cast<std::size_t>(_source_number_column_width), ' ');
        rendered_text += ' ';
    }

    rendered_text += "hiding " + std::to_string(hidden_identical_run.hidden_count) + " identical messages above";
    return apply_hidden_columns(std::move(rendered_text));
}

std::string AllProcessedSources::entry_deduplication_text(const LogEntry& entry) const
{
    return std::to_string(entry.metadata.source_index) + "\n" + message_text_without_extracted_timestamp(entry);
}

void AllProcessedSources::append_lines_immediately(const std::vector<std::shared_ptr<LogEntry>>& lines)
{
    const AllLineIndex first_new_entry_index {static_cast<int>(_all_entries.size())};

    for (const auto& line : lines)
    {
        _all_entries.push_back(line);
        observe_entry_widths(AllLineIndex {static_cast<int>(_all_entries.size() - 1)}, *line);
    }

    expand_visible_entries(first_new_entry_index);
}

void AllProcessedSources::apply_source_replacement(AllLineIndex first_changed_entry_index, const std::vector<std::shared_ptr<LogEntry>>& replacement_entries)
{
    const int clamped_start = std::clamp(first_changed_entry_index.value, 0, static_cast<int>(_all_entries.size()));

    IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex> updated_entries;
    updated_entries.reserve(static_cast<std::size_t>(clamped_start) + replacement_entries.size());
    for (int index = 0; index < clamped_start; ++index)
    {
        updated_entries.push_back(_all_entries[AllLineIndex {index}]);
    }

    for (const auto& entry : replacement_entries)
    {
        updated_entries.push_back(entry);
    }

    _all_entries = std::move(updated_entries);

    if (_all_entries.empty())
    {
        reset_column_width_cache();
    }
    else
    {
        for (int index = clamped_start; index < static_cast<int>(_all_entries.size()); ++index)
        {
            const AllLineIndex entry_index {index};
            observe_entry_widths(entry_index, *_all_entries[entry_index]);
        }
    }

    rebuild_visible_entries();
}

void AllProcessedSources::flush_paused_updates()
{
    if (_pending_source_replacement.has_value())
    {
        apply_source_replacement(_pending_source_replacement->first_changed_entry_index, _pending_source_replacement->replacement_entries);
        _pending_source_replacement.reset();
    }

    if (_paused_updates.empty())
    {
        return;
    }

    append_lines_immediately(_paused_updates);
    _paused_updates.clear();
}

void AllProcessedSources::rebuild_visible_entries()
{
    _visible_rows.clear();
    _visible_rows.reserve(_all_entries.size());

    std::size_t index = 0;
    if (_hidden_before_line_number.has_value())
    {
        index = static_cast<std::size_t>(std::max(0, *_hidden_before_line_number - 1));
    }

    std::optional<std::string> previous_deduplication_text;
    
    for (; index < _all_entries.size(); ++index)
    {
        const AllLineIndex entry_index {static_cast<int>(index)};
        if (entry_matches_filters(_all_entries[entry_index]))
        {
            if (!_hide_identical_lines)
            {
                _visible_rows.push_back(VisibleRow {
                    std::optional<AllLineIndex>(entry_index),
                    std::nullopt,
                });
                previous_deduplication_text = std::nullopt;
                continue;
            }

            const std::string deduplication_text = entry_deduplication_text(*_all_entries[entry_index]);
            if (!previous_deduplication_text.has_value() || *previous_deduplication_text != deduplication_text)
            {
                _visible_rows.push_back(VisibleRow {
                    std::optional<AllLineIndex>(entry_index),
                    std::nullopt,
                });
                previous_deduplication_text = deduplication_text;
                continue;
            }

            if (_visible_rows.empty() || !_visible_rows[VisibleLineIndex {static_cast<int>(_visible_rows.size() - 1)}].hidden_identical_run.has_value())
            {
                _visible_rows.push_back(VisibleRow {
                    std::nullopt,
                    HiddenIdenticalRun {
                        entry_index,
                        entry_index,
                        1,
                    },
                });
                continue;
            }

            auto& hidden_identical_run = _visible_rows[VisibleLineIndex {static_cast<int>(_visible_rows.size() - 1)}].hidden_identical_run;
            hidden_identical_run->last_hidden_entry_index = entry_index;
            ++hidden_identical_run->hidden_count;
        }
    }
}

void AllProcessedSources::expand_visible_entries(AllLineIndex first_new_entry_index)
{
    (void)first_new_entry_index;
    rebuild_visible_entries();
}

void AllProcessedSources::reset_column_width_cache()
{
    _line_number_column_width   = 1;
    _timestamp_column_width     = 0;
    _source_number_column_width = minimum_source_number_column_width;
    _column_width_grew          = false;
}

void AllProcessedSources::observe_entry_widths(AllLineIndex entry_index, const LogEntry& entry)
{
    bool grew = false;

    const int line_width = decimal_width(static_cast<std::size_t>(entry_index.value + 1));
    if (line_width > _line_number_column_width)
    {
        _line_number_column_width = line_width;
        grew = true;
    }

    const int timestamp_width = rendered_timestamp_width(entry);
    if (timestamp_width > _timestamp_column_width)
    {
        _timestamp_column_width = timestamp_width;
        grew = true;
    }

    const int source_width = std::max(minimum_source_number_column_width, decimal_width(entry.metadata.source_index + 1));
    if (source_width > _source_number_column_width)
    {
        _source_number_column_width = source_width;
        grew = true;
    }

    _column_width_grew = _column_width_grew || grew;
}

std::string AllProcessedSources::render_timestamp_text(const LogEntry& entry) const
{
    if (entry.metadata.parsed_time_text.empty())
    {
        return {};
    }

    return "{" + entry.metadata.parsed_time_text + "}";
}

std::string AllProcessedSources::render_message_text(const LogEntry& entry) const
{
    if (_show_original_time)
    {
        return entry.text;
    }

    return message_text_without_extracted_timestamp(entry);
}

bool AllProcessedSources::entry_matches_filters(const std::shared_ptr<LogEntry>& entry) const
{
    const std::string searchable_text = entry->metadata.source_label + "\n" + entry->text;
    const bool matches_include        = _include_filter_patterns.empty() || matches_any_pattern(searchable_text, _include_filter_patterns);
    const bool matches_exclude        = matches_any_pattern(searchable_text, _exclude_filter_patterns);
    return matches_include && !matches_exclude;
}

std::string AllProcessedSources::apply_hidden_columns(std::string text) const
{
    if (!_hidden_columns.has_value())
    {
        return text;
    }

    const int clamped_start = std::clamp(_hidden_columns->start, 0, static_cast<int>(text.size()));
    const int clamped_end   = std::clamp(_hidden_columns->end, clamped_start, static_cast<int>(text.size()));
    if (clamped_start == clamped_end)
    {
        return text;
    }

    text.erase(static_cast<std::size_t>(clamped_start), static_cast<std::size_t>(clamped_end - clamped_start));
    return text;
}

} // namespace slayerlog
