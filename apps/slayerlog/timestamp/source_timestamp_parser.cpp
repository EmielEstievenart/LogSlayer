#include "timestamp/source_timestamp_parser.hpp"

#include <iomanip>
#include <sstream>

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

std::string format_two_digits(unsigned value)
{
    std::ostringstream output;
    output << std::setw(2) << std::setfill('0') << value;
    return output.str();
}

std::string format_nine_digits(unsigned value)
{
    std::ostringstream output;
    output << std::setw(9) << std::setfill('0') << value;
    return output.str();
}

std::string trim_fraction_suffix(std::string text)
{
    while (!text.empty() && text.back() == '0')
    {
        text.pop_back();
    }

    return text;
}

std::string format_display_time(const DateAndTime& parsed)
{
    std::ostringstream output;
    output << std::setw(4) << std::setfill('0') << parsed.year << '-' << std::setw(2) << std::setfill('0') << parsed.month << '-' << std::setw(2) << std::setfill('0') << parsed.day << ' ' << std::setw(2) << std::setfill('0') << parsed.hour
           << ':' << std::setw(2) << std::setfill('0') << parsed.minute << ':' << std::setw(2) << std::setfill('0') << parsed.second;

    if (parsed.nanosecond != 0)
    {
        output << '.' << trim_fraction_suffix(format_nine_digits(parsed.nanosecond));
    }

    if (parsed.utc_offset_minutes.has_value())
    {
        const int total_minutes    = *parsed.utc_offset_minutes;
        const int absolute_minutes = std::abs(total_minutes);
        const int hours            = absolute_minutes / 60;
        const int minutes          = absolute_minutes % 60;
        output << (total_minutes >= 0 ? '+' : '-') << format_two_digits(static_cast<unsigned>(hours)) << ':' << format_two_digits(static_cast<unsigned>(minutes));
    }

    return output.str();
}

bool try_parse_with_format(const eestv::compiledDataAndTimeParser& parser, const std::string& line, int start_index, LogEntryMetadata& metadata)
{
    DateAndTime parsed;
    int end_index = 0;
    if (!apply_parser(parser, line, start_index, parsed, end_index))
    {
        return false;
    }

    const auto time_point = parsed.to_time_point();
    if (!time_point.has_value())
    {
        return false;
    }

    metadata.timestamp = *time_point;
    metadata.extracted_time_text = line.substr(static_cast<std::size_t>(start_index), static_cast<std::size_t>(end_index - start_index));
    metadata.parsed_time_text    = format_display_time(parsed);
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
