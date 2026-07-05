#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "tracked_sources/log_line.hpp"

namespace slayerlog
{

struct LogBatchSourceRange
{
    const std::vector<std::shared_ptr<LogEntry>>* entries = nullptr;
    std::size_t first_entry_index                         = 0;
    std::size_t source_index                              = 0;
    bool preserve_source_metadata                         = false;
};

/// How merged lines relate to the range-owned entries they came from.
enum class MergeEntryMode
{
    /// Push the range-owned entry pointer itself, stamping source_index in place.
    /// The cheap mode for merged views that share entry storage with the sources.
    Share,
    /// Push a copy of each entry. For owners that re-stamp source/sequence metadata
    /// on the merged result without disturbing the originals (folder sources).
    Clone,
};

void merge_log_batch(const std::vector<LogBatchSourceRange>& source_ranges, std::vector<std::shared_ptr<LogEntry>>& merged_lines, MergeEntryMode entry_mode);
std::vector<std::shared_ptr<LogEntry>> merge_log_batch(const std::vector<LogBatchSourceRange>& source_ranges, MergeEntryMode entry_mode);
std::vector<std::shared_ptr<LogEntry>> merge_log_batch(const std::vector<std::shared_ptr<LogEntry>>& batch);
std::vector<LogEntry> merge_log_batch(const std::vector<LogEntry>& batch);

} // namespace slayerlog
