#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "timestamp/log_timestamp.hpp"

namespace slayerlog
{

class TrackedSourceBase;

struct LogEntryMetadata
{
    std::optional<LogTimestamp> timestamp;
    std::optional<LogTimestamp> offset_timestamp;
    std::optional<std::size_t> extracted_time_start;
    std::optional<std::size_t> extracted_time_end;
    std::uint64_t sequence_number = 0;
    std::size_t source_index      = 0;
    /// Fallback only: entries owned by a tracked source resolve their label live through
    /// @ref source (see entry_source_label()), so merges do not copy the label per entry.
    /// Set it directly only on entries that have no source object (tests, transient copies).
    std::string source_label;
    TrackedSourceBase* source = nullptr;

    LogEntryMetadata() = default;

    explicit LogEntryMetadata(std::optional<LogTimestamp> timestamp) : timestamp(std::move(timestamp)) { }
};

inline std::optional<LogTimestamp> effective_timestamp(const LogEntryMetadata& metadata)
{
    return metadata.offset_timestamp.has_value() ? metadata.offset_timestamp : metadata.timestamp;
}

struct LogEntry
{
    std::string text;
    LogEntryMetadata metadata;

    LogEntry() = default;

    LogEntry(std::string text, std::optional<LogTimestamp> timestamp = std::nullopt) : text(std::move(text)), metadata(std::move(timestamp)) { }

    LogEntry(std::string source_label, std::string text, std::optional<LogTimestamp> timestamp = std::nullopt) : LogEntry(std::move(text), std::move(timestamp)) { metadata.source_label = std::move(source_label); }

    LogEntry(std::size_t source_index, std::string source_label, std::string text, std::optional<LogTimestamp> timestamp = std::nullopt, std::uint64_t sequence_number = 0)
        : LogEntry(std::move(source_label), std::move(text), std::move(timestamp))
    {
        metadata.source_index    = source_index;
        metadata.sequence_number = sequence_number;
    }
};

/// The slice of the entry text the timestamp was parsed from; empty when no timestamp was
/// extracted. A view into entry.text — do not keep it beyond the entry's lifetime.
inline std::string_view extracted_time_view(const LogEntry& entry)
{
    if (!entry.metadata.extracted_time_start.has_value() || !entry.metadata.extracted_time_end.has_value())
    {
        return {};
    }

    const std::size_t start = *entry.metadata.extracted_time_start;
    const std::size_t end   = *entry.metadata.extracted_time_end;
    if (start >= end || end > entry.text.size())
    {
        return {};
    }

    return std::string_view(entry.text).substr(start, end - start);
}

} // namespace slayerlog
