#include "tracked_source_base.hpp"

#include <utility>

#include "log_batch.hpp"

namespace slayerlog
{

namespace
{

// Shifts a single extracted-timestamp offset that lives inside entry.text by @p delta.
void shift_offset(std::optional<std::size_t>& offset, std::ptrdiff_t delta)
{
    if (offset.has_value())
    {
        *offset = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(*offset) + delta);
    }
}

// Removes @p prefix from the front of an entry whose text currently carries it, moving the
// extracted-timestamp offsets back to their raw (prefix-free) positions.
void strip_prefix_from_entry(LogEntry& entry, const std::string& prefix)
{
    if (prefix.empty() || entry.text.compare(0, prefix.size(), prefix) != 0)
    {
        return;
    }

    entry.text.erase(0, prefix.size());
    shift_offset(entry.metadata.extracted_time_start, -static_cast<std::ptrdiff_t>(prefix.size()));
    shift_offset(entry.metadata.extracted_time_end, -static_cast<std::ptrdiff_t>(prefix.size()));
}

// Prepends @p prefix to a raw entry, moving the extracted-timestamp offsets forward to match.
void prepend_prefix_to_entry(LogEntry& entry, const std::string& prefix)
{
    if (prefix.empty())
    {
        return;
    }

    entry.text.insert(0, prefix);
    shift_offset(entry.metadata.extracted_time_start, static_cast<std::ptrdiff_t>(prefix.size()));
    shift_offset(entry.metadata.extracted_time_end, static_cast<std::ptrdiff_t>(prefix.size()));
}

} // namespace

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
    const std::string old_prefix = mnemonic_prefix();
    _source_mnemonic             = std::move(source_mnemonic);
    const std::string new_prefix = mnemonic_prefix();
    rewrite_prefix_on_entries(old_prefix, new_prefix);
}

bool TrackedSourceBase::mnemonic_visible() const
{
    return _mnemonic_visible;
}

void TrackedSourceBase::set_mnemonic_visible(bool mnemonic_visible)
{
    if (_mnemonic_visible == mnemonic_visible)
    {
        return;
    }

    const std::string old_prefix = mnemonic_prefix();
    _mnemonic_visible            = mnemonic_visible;
    const std::string new_prefix = mnemonic_prefix();
    rewrite_prefix_on_entries(old_prefix, new_prefix);
}

std::string TrackedSourceBase::mnemonic_prefix() const
{
    if (!_mnemonic_visible || _source_mnemonic.empty())
    {
        return {};
    }

    return _source_mnemonic + " ";
}

void TrackedSourceBase::rewrite_prefix_on_entries(const std::string& old_prefix, const std::string& new_prefix)
{
    if (old_prefix == new_prefix)
    {
        return;
    }

    // Peel off the old prefix (if any) and stamp on the new one, keeping the extracted-timestamp
    // offsets aligned with the text.
    for (const auto& entry : _entries)
    {
        strip_prefix_from_entry(*entry, old_prefix);
        prepend_prefix_to_entry(*entry, new_prefix);
    }
}

void TrackedSourceBase::inject_mnemonic_prefix(LogEntry& entry) const
{
    prepend_prefix_to_entry(entry, mnemonic_prefix());
}

std::optional<std::string> TrackedSourceBase::set_timestamp_offset(LogTimestampOffset offset)
{
    _timestamp_offset = offset;
    return apply_timestamp_offset_to_entries();
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

void TrackedSourceBase::reparse_entries(SourceTimestampParser& parser, bool& parser_initialized)
{
    parser             = SourceTimestampParser();
    parser_initialized = false;

    // The timestamp parser locks onto a token position, so it must only ever see raw lines.
    // Peel the embedded mnemonic prefix off before reparsing, then stamp it back on so the
    // freshly computed extracted-timestamp offsets land in the prefixed coordinate space.
    const std::string prefix = mnemonic_prefix();

    const auto catalog = timestamp_formats();
    for (const auto& entry : _entries)
    {
        strip_prefix_from_entry(*entry, prefix);

        entry->metadata.timestamp.reset();
        entry->metadata.offset_timestamp.reset();
        entry->metadata.extracted_time_text.clear();
        entry->metadata.extracted_time_start.reset();
        entry->metadata.extracted_time_end.reset();

        if (catalog != nullptr)
        {
            if (!parser_initialized)
            {
                parser_initialized = parser.init(*entry, *catalog);
            }

            if (parser_initialized)
            {
                parser.parse(*entry);
                (void)apply_timestamp_offset(*entry);
            }
        }

        prepend_prefix_to_entry(*entry, prefix);
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
    merge_log_batch(source_ranges, _entries);

    // Merged entries arrive as raw (prefix-free) clones, so embed the mnemonic prefix here.
    for (std::size_t entry_index = first_new_entry_index; entry_index < _entries.size(); ++entry_index)
    {
        _entries[entry_index]->metadata.sequence_number = _next_sequence_number++;
        _entries[entry_index]->metadata.source          = this;
        inject_mnemonic_prefix(*_entries[entry_index]);
    }
}

void TrackedSourceBase::replace_entries_with_merged_entries(const std::vector<LogBatchSourceRange>& source_ranges)
{
    _entries.clear();
    _next_sequence_number = 0;
    append_merged_entries(source_ranges);
}

} // namespace slayerlog
