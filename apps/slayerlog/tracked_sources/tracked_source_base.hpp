#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
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

    virtual bool poll() = 0;

    const std::vector<std::shared_ptr<LogEntry>>& entries() const;

protected:
    const std::shared_ptr<const TimestampFormatCatalog>& timestamp_formats() const;
    void reserve_entries(std::size_t additional_count);
    LogEntry& append_entry();
    void append_merged_entries(const std::vector<LogBatchSourceRange>& source_ranges);

private:
    LogSource _source;
    std::string _source_label;
    std::vector<std::shared_ptr<LogEntry>> _entries;
    std::uint64_t _next_sequence_number = 0;
    std::shared_ptr<const TimestampFormatCatalog> _timestamp_formats;
};

} // namespace slayerlog
