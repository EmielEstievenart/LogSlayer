#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace slayerlog
{

struct LogTimestamp
{
    std::int64_t epoch_seconds = 0;
    std::uint32_t nanosecond   = 0;
};

struct LogTimestampOffset
{
    std::int64_t seconds    = 0;
    std::int32_t nanosecond = 0;
};

struct LogCivilTime
{
    int year            = 1970;
    unsigned month      = 1;
    unsigned day        = 1;
    unsigned hour       = 0;
    unsigned minute     = 0;
    unsigned second     = 0;
    unsigned nanosecond = 0;
};

bool operator==(LogTimestamp lhs, LogTimestamp rhs);
bool operator!=(LogTimestamp lhs, LogTimestamp rhs);
bool operator<(LogTimestamp lhs, LogTimestamp rhs);
bool operator<=(LogTimestamp lhs, LogTimestamp rhs);
bool operator>(LogTimestamp lhs, LogTimestamp rhs);
bool operator>=(LogTimestamp lhs, LogTimestamp rhs);

std::optional<LogTimestamp> make_log_timestamp_utc(int year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second, unsigned nanosecond, std::optional<int> utc_offset_minutes = std::nullopt);
LogCivilTime to_utc_civil_time(LogTimestamp timestamp);
std::string format_log_timestamp_utc(LogTimestamp timestamp);
std::optional<LogTimestampOffset> parse_log_timestamp_offset(std::string_view text);
std::string format_log_timestamp_offset(LogTimestampOffset offset);
std::optional<LogTimestamp> add_offset(LogTimestamp timestamp, LogTimestampOffset offset);
std::optional<LogTimestampOffset> offset_between(LogTimestamp from, LogTimestamp to);
std::optional<LogTimestampOffset> add_offsets(LogTimestampOffset lhs, LogTimestampOffset rhs);

} // namespace slayerlog
