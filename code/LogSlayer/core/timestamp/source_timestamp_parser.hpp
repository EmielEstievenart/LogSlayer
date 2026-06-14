#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "eestv/timestamp/timestamp_parser.hpp"
#include "timestamp_format_catalog.hpp"
#include "tracked_sources/log_line.hpp"

namespace slayerlog
{

class SourceTimestampParser
{
public:
    bool init(const LogEntry& line, const TimestampFormatCatalog& catalog);
    bool parse(LogEntry& line);

private:
    std::optional<eestv::compiledDataAndTimeParser> _compiled_parser;
    std::optional<std::size_t> _detected_start_index_slot;
};

} // namespace slayerlog
