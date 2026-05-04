#include "all_tracked_sources.hpp"

#include <cstddef>
#include <chrono>
#include <exception>
#include <memory>
#include <utility>

#include "debug_log.hpp"
#include "tracked_source_base.hpp"
#include "tracked_source_factory.hpp"

namespace slayerlog
{

namespace
{

std::optional<std::chrono::system_clock::time_point> earliest_new_timestamp(const std::vector<LogBatchSourceRange>& source_ranges)
{
    std::optional<std::chrono::system_clock::time_point> earliest_timestamp;
    for (const auto& source_range : source_ranges)
    {
        if (source_range.entries == nullptr || source_range.first_entry_index >= source_range.entries->size())
        {
            continue;
        }

        const auto& entry = (*source_range.entries)[source_range.first_entry_index];
        if (!entry->metadata.timestamp.has_value())
        {
            continue;
        }

        if (!earliest_timestamp.has_value() || entry->metadata.timestamp.value() < earliest_timestamp.value())
        {
            earliest_timestamp = entry->metadata.timestamp;
        }
    }

    return earliest_timestamp;
}

std::size_t find_rewrite_start_index(const IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex>& all_lines,
                                     const std::chrono::system_clock::time_point& earliest_timestamp)
{
    for (std::size_t line_index = 0; line_index < all_lines.size(); ++line_index)
    {
        const auto& line = all_lines[AllLineIndex {static_cast<int>(line_index)}];
        if (!line->metadata.timestamp.has_value())
        {
            continue;
        }

        if (line->metadata.timestamp.value() >= earliest_timestamp)
        {
            return line_index;
        }
    }

    return all_lines.size();
}

} // namespace

AllTrackedSources::AllTrackedSources(std::shared_ptr<const TimestampFormatCatalog> timestamp_formats) : _timestamp_formats(std::move(timestamp_formats))
{
    if (_timestamp_formats == nullptr)
    {
        _timestamp_formats = default_timestamp_format_catalog();
    }
}

AllTrackedSources::~AllTrackedSources() = default;

std::optional<std::string> AllTrackedSources::open_source(const LogSource& source)
{
    if (contains_source(source))
    {
        return "Source already open: " + source_display_path(source);
    }

    try
    {
        const std::size_t source_index = _sources.size();
        auto source_state              = create_tracked_source(source, source_display_path(source), _timestamp_formats);

        source_state->poll();

        _sources.push_back(std::move(source_state));
        rebuild_source_labels();
        rebuild_all_lines();

        SLAYERLOG_LOG_INFO("Opened source index=" << source_index << " source=" << source_display_path(source));
        return std::nullopt;
    }
    catch (const std::exception& ex)
    {
        return ex.what();
    }
}

std::optional<std::string> AllTrackedSources::add_opened_source(std::unique_ptr<TrackedSourceBase> source_state)
{
    if (source_state == nullptr)
    {
        return "Opened source is invalid";
    }

    if (contains_source(source_state->source()))
    {
        return "Source already open: " + source_display_path(source_state->source());
    }

    const std::size_t source_index = _sources.size();
    const std::string display_path = source_display_path(source_state->source());
    _sources.push_back(std::move(source_state));
    rebuild_source_labels();
    rebuild_all_lines();

    SLAYERLOG_LOG_INFO("Opened source index=" << source_index << " source=" << display_path);
    return std::nullopt;
}

std::optional<std::string> AllTrackedSources::close_source(std::size_t source_index, std::string* closed_label)
{
    if (source_index >= _sources.size())
    {
        return "Invalid open file selection";
    }

    if (closed_label != nullptr)
    {
        *closed_label = _sources[source_index]->source_label();
    }

    _sources.erase(_sources.begin() + static_cast<std::ptrdiff_t>(source_index));
    rebuild_source_labels();
    rebuild_all_lines();
    return std::nullopt;
}

std::optional<AllLineIndex> AllTrackedSources::poll()
{
    std::vector<LogBatchSourceRange> source_ranges;
    source_ranges.reserve(_sources.size());

    for (std::size_t source_index = 0; source_index < _sources.size(); ++source_index)
    {
        auto& source = _sources[source_index];

        try
        {
            const std::size_t first_new_entry_index = source->entries().size();

            if (!source->poll())
            {
                continue;
            }

            append_source_range(source_ranges, *source, source_index, first_new_entry_index);
        }
        catch (const std::exception& ex)
        {
            SLAYERLOG_LOG_WARNING("Watcher poll threw for source=" << source_display_path(source->source()) << " error=" << ex.what());
            continue;
        }
        catch (...)
        {
            SLAYERLOG_LOG_WARNING("Watcher poll threw for source=" << source_display_path(source->source()) << " error=<unknown>");
            continue;
        }
    }

    if (source_ranges.empty())
    {
        return std::nullopt;
    }

    std::vector<std::shared_ptr<LogEntry>> merged_lines;
    const auto append_new_tail            = [&]() -> AllLineIndex
    {
        const AllLineIndex first_new_index {static_cast<int>(_all_lines.size())};
        merge_log_batch(source_ranges, merged_lines);
        append_merged_lines(merged_lines);
        return first_new_index;
    };

    bool can_append_to_tail = true;
    std::optional<std::chrono::system_clock::time_point> min_new_timestamp;
    if (!_all_lines.empty())
    {
        const auto& last_line = _all_lines[AllLineIndex {static_cast<int>(_all_lines.size() - 1)}];
        if (last_line->metadata.timestamp.has_value())
        {
            min_new_timestamp = earliest_new_timestamp(source_ranges);
            if (min_new_timestamp.has_value() && min_new_timestamp.value() < last_line->metadata.timestamp.value())
            {
                can_append_to_tail = false;
            }
        }
    }

    if (can_append_to_tail)
    {
        return append_new_tail();
    }

    if (!min_new_timestamp.has_value())
    {
        return append_new_tail();
    }

    const std::size_t rewrite_start_index = find_rewrite_start_index(_all_lines, min_new_timestamp.value());
    if (rewrite_start_index >= _all_lines.size())
    {
        return append_new_tail();
    }

    std::vector<std::shared_ptr<LogEntry>> existing_suffix;
    existing_suffix.reserve(_all_lines.size() - rewrite_start_index);
    for (std::size_t line_index = rewrite_start_index; line_index < _all_lines.size(); ++line_index)
    {
        existing_suffix.push_back(_all_lines[AllLineIndex {static_cast<int>(line_index)}]);
    }

    std::vector<LogBatchSourceRange> rewrite_ranges;
    rewrite_ranges.reserve(source_ranges.size() + 1);
    rewrite_ranges.push_back({
        &existing_suffix,
        0,
        0,
        std::string(),
        true,
    });
    for (const auto& source_range : source_ranges)
    {
        rewrite_ranges.push_back(source_range);
    }

    merge_log_batch(rewrite_ranges, merged_lines);

    for (std::size_t merged_index = 0; merged_index < existing_suffix.size(); ++merged_index)
    {
        _all_lines[AllLineIndex {static_cast<int>(rewrite_start_index + merged_index)}] = merged_lines[merged_index];
    }

    for (std::size_t merged_index = existing_suffix.size(); merged_index < merged_lines.size(); ++merged_index)
    {
        _all_lines.push_back(merged_lines[merged_index]);
    }

    return AllLineIndex {static_cast<int>(rewrite_start_index)};
}

const IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex>& AllTrackedSources::all_lines() const
{
    return _all_lines;
}

int AllTrackedSources::line_count() const
{
    return static_cast<int>(_all_lines.size());
}

std::size_t AllTrackedSources::source_count() const
{
    return _sources.size();
}

bool AllTrackedSources::empty() const
{
    return _sources.empty();
}

std::vector<std::string> AllTrackedSources::source_labels() const
{
    std::vector<std::string> labels;
    labels.reserve(_sources.size());
    for (const auto& source : _sources)
    {
        labels.push_back(source->source_label());
    }

    return labels;
}

const std::vector<std::string>& AllTrackedSources::timestamp_formats() const
{
    return _timestamp_formats->formats();
}

std::shared_ptr<const TimestampFormatCatalog> AllTrackedSources::timestamp_format_catalog() const
{
    return _timestamp_formats;
}

std::optional<std::string> AllTrackedSources::set_source_timestamp_format(std::size_t source_index, const std::string& format)
{
    if (source_index >= _sources.size())
    {
        return "Invalid source selection";
    }

    if (format.empty())
    {
        return "Invalid timestamp format selection";
    }

    _sources[source_index]->set_timestamp_format(format);
    rebuild_all_lines();
    return std::nullopt;
}

bool AllTrackedSources::contains_source(const LogSource& candidate_source) const
{
    for (const auto& source : _sources)
    {
        if (same_source(source->source(), candidate_source))
        {
            return true;
        }
    }

    return false;
}

void AllTrackedSources::rebuild_source_labels()
{
    std::vector<LogSource> sources;
    sources.reserve(_sources.size());
    for (const auto& source : _sources)
    {
        sources.push_back(source->source());
    }

    const auto labels = build_source_labels(sources);
    for (std::size_t index = 0; index < _sources.size(); ++index)
    {
        _sources[index]->set_source_label(labels[index]);
    }
}

void AllTrackedSources::rebuild_all_lines()
{
    _all_lines.clear();

    std::vector<LogBatchSourceRange> source_ranges;
    source_ranges.reserve(_sources.size());
    for (std::size_t source_index = 0; source_index < _sources.size(); ++source_index)
    {
        append_source_range(source_ranges, *_sources[source_index], source_index, 0);
    }

    std::vector<std::shared_ptr<LogEntry>> merged_lines;
    merge_log_batch(source_ranges, merged_lines);
    append_merged_lines(merged_lines);
}

void AllTrackedSources::append_source_range(std::vector<LogBatchSourceRange>& source_ranges, const TrackedSourceBase& source, std::size_t source_index,
                                            std::size_t first_entry_index) const
{
    const auto& entries = source.entries();
    if (first_entry_index >= entries.size())
    {
        return;
    }

    source_ranges.push_back({
        &entries,
        first_entry_index,
        source_index,
        source.source_label(),
    });
}

void AllTrackedSources::append_merged_lines(const std::vector<std::shared_ptr<LogEntry>>& lines)
{
    _all_lines.reserve(_all_lines.size() + lines.size());
    for (const auto& line : lines)
    {
        _all_lines.push_back(line);
    }
}

} // namespace slayerlog
