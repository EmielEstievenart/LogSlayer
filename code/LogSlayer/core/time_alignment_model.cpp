#include "time_alignment_model.hpp"

#include <algorithm>
#include <utility>

#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/log_line.hpp"

namespace slayerlog
{

void TimeAlignmentModel::start(int first_selected_line, ApplyCallback apply_callback)
{
    _apply_callback = std::move(apply_callback);
    _phase          = Phase::SelectSource;
    _source.reset();
    _selected_line = std::max(0, first_selected_line);
    set_status("Select source line");
}

void TimeAlignmentModel::cancel()
{
    reset();
}

bool TimeAlignmentModel::active() const
{
    return _phase != Phase::Inactive;
}

std::optional<int> TimeAlignmentModel::selected_line() const
{
    return _selected_line;
}

const std::string& TimeAlignmentModel::status_text() const
{
    return _status;
}

bool TimeAlignmentModel::status_is_error() const
{
    return _status_is_error;
}

void TimeAlignmentModel::set_selected_line(const AllProcessedSources& processed_sources, int visible_line_index)
{
    if (processed_sources.line_count() <= 0)
    {
        _selected_line.reset();
        return;
    }

    _selected_line = std::clamp(visible_line_index, 0, processed_sources.line_count() - 1);
}

void TimeAlignmentModel::move_selection(const AllProcessedSources& processed_sources, int delta, int fallback_line)
{
    const int current_line = _selected_line.value_or(fallback_line);
    set_selected_line(processed_sources, current_line + delta);
}

bool TimeAlignmentModel::confirm_selection(const AllProcessedSources& processed_sources)
{
    const LogEntry* entry = selected_entry(processed_sources);
    if (entry == nullptr)
    {
        set_status("Select a visible log entry, not a collapsed summary row", true);
        return true;
    }

    if (_phase == Phase::SelectSource)
    {
        if (!entry->metadata.timestamp.has_value())
        {
            set_status("Source line has no original timestamp", true);
            return true;
        }

        _source = SourceSelection {
            entry->metadata.source_index,
            entry->metadata.source_label,
            *entry->metadata.timestamp,
        };
        _phase = Phase::SelectDestination;
        set_status("Source " + entry->metadata.source_label + " selected, select destination line");
        return true;
    }

    if (!_source.has_value())
    {
        set_status("No source line selected", true);
        _phase = Phase::SelectSource;
        return true;
    }

    const auto destination_timestamp = effective_timestamp(entry->metadata);
    if (!destination_timestamp.has_value())
    {
        set_status("Destination line has no timestamp", true);
        return true;
    }

    if (entry->metadata.source_index == _source->source_index)
    {
        set_status("Destination must be from a different source", true);
        return true;
    }

    if (!_apply_callback)
    {
        set_status("Time alignment handler is unavailable", true);
        return true;
    }

    LogEntry source_entry;
    source_entry.metadata.timestamp    = _source->timestamp;
    source_entry.metadata.source_index = _source->source_index;
    source_entry.metadata.source_label = _source->source_label;

    const TimeAlignmentApplyResult result = _apply_callback(source_entry, *entry);
    if (!result.success)
    {
        set_status(result.message, true);
        return true;
    }

    reset();
    return true;
}

void TimeAlignmentModel::reset()
{
    _phase = Phase::Inactive;
    _selected_line.reset();
    _source.reset();
    _status.clear();
    _status_is_error = false;
    _apply_callback  = {};
}

void TimeAlignmentModel::set_status(std::string message, bool is_error)
{
    _status          = std::move(message);
    _status_is_error = is_error;
}

const LogEntry* TimeAlignmentModel::selected_entry(const AllProcessedSources& processed_sources) const
{
    if (!_selected_line.has_value())
    {
        return nullptr;
    }

    const auto entry_index = processed_sources.entry_index_for_visible_line(VisibleLineIndex {*_selected_line});
    if (!entry_index.has_value())
    {
        return nullptr;
    }

    return &processed_sources.entry_at(*entry_index);
}

} // namespace slayerlog
