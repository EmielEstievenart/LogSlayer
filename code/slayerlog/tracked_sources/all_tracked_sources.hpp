#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "log_batch.hpp"
#include "log_source.hpp"
#include "timestamp/source_timestamp_parser.hpp"
#include "log_types.hpp"

namespace slayerlog
{

class TrackedSourceBase;

class AllTrackedSources
{
public:
    explicit AllTrackedSources(std::shared_ptr<const TimestampFormatCatalog> timestamp_formats = default_timestamp_format_catalog());
    ~AllTrackedSources();

    std::optional<std::string> open_source(const LogSource& source);
    std::optional<std::string> close_source(std::size_t source_index, std::string* closed_label = nullptr);

    std::optional<AllLineIndex> poll();

    const IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex>& all_lines() const;
    int line_count() const;

    std::size_t source_count() const;
    bool empty() const;
    std::vector<std::string> source_labels() const;
    const std::vector<std::string>& timestamp_formats() const;
    std::optional<std::string> set_source_timestamp_format(std::size_t source_index, const std::string& format);

private:
    bool contains_source(const LogSource& candidate_source) const;
    void rebuild_source_labels();
    void rebuild_all_lines();
    void append_source_range(std::vector<LogBatchSourceRange>& source_ranges, const TrackedSourceBase& source, std::size_t source_index, std::size_t first_entry_index) const;
    void append_merged_lines(const std::vector<std::shared_ptr<LogEntry>>& lines);

    std::vector<std::unique_ptr<TrackedSourceBase>> _sources;
    IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex> _all_lines;
    std::shared_ptr<const TimestampFormatCatalog> _timestamp_formats;
};

} // namespace slayerlog
