#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "log_source.hpp"
#include "log_line.hpp"
#include "timestamp/source_timestamp_parser.hpp"

namespace slayerlog
{

struct LogBatchSourceRange;

class TrackedSourceBase
{
public:
    TrackedSourceBase(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats = default_timestamp_format_catalog());
    virtual ~TrackedSourceBase() = default;

    const LogSource& source() const;
    const std::string& source_label() const;
    void set_source_label(std::string source_label);
    virtual void set_timestamp_format(std::string format) = 0;
    virtual std::optional<std::string> set_timestamp_offset(LogTimestampOffset offset);
    virtual void clear_timestamp_offset();

    virtual bool poll() = 0;

    const std::vector<std::shared_ptr<LogEntry>>& entries() const;

protected:
    const std::shared_ptr<const TimestampFormatCatalog>& timestamp_formats() const;
    const std::optional<LogTimestampOffset>& timestamp_offset() const;
    void set_timestamp_formats(std::shared_ptr<const TimestampFormatCatalog> timestamp_formats);
    void reparse_entries(SourceTimestampParser& parser, bool& parser_initialized);
    std::optional<std::string> apply_timestamp_offset_to_entries();
    std::optional<std::string> apply_timestamp_offset(LogEntry& entry) const;
    void reserve_entries(std::size_t additional_count);
    LogEntry& append_entry();
    void append_merged_entries(const std::vector<LogBatchSourceRange>& source_ranges);
    void replace_entries_with_merged_entries(const std::vector<LogBatchSourceRange>& source_ranges);

private:
    LogSource _source;
    std::string _source_label;
    std::vector<std::shared_ptr<LogEntry>> _entries;
    std::uint64_t _next_sequence_number = 0;
    std::shared_ptr<const TimestampFormatCatalog> _timestamp_formats;
    std::optional<LogTimestampOffset> _timestamp_offset;
};

} // namespace slayerlog
