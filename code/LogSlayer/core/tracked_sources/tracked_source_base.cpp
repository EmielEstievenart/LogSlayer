#include "tracked_source_base.hpp"

#include <utility>

#include "log_batch.hpp"

namespace slayerlog
{

TrackedSourceBase::TrackedSourceBase(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats)
    : _source(std::move(source)), _source_label(std::move(source_label)), _timestamp_formats(std::move(timestamp_formats))
{
    if (_timestamp_formats == nullptr)
    {
        _timestamp_formats = default_timestamp_format_catalog();
    }
}

const LogSource& TrackedSourceBase::source() const
{
    return _source;
}

const std::string& TrackedSourceBase::source_label() const
{
    return _source_label;
}

void TrackedSourceBase::set_source_label(std::string source_label)
{
    _source_label = std::move(source_label);
}

const std::string& TrackedSourceBase::source_mnemonic() const
{
    return _source_mnemonic;
}

void TrackedSourceBase::set_source_mnemonic(std::string source_mnemonic)
{
    _source_mnemonic = std::move(source_mnemonic);
    update_mnemonic_prefix();
}

bool TrackedSourceBase::mnemonic_visible() const
{
    return _mnemonic_visible;
}

void TrackedSourceBase::set_mnemonic_visible(bool mnemonic_visible)
{
    _mnemonic_visible = mnemonic_visible;
    update_mnemonic_prefix();
}

const std::string& TrackedSourceBase::mnemonic_prefix() const
{
    return _mnemonic_prefix;
}

void TrackedSourceBase::update_mnemonic_prefix()
{
    if (!_mnemonic_visible || _source_mnemonic.empty())
    {
        _mnemonic_prefix.clear();
        return;
    }

    _mnemonic_prefix = _source_mnemonic + " ";
}

void TrackedSourceBase::set_timestamp_format(std::string format)
{
    set_timestamp_catalog(std::make_shared<const TimestampFormatCatalog>(std::vector<std::string> {format}));
    _pinned_timestamp_format = std::move(format);
}

const std::optional<std::string>& TrackedSourceBase::pinned_timestamp_format() const
{
    return _pinned_timestamp_format;
}

void TrackedSourceBase::reset_timestamp_format(std::shared_ptr<const TimestampFormatCatalog> timestamp_formats)
{
    set_timestamp_catalog(std::move(timestamp_formats));
    _pinned_timestamp_format.reset();
}

std::optional<std::string> TrackedSourceBase::set_timestamp_offset(LogTimestampOffset offset)
{
    const auto previous_offset = _timestamp_offset;
    _timestamp_offset          = offset;

    const auto error = apply_timestamp_offset_to_entries();
    if (error.has_value())
    {
        _timestamp_offset = previous_offset;
    }

    return error;
}

std::optional<std::string> TrackedSourceBase::adjust_timestamp_offset(LogTimestampOffset delta)
{
    const auto combined = add_offsets(_timestamp_offset.value_or(LogTimestampOffset {}), delta);
    if (!combined.has_value())
    {
        return "Timestamp offset would overflow";
    }

    return set_timestamp_offset(*combined);
}

void TrackedSourceBase::clear_timestamp_offset()
{
    _timestamp_offset.reset();
    for (const auto& entry : _entries)
    {
        entry->metadata.offset_timestamp.reset();
    }
}

const std::vector<std::shared_ptr<LogEntry>>& TrackedSourceBase::entries() const
{
    return _entries;
}

const std::shared_ptr<const TimestampFormatCatalog>& TrackedSourceBase::timestamp_formats() const
{
    return _timestamp_formats;
}

const std::optional<LogTimestampOffset>& TrackedSourceBase::timestamp_offset() const
{
    return _timestamp_offset;
}

void TrackedSourceBase::set_timestamp_formats(std::shared_ptr<const TimestampFormatCatalog> timestamp_formats)
{
    _timestamp_formats = std::move(timestamp_formats);
    if (_timestamp_formats == nullptr)
    {
        _timestamp_formats = default_timestamp_format_catalog();
    }
}

void TrackedSourceBase::store_timestamp_offset(std::optional<LogTimestampOffset> offset)
{
    _timestamp_offset = offset;
}

void TrackedSourceBase::reparse_entries(SourceTimestampParser& parser)
{
    parser = SourceTimestampParser();

    for (const auto& entry : _entries)
    {
        entry->metadata.timestamp.reset();
        entry->metadata.offset_timestamp.reset();
        entry->metadata.extracted_time_start.reset();
        entry->metadata.extracted_time_end.reset();
    }

    const auto catalog = timestamp_formats();
    if (catalog == nullptr || !parser.init(_entries, *catalog))
    {
        return;
    }

    for (const auto& entry : _entries)
    {
        parser.parse(*entry);
        (void)apply_timestamp_offset(*entry);
    }
}

std::optional<std::string> TrackedSourceBase::apply_timestamp_offset_to_entries()
{
    std::vector<std::optional<LogTimestamp>> offset_timestamps;
    offset_timestamps.reserve(_entries.size());

    for (const auto& entry : _entries)
    {
        if (!_timestamp_offset.has_value() || !entry->metadata.timestamp.has_value())
        {
            offset_timestamps.push_back(std::nullopt);
            continue;
        }

        const auto offset_timestamp = add_offset(*entry->metadata.timestamp, *_timestamp_offset);
        if (!offset_timestamp.has_value())
        {
            return "Timestamp offset would overflow";
        }

        offset_timestamps.push_back(*offset_timestamp);
    }

    for (std::size_t index = 0; index < _entries.size(); ++index)
    {
        _entries[index]->metadata.offset_timestamp = offset_timestamps[index];
    }

    return std::nullopt;
}

std::optional<std::string> TrackedSourceBase::apply_timestamp_offset(LogEntry& entry) const
{
    entry.metadata.offset_timestamp.reset();
    if (!_timestamp_offset.has_value() || !entry.metadata.timestamp.has_value())
    {
        return std::nullopt;
    }

    const auto offset_timestamp = add_offset(*entry.metadata.timestamp, *_timestamp_offset);
    if (!offset_timestamp.has_value())
    {
        return "Timestamp offset would overflow";
    }

    entry.metadata.offset_timestamp = *offset_timestamp;
    return std::nullopt;
}

void TrackedSourceBase::reserve_entries(std::size_t additional_count)
{
    _entries.reserve(_entries.size() + additional_count);
}

LogEntry& TrackedSourceBase::append_entry()
{
    auto entry_ptr                      = std::make_shared<LogEntry>();
    entry_ptr->metadata.sequence_number = _next_sequence_number++;
    entry_ptr->metadata.source          = this;
    _entries.push_back(entry_ptr);
    return *entry_ptr;
}

void TrackedSourceBase::append_merged_entries(const std::vector<LogBatchSourceRange>& source_ranges)
{
    const std::size_t first_new_entry_index = _entries.size();

    // Clone mode: the merged entries get this source's sequence numbers and source link
    // stamped below, which must not disturb the range-owned originals (folder children).
    merge_log_batch(source_ranges, _entries, MergeEntryMode::Clone);

    for (std::size_t entry_index = first_new_entry_index; entry_index < _entries.size(); ++entry_index)
    {
        _entries[entry_index]->metadata.sequence_number = _next_sequence_number++;
        _entries[entry_index]->metadata.source          = this;
    }
}

void TrackedSourceBase::replace_entries_with_merged_entries(const std::vector<LogBatchSourceRange>& source_ranges)
{
    _entries.clear();
    _next_sequence_number = 0;
    append_merged_entries(source_ranges);
}

} // namespace slayerlog
