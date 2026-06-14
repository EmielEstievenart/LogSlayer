#include "align_time_session.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include "log_batch.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

namespace
{

constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;

LogTimestamp midpoint_timestamp(LogTimestamp first, LogTimestamp second)
{
    // first + (second - first) / 2, computed in nanoseconds to keep sub-second precision.
    const std::int64_t first_nanos  = first.epoch_seconds * nanoseconds_per_second + first.nanosecond;
    const std::int64_t second_nanos = second.epoch_seconds * nanoseconds_per_second + second.nanosecond;
    const std::int64_t midpoint     = first_nanos + (second_nanos - first_nanos) / 2;

    LogTimestamp result;
    result.epoch_seconds    = midpoint / nanoseconds_per_second;
    std::int64_t nanosecond = midpoint % nanoseconds_per_second;
    if (nanosecond < 0)
    {
        nanosecond += nanoseconds_per_second;
        --result.epoch_seconds;
    }

    result.nanosecond = static_cast<std::uint32_t>(nanosecond);
    return result;
}

} // namespace

AlignTimeSession::AlignTimeSession(const AllProcessedSources& processed_sources, std::size_t aligning_source_index) : _aligning_source_index(aligning_source_index)
{
    const int total_entries = processed_sources.total_line_count();
    for (int index = 0; index < total_entries; ++index)
    {
        const LogEntry& entry = processed_sources.entry_at(AllLineIndex {index});
        if (entry.metadata.source_index == aligning_source_index)
        {
            // The aligning source contributes all of its lines (filters are ignored here so the
            // line being aligned can never be hidden out from under the user).
            AligningEntry aligning_entry;
            aligning_entry.entry          = std::make_shared<LogEntry>(entry);
            aligning_entry.base_timestamp = effective_timestamp(entry.metadata);
            _aligning.push_back(std::move(aligning_entry));
            if (_aligning_source_label.empty())
            {
                _aligning_source_label = entry.metadata.source_label;
            }
        }
        else if (processed_sources.entry_matches_active_filters(entry))
        {
            _backdrop.push_back(std::make_shared<LogEntry>(entry));
        }
    }

    _show_original_time = processed_sources.show_original_time();

    if (_aligning.empty())
    {
        set_status("The selected source has no lines to align.", true);
    }
    else if (_backdrop.empty())
    {
        set_status("No other lines are visible to align against.", true);
    }
    else
    {
        _ready = true;
    }

    const int row_total              = static_cast<int>(_backdrop.size() + _aligning.size());
    _column_widths.line_number_width = std::max(1, static_cast<int>(std::to_string(row_total).size()));

    int timestamp_width = 0;
    for (const auto& entry : _backdrop)
    {
        timestamp_width = std::max(timestamp_width, log_entry_timestamp_field_width(*entry));
    }
    for (const auto& aligning_entry : _aligning)
    {
        timestamp_width = std::max(timestamp_width, log_entry_timestamp_field_width(*aligning_entry.entry));
    }
    _column_widths.timestamp_width = timestamp_width;

    apply_preview_offset_to_aligning();
    rebuild_merge();

    for (int row = 0; row < static_cast<int>(_merged.size()); ++row)
    {
        _widest_row_width = std::max(_widest_row_width, static_cast<int>(render_row(static_cast<std::size_t>(row)).size()));
    }

    if (_ready)
    {
        enter_phase(Phase::SelectRight);
        set_status("Select the line to align (Up/Down, then Enter).");
    }
}

bool AlignTimeSession::ready() const
{
    return _ready;
}

const std::string& AlignTimeSession::status_text() const
{
    return _status;
}

bool AlignTimeSession::status_is_error() const
{
    return _status_is_error;
}

std::size_t AlignTimeSession::aligning_source_index() const
{
    return _aligning_source_index;
}

const std::string& AlignTimeSession::aligning_source_label() const
{
    return _aligning_source_label;
}

std::size_t AlignTimeSession::row_count() const
{
    return _merged.size();
}

AlignTimeSession::RowKind AlignTimeSession::row_kind(std::size_t row) const
{
    return _merged[row]->metadata.source_index == _aligning_source_index ? RowKind::Aligning : RowKind::Backdrop;
}

std::string AlignTimeSession::render_row(std::size_t row) const
{
    return render_log_entry_line(*_merged[row], static_cast<int>(row) + 1, _column_widths, _show_original_time);
}

int AlignTimeSession::widest_row_width() const
{
    return _widest_row_width;
}

AlignTimeSession::Phase AlignTimeSession::phase() const
{
    return _phase;
}

std::optional<int> AlignTimeSession::cursor_row() const
{
    if (_phase == Phase::Nudge)
    {
        return right_selected_row();
    }

    if (_cursor_row < 0 || _cursor_row >= static_cast<int>(_merged.size()))
    {
        return std::nullopt;
    }

    return _cursor_row;
}

std::optional<int> AlignTimeSession::right_selected_row() const
{
    return row_of_entry(_right_selection);
}

std::vector<int> AlignTimeSession::left_selected_rows() const
{
    std::vector<int> rows;
    rows.reserve(_left_selections.size());
    for (const auto& selection : _left_selections)
    {
        if (const auto row = row_of_entry(selection))
        {
            rows.push_back(*row);
        }
    }

    return rows;
}

LogTimestampOffset AlignTimeSession::preview_offset() const
{
    return _preview_offset;
}

bool AlignTimeSession::can_commit() const
{
    return _ready && _phase == Phase::Nudge && _right_selection != nullptr;
}

void AlignTimeSession::move_cursor(int delta)
{
    if (!_ready || _phase == Phase::Nudge || delta == 0 || _merged.empty())
    {
        return;
    }

    const RowKind kind  = _phase == Phase::SelectRight ? RowKind::Aligning : RowKind::Backdrop;
    const int direction = delta > 0 ? 1 : -1;
    const int row_total = static_cast<int>(_merged.size());

    int row             = _cursor_row;
    int remaining_steps = std::abs(delta);
    while (remaining_steps > 0)
    {
        int next = row + direction;
        while (next >= 0 && next < row_total && row_kind(static_cast<std::size_t>(next)) != kind)
        {
            next += direction;
        }

        if (next < 0 || next >= row_total)
        {
            break;
        }

        row = next;
        --remaining_steps;
    }

    _cursor_row = row;
}

void AlignTimeSession::toggle_left_selection()
{
    if (_phase != Phase::SelectLeft || _cursor_row < 0 || _cursor_row >= static_cast<int>(_merged.size()))
    {
        return;
    }

    const auto entry = _merged[_cursor_row];
    if (entry->metadata.source_index == _aligning_source_index)
    {
        return;
    }

    const auto existing = std::find(_left_selections.begin(), _left_selections.end(), entry);
    if (existing != _left_selections.end())
    {
        _left_selections.erase(existing);
        set_status("Reference removed. Select one or two references, then Enter.");
        return;
    }

    if (_left_selections.size() >= 2)
    {
        set_status("Two references already selected; deselect one first.", true);
        return;
    }

    _left_selections.push_back(entry);
    set_status(_left_selections.size() == 1 ? "One reference selected. Add another or press Enter." : "Two references selected. Press Enter to nudge.");
}

bool AlignTimeSession::advance()
{
    if (!_ready)
    {
        return false;
    }

    switch (_phase)
    {
    case Phase::SelectRight:
    {
        if (_cursor_row < 0 || _cursor_row >= static_cast<int>(_merged.size()))
        {
            return false;
        }

        const auto entry = _merged[_cursor_row];
        if (entry->metadata.source_index != _aligning_source_index)
        {
            set_status("Select a line from the source being aligned.", true);
            return false;
        }

        std::optional<LogTimestamp> base;
        for (const auto& aligning_entry : _aligning)
        {
            if (aligning_entry.entry == entry)
            {
                base = aligning_entry.base_timestamp;
                break;
            }
        }

        if (!base.has_value())
        {
            set_status("That line has no timestamp to align.", true);
            return false;
        }

        _right_selection = entry;
        _right_base      = base;
        enter_phase(Phase::SelectLeft);
        set_status("Select one or two reference lines on the left (Space), then Enter.");
        return true;
    }
    case Phase::SelectLeft:
    {
        if (_left_selections.empty())
        {
            set_status("Select at least one reference line (Space).", true);
            return false;
        }

        if (!compute_coarse_offset())
        {
            return false;
        }

        apply_preview_offset_to_aligning();
        rebuild_merge();
        _phase = Phase::Nudge;
        set_status("Nudge with Up/Down. Enter to apply, Esc to cancel.");
        return true;
    }
    case Phase::Nudge:
        return false;
    }

    return false;
}

void AlignTimeSession::nudge(int steps)
{
    if (_phase != Phase::Nudge || steps == 0)
    {
        return;
    }

    const int count              = std::abs(steps);
    const LogTimestampOffset one = steps > 0 ? kNudgeStep : LogTimestampOffset {-kNudgeStep.seconds, -kNudgeStep.nanosecond};
    for (int applied = 0; applied < count; ++applied)
    {
        const auto combined = add_offsets(_preview_offset, one);
        if (!combined.has_value())
        {
            break;
        }

        _preview_offset = *combined;
    }

    apply_preview_offset_to_aligning();
    rebuild_merge();
}

bool AlignTimeSession::step_back()
{
    switch (_phase)
    {
    case Phase::SelectRight:
        return false;
    case Phase::SelectLeft:
        _left_selections.clear();
        _right_selection.reset();
        _right_base.reset();
        enter_phase(Phase::SelectRight);
        set_status("Select the line to align (Up/Down, then Enter).");
        return true;
    case Phase::Nudge:
        _preview_offset = LogTimestampOffset {0, 0};
        apply_preview_offset_to_aligning();
        rebuild_merge();
        enter_phase(Phase::SelectLeft);
        set_status("Select one or two reference lines on the left (Space), then Enter.");
        return true;
    }

    return false;
}

void AlignTimeSession::set_status(std::string message, bool is_error)
{
    _status          = std::move(message);
    _status_is_error = is_error;
}

void AlignTimeSession::enter_phase(Phase phase)
{
    _phase = phase;
    if (phase == Phase::SelectRight)
    {
        _cursor_row = first_row_of_kind(RowKind::Aligning).value_or(0);
    }
    else if (phase == Phase::SelectLeft)
    {
        _cursor_row = first_row_of_kind(RowKind::Backdrop).value_or(0);
    }
}

std::optional<int> AlignTimeSession::first_row_of_kind(RowKind kind) const
{
    for (int row = 0; row < static_cast<int>(_merged.size()); ++row)
    {
        if (row_kind(static_cast<std::size_t>(row)) == kind)
        {
            return row;
        }
    }

    return std::nullopt;
}

void AlignTimeSession::apply_preview_offset_to_aligning()
{
    for (auto& aligning_entry : _aligning)
    {
        if (!aligning_entry.base_timestamp.has_value())
        {
            aligning_entry.entry->metadata.offset_timestamp = std::nullopt;
            continue;
        }

        auto shifted = add_offset(*aligning_entry.base_timestamp, _preview_offset);
        // Fall back to the unshifted base on overflow so the row keeps a sort key.
        aligning_entry.entry->metadata.offset_timestamp = shifted.has_value() ? shifted : aligning_entry.base_timestamp;
    }
}

void AlignTimeSession::rebuild_merge()
{
    std::vector<std::shared_ptr<LogEntry>> batch;
    batch.reserve(_backdrop.size() + _aligning.size());
    batch.insert(batch.end(), _backdrop.begin(), _backdrop.end());
    for (const auto& aligning_entry : _aligning)
    {
        batch.push_back(aligning_entry.entry);
    }

    _merged = merge_log_batch(batch);
}

std::optional<int> AlignTimeSession::row_of_entry(const std::shared_ptr<LogEntry>& entry) const
{
    if (!entry)
    {
        return std::nullopt;
    }

    for (int row = 0; row < static_cast<int>(_merged.size()); ++row)
    {
        if (_merged[row] == entry)
        {
            return row;
        }
    }

    return std::nullopt;
}

bool AlignTimeSession::compute_coarse_offset()
{
    if (!_right_base.has_value())
    {
        set_status("The line to align has no timestamp.", true);
        return false;
    }

    std::vector<LogTimestamp> references;
    references.reserve(_left_selections.size());
    for (const auto& selection : _left_selections)
    {
        const auto timestamp = effective_timestamp(selection->metadata);
        if (!timestamp.has_value())
        {
            set_status("A selected reference line has no timestamp.", true);
            return false;
        }

        references.push_back(*timestamp);
    }

    if (references.empty())
    {
        set_status("Select at least one reference line.", true);
        return false;
    }

    const LogTimestamp target = references.size() == 1 ? references[0] : midpoint_timestamp(references[0], references[1]);
    const auto offset         = offset_between(*_right_base, target);
    if (!offset.has_value())
    {
        set_status("Cannot compute an offset for that pair of lines.", true);
        return false;
    }

    _preview_offset = *offset;
    return true;
}

} // namespace slayerlog
