#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "tracked_sources/log_line.hpp"

namespace slayerlog
{

struct LogBatchSourceRange
{
    const std::vector<std::shared_ptr<LogEntry>>* entries = nullptr;
    std::size_t first_entry_index                          = 0;
    std::size_t source_index                               = 0;
    std::string source_label;
    bool preserve_source_metadata                          = false;
};

void merge_log_batch(const std::vector<LogBatchSourceRange>& source_ranges, std::vector<std::shared_ptr<LogEntry>>& merged_lines);
std::vector<std::shared_ptr<LogEntry>> merge_log_batch(const std::vector<LogBatchSourceRange>& source_ranges);
std::vector<std::shared_ptr<LogEntry>> merge_log_batch(const std::vector<std::shared_ptr<LogEntry>>& batch);
std::vector<LogEntry> merge_log_batch(const std::vector<LogEntry>& batch);

} // namespace slayerlog
