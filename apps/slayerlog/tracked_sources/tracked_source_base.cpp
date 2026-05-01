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

const std::vector<std::shared_ptr<LogEntry>>& TrackedSourceBase::entries() const
{
    return _entries;
}

const std::shared_ptr<const TimestampFormatCatalog>& TrackedSourceBase::timestamp_formats() const
{
    return _timestamp_formats;
}

void TrackedSourceBase::reserve_entries(std::size_t additional_count)
{
    _entries.reserve(_entries.size() + additional_count);
}

LogEntry& TrackedSourceBase::append_entry()
{
    auto entry_ptr = std::make_shared<LogEntry>();
    entry_ptr->metadata.sequence_number = _next_sequence_number++;
    entry_ptr->metadata.source          = this;
    _entries.push_back(entry_ptr);
    return *entry_ptr;
}

void TrackedSourceBase::append_merged_entries(const std::vector<LogBatchSourceRange>& source_ranges)
{
    const std::size_t first_new_entry_index = _entries.size();
    merge_log_batch(source_ranges, _entries);

    for (std::size_t entry_index = first_new_entry_index; entry_index < _entries.size(); ++entry_index)
    {
        _entries[entry_index]->metadata.sequence_number = _next_sequence_number++;
        _entries[entry_index]->metadata.source          = this;
    }
}

} // namespace slayerlog
