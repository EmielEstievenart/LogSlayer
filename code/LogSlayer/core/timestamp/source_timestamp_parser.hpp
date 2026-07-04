#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "timestamp_format_catalog.hpp"
#include "tracked_sources/log_line.hpp"

namespace eestv
{
struct CompiledDateAndTimeParser;
}

namespace slayerlog
{

/// Detects and applies one timestamp format for a single log source.
///
/// init() probes lines against every catalog format and locks onto the winning
/// (format, token position) pair; parse() then applies only that pair to each line.
/// Detection is a majority vote over the first few lines that match anything, so one
/// stray line with an embedded timestamp (a banner, say) cannot poison the source.
class SourceTimestampParser
{
public:
    /// Probes a single line; equivalent to a one-line vote. Returns true once initialized.
    bool init(const LogEntry& line, const TimestampFormatCatalog& catalog);

    /// Probes a batch of raw lines.
    bool init(const std::vector<std::string>& lines, const TimestampFormatCatalog& catalog);

    /// Probes a batch of existing entries.
    bool init(const std::vector<std::shared_ptr<LogEntry>>& entries, const TimestampFormatCatalog& catalog);

    bool initialized() const;

    /// Applies the detected format at the detected token position, filling the line's
    /// timestamp metadata. Returns false when the line carries no timestamp there.
    bool parse(LogEntry& line) const;

private:
    bool init_from_lines(const std::function<std::string_view(std::size_t)>& line_at, std::size_t line_count, const TimestampFormatCatalog& catalog);

    std::shared_ptr<const eestv::CompiledDateAndTimeParser> _compiled_parser;
    std::optional<std::size_t> _detected_start_index_slot;
};

} // namespace slayerlog
