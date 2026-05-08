#include "timestamp/source_timestamp_parser.hpp"

namespace slayerlog
{

namespace
{

using eestv::DateAndTime;
using eestv::TimestampParser;

bool apply_parser(const eestv::compiledDataAndTimeParser& parser, const std::string& input, int start_index, DateAndTime& output, int& end_index)
{
    std::string to_parse = input;
    int index            = start_index;

    for (const auto& step : parser.dateParser)
    {
        int index_jump = 0;
        if (!step(to_parse, index, index_jump, output))
        {
            return false;
        }

        index += index_jump;
    }

    end_index = index;
    return true;
}

bool try_parse_with_format(const eestv::compiledDataAndTimeParser& parser, const std::string& line, int start_index, LogEntryMetadata& metadata)
{
    DateAndTime parsed;
    int end_index = 0;
    if (!apply_parser(parser, line, start_index, parsed, end_index))
    {
        return false;
    }

    const auto timestamp = make_log_timestamp_utc(parsed.year, parsed.month, parsed.day, parsed.hour, parsed.minute, parsed.second, parsed.nanosecond, parsed.utc_offset_minutes);
    if (!timestamp.has_value())
    {
        return false;
    }

    metadata.timestamp = *timestamp;
    metadata.extracted_time_text = line.substr(static_cast<std::size_t>(start_index), static_cast<std::size_t>(end_index - start_index));
    metadata.extracted_time_start = static_cast<std::size_t>(start_index);
    metadata.extracted_time_end   = static_cast<std::size_t>(end_index);
    return true;
}

const eestv::compiledDataAndTimeParser& compiled_parser_from_entry(const TimestampFormatCatalog::Entry& entry)
{
    return *entry.compiled_parser;
}

} // namespace

bool SourceTimestampParser::init(const LogEntry& line, const TimestampFormatCatalog& catalog)
{
    if (_compiled_parser.has_value() && _detected_start_index_slot.has_value())
    {
        return true;
    }

    const auto start_indices = TimestampParser::possible_parse_start_indices(line.text);
    if (start_indices.empty())
    {
        return false;
    }

    for (std::size_t start_slot = 0; start_slot < start_indices.size(); ++start_slot)
    {
        const int start_index = start_indices[start_slot];
        for (const auto& entry : catalog.entries())
        {
            const auto& parser = compiled_parser_from_entry(entry);
            LogEntryMetadata parsed_metadata;
            if (!try_parse_with_format(parser, line.text, start_index, parsed_metadata))
            {
                continue;
            }

            _compiled_parser           = parser;
            _detected_start_index_slot = start_slot;
            return true;
        }
    }

    return false;
}

bool SourceTimestampParser::parse(LogEntry& line)
{
    if (!_compiled_parser.has_value() || !_detected_start_index_slot.has_value())
    {
        return false;
    }

    const auto start_indices = TimestampParser::possible_parse_start_indices(line.text, static_cast<int>(*_detected_start_index_slot) + 1);
    if (start_indices.empty() || *_detected_start_index_slot >= start_indices.size())
    {
        return false;
    }

    return try_parse_with_format(*_compiled_parser, line.text, start_indices[*_detected_start_index_slot], line.metadata);
}

} // namespace slayerlog
