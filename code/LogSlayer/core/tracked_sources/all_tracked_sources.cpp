#include "all_tracked_sources.hpp"

#include <cstddef>
#include <exception>
#include <memory>
#include <algorithm>
#include <utility>

#include "debug_log.hpp"
#include "log_entry_presentation.hpp"
#include "tracked_source_base.hpp"

namespace slayerlog
{

namespace
{

std::optional<LogTimestamp> earliest_new_timestamp(const std::vector<LogBatchSourceRange>& source_ranges)
{
    std::optional<LogTimestamp> earliest_timestamp;
    for (const auto& source_range : source_ranges)
    {
        if (source_range.entries == nullptr || source_range.first_entry_index >= source_range.entries->size())
        {
            continue;
        }

        const auto& entry    = (*source_range.entries)[source_range.first_entry_index];
        const auto timestamp = effective_timestamp(entry->metadata);
        if (!timestamp.has_value())
        {
            continue;
        }

        if (!earliest_timestamp.has_value() || timestamp.value() < earliest_timestamp.value())
        {
            earliest_timestamp = timestamp;
        }
    }

    return earliest_timestamp;
}

std::size_t find_rewrite_start_index(const IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex>& all_lines, LogTimestamp earliest_timestamp)
{
    for (std::size_t line_index = 0; line_index < all_lines.size(); ++line_index)
    {
        const auto& line     = all_lines[AllLineIndex {static_cast<int>(line_index)}];
        const auto timestamp = effective_timestamp(line->metadata);
        if (!timestamp.has_value())
        {
            continue;
        }

        if (timestamp.value() >= earliest_timestamp)
        {
            return line_index;
        }
    }

    return all_lines.size();
}

int rebuild_progress_percent(std::size_t completed_step_count, std::size_t total_step_count)
{
    if (total_step_count == 0)
    {
        return 100;
    }

    return static_cast<int>((completed_step_count * 100) / total_step_count);
}

Notification rebuild_progress_notification(int percent, std::string message)
{
    return make_progress_notification("Rebuilding log lines", std::move(message), static_cast<float>(percent) / 100.0F);
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

std::optional<std::string> AllTrackedSources::add_opened_source(std::unique_ptr<TrackedSourceBase> source_state)
{
    if (source_state == nullptr)
    {
        return "Opened source is invalid";
    }

    if (is_source_open(source_state->source()))
    {
        return "Source already open: " + source_display_path(source_state->source());
    }

    const std::size_t source_index = _sources.size();
    const std::string display_path = source_display_path(source_state->source());

    _sources.push_back(std::move(source_state));
    rebuild_source_labels();
    update_mnemonic_visibility();
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
    update_mnemonic_visibility();
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
    const auto append_new_tail = [&]() -> AllLineIndex
    {
        const AllLineIndex first_new_index {static_cast<int>(_all_lines.size())};
        merge_log_batch(source_ranges, merged_lines);
        append_merged_lines(merged_lines);
        return first_new_index;
    };

    bool can_append_to_tail = true;
    std::optional<LogTimestamp> min_new_timestamp;
    if (!_all_lines.empty())
    {
        const auto& last_line     = _all_lines[AllLineIndex {static_cast<int>(_all_lines.size() - 1)}];
        const auto last_timestamp = effective_timestamp(last_line->metadata);
        if (last_timestamp.has_value())
        {
            min_new_timestamp = earliest_new_timestamp(source_ranges);
            if (min_new_timestamp.has_value() && min_new_timestamp.value() < last_timestamp.value())
            {
                can_append_to_tail = false;
            }
        }
    }

    if (can_append_to_tail)
    {
        const auto first_changed_line = append_new_tail();
        return first_changed_line;
    }

    if (!min_new_timestamp.has_value())
    {
        const auto first_changed_line = append_new_tail();
        return first_changed_line;
    }

    const std::size_t rewrite_start_index = find_rewrite_start_index(_all_lines, min_new_timestamp.value());
    if (rewrite_start_index >= _all_lines.size())
    {
        const auto first_changed_line = append_new_tail();
        return first_changed_line;
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
        update_widest_line_width(merged_lines[merged_index]);
    }

    for (std::size_t merged_index = existing_suffix.size(); merged_index < merged_lines.size(); ++merged_index)
    {
        _all_lines.push_back(merged_lines[merged_index]);
        update_widest_line_width(merged_lines[merged_index]);
    }

    const AllLineIndex first_changed_line {static_cast<int>(rewrite_start_index)};
    return first_changed_line;
}

const IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex>& AllTrackedSources::all_lines() const
{
    return _all_lines;
}

int AllTrackedSources::line_count() const
{
    return static_cast<int>(_all_lines.size());
}

int AllTrackedSources::widest_line_width() const
{
    return _widest_line_width;
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

std::vector<std::string> AllTrackedSources::source_mnemonics() const
{
    std::vector<std::string> mnemonics;
    mnemonics.reserve(_sources.size());
    for (const auto& source : _sources)
    {
        mnemonics.push_back(source->source_mnemonic());
    }

    return mnemonics;
}

std::vector<std::string> AllTrackedSources::source_display_labels() const
{
    std::vector<std::string> display_labels;
    display_labels.reserve(_sources.size());
    for (const auto& source : _sources)
    {
        const std::string& mnemonic = source->source_mnemonic();
        if (mnemonic.empty() || !source->mnemonic_visible())
        {
            display_labels.push_back(source->source_label());
        }
        else
        {
            display_labels.push_back(mnemonic + " \xe2\x80\x94 " + source->source_label());
        }
    }

    return display_labels;
}

const std::vector<std::string>& AllTrackedSources::timestamp_formats() const
{
    return _timestamp_formats->formats();
}

std::shared_ptr<const TimestampFormatCatalog> AllTrackedSources::timestamp_format_catalog() const
{
    return _timestamp_formats;
}

void AllTrackedSources::set_notifier(Notifier notifier)
{
    _notifier = std::move(notifier);
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

std::optional<std::string> AllTrackedSources::reset_source_timestamp_format(std::size_t source_index)
{
    if (source_index >= _sources.size())
    {
        return "Invalid source selection";
    }

    _sources[source_index]->set_timestamp_catalog(_timestamp_formats);
    rebuild_all_lines();
    return std::nullopt;
}

std::optional<std::string> AllTrackedSources::set_source_timestamp_offset(std::size_t source_index, LogTimestampOffset offset)
{
    if (source_index >= _sources.size())
    {
        return "Invalid source selection";
    }

    const auto error = _sources[source_index]->set_timestamp_offset(offset);
    if (error.has_value())
    {
        return error;
    }

    rebuild_all_lines();
    return std::nullopt;
}

std::optional<std::string> AllTrackedSources::adjust_source_timestamp_offset(std::size_t source_index, LogTimestampOffset delta)
{
    if (source_index >= _sources.size())
    {
        return "Invalid source selection";
    }

    const auto error = _sources[source_index]->adjust_timestamp_offset(delta);
    if (error.has_value())
    {
        return error;
    }

    rebuild_all_lines();
    return std::nullopt;
}

std::optional<LogTimestampOffset> AllTrackedSources::source_timestamp_offset(std::size_t source_index) const
{
    if (source_index >= _sources.size())
    {
        return std::nullopt;
    }

    return _sources[source_index]->timestamp_offset();
}

std::optional<std::string> AllTrackedSources::clear_source_timestamp_offset(std::size_t source_index)
{
    if (source_index >= _sources.size())
    {
        return "Invalid source selection";
    }

    _sources[source_index]->clear_timestamp_offset();
    rebuild_all_lines();
    return std::nullopt;
}

bool AllTrackedSources::is_source_open(const LogSource& candidate_source) const
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

    // Labels (deduplicated basenames) are recomputed across all sources on every open/close.
    // Mnemonics are intentionally left untouched here; the factory assigns them once at adoption.
    const auto labels = build_source_labels(sources);
    for (std::size_t index = 0; index < _sources.size(); ++index)
    {
        _sources[index]->set_source_label(labels[index]);
    }
}

void AllTrackedSources::update_mnemonic_visibility()
{
    // A mnemonic only disambiguates interleaved sources, so it stays hidden until a second
    // source is open. Flipping visibility changes presentation only; entries remain raw.
    const bool show_mnemonics = _sources.size() > 1;
    for (const auto& source : _sources)
    {
        source->set_mnemonic_visible(show_mnemonics);
    }
}

void AllTrackedSources::rebuild_all_lines()
{
    _all_lines.clear();
    _widest_line_width = 0;

    // Callers hold the model mutex, and the terminal UI cannot paint while it is
    // held (the log view renders from the model under the same mutex), so only
    // the final state of this notification is visible there. The intermediate
    // updates are still reported for UIs that can paint concurrently.
    const std::size_t total_step_count = _sources.size() + 1;
    NotificationHandle progress_notification(_notifier);
    int last_reported_percent = 0;
    progress_notification.show_or_update(rebuild_progress_notification(last_reported_percent, "0% rebuilt"));

    std::vector<LogBatchSourceRange> source_ranges;
    source_ranges.reserve(_sources.size());
    for (std::size_t source_index = 0; source_index < _sources.size(); ++source_index)
    {
        append_source_range(source_ranges, *_sources[source_index], source_index, 0);
        const std::size_t completed_step_count = source_index + 1;
        const int percent                      = rebuild_progress_percent(completed_step_count, total_step_count);
        if (percent != last_reported_percent)
        {
            last_reported_percent = percent;
            progress_notification.show_or_update(rebuild_progress_notification(percent, std::to_string(percent) + "% rebuilt"));
        }
    }

    std::vector<std::shared_ptr<LogEntry>> merged_lines;
    merge_log_batch(source_ranges, merged_lines);
    append_merged_lines(merged_lines);

    progress_notification.show_or_update(rebuild_progress_notification(100, "100% rebuilt (" + std::to_string(merged_lines.size()) + " log lines)"));
}

void AllTrackedSources::update_widest_line_width(const std::shared_ptr<LogEntry>& line)
{
    if (line != nullptr)
    {
        _widest_line_width = std::max(_widest_line_width, static_cast<int>(presented_prefix(*line).size() + line->text.size()));
    }
}

void AllTrackedSources::append_source_range(std::vector<LogBatchSourceRange>& source_ranges, const TrackedSourceBase& source, std::size_t source_index, std::size_t first_entry_index) const
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
        update_widest_line_width(line);
    }
}

} // namespace slayerlog
