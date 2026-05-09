#include "timestamp/log_timestamp.hpp"

#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "eestv/timestamp/timestamp_parser.hpp"

namespace slayerlog
{

namespace
{

constexpr std::int64_t seconds_per_day = 86400;
constexpr std::int64_t days_to_unix_epoch = 719468;
constexpr std::int64_t nanoseconds_per_second = 1000000000;

struct OffsetParserEntry
{
    eestv::compiledDataAndTimeParser parser;
    unsigned fraction_digits = 0;
};

std::string_view trim_view(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }

    return text.substr(start, end - start);
}

bool apply_parser(const eestv::compiledDataAndTimeParser& parser, std::string input, eestv::DateAndTime& output)
{
    int index = 0;
    for (const auto& step : parser.dateParser)
    {
        int index_jump = 0;
        if (!step(input, index, index_jump, output))
        {
            return false;
        }

        index += index_jump;
    }

    return index == static_cast<int>(input.size());
}

std::vector<OffsetParserEntry> build_offset_parsers()
{
    eestv::TimestampParser parser;
    std::vector<OffsetParserEntry> parsers;
    parsers.push_back({parser.CompileFormat("YY hh:mm:ss"), 0});

    std::string fraction_format = "YY hh:mm:ss.";
    for (unsigned digits = 1; digits <= 9; ++digits)
    {
        fraction_format.push_back('f');
        parsers.push_back({parser.CompileFormat(fraction_format), digits});
    }

    return parsers;
}

const std::vector<OffsetParserEntry>& offset_parsers()
{
    static const std::vector<OffsetParserEntry> parsers = build_offset_parsers();
    return parsers;
}

bool add_overflows(std::int64_t lhs, std::int64_t rhs, std::int64_t& output)
{
    if ((rhs > 0 && lhs > (std::numeric_limits<std::int64_t>::max)() - rhs) || (rhs < 0 && lhs < (std::numeric_limits<std::int64_t>::min)() - rhs))
    {
        return true;
    }

    output = lhs + rhs;
    return false;
}

bool is_leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

unsigned max_day_in_month(int year, unsigned month)
{
    switch (month)
    {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
        return 31;
    case 4:
    case 6:
    case 9:
    case 11:
        return 30;
    case 2:
        return is_leap_year(year) ? 29 : 28;
    default:
        return 0;
    }
}

std::int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned shifted_month = static_cast<unsigned>(static_cast<int>(month) + (month > 2 ? -3 : 9));
    const unsigned day_of_year = (153 * shifted_month + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + static_cast<std::int64_t>(day_of_era) - days_to_unix_epoch;
}

LogCivilTime civil_from_days(std::int64_t days)
{
    days += days_to_unix_epoch;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned day_of_era = static_cast<unsigned>(days - era * 146097);
    const unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int year = static_cast<int>(year_of_era) + static_cast<int>(era * 400);
    const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned month_prime = (5 * day_of_year + 2) / 153;
    const unsigned day = day_of_year - (153 * month_prime + 2) / 5 + 1;
    const unsigned month = static_cast<unsigned>(static_cast<int>(month_prime) + (month_prime < 10 ? 3 : -9));
    year += month <= 2 ? 1 : 0;

    LogCivilTime civil;
    civil.year = year;
    civil.month = month;
    civil.day = day;
    return civil;
}

std::int64_t floor_div(std::int64_t lhs, std::int64_t rhs)
{
    const std::int64_t quotient = lhs / rhs;
    const std::int64_t remainder = lhs % rhs;
    return remainder < 0 ? quotient - 1 : quotient;
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

std::string format_fraction(unsigned nanosecond)
{
    return trim_fraction_suffix(format_nine_digits(nanosecond));
}

} // namespace

bool operator==(LogTimestamp lhs, LogTimestamp rhs)
{
    return lhs.epoch_seconds == rhs.epoch_seconds && lhs.nanosecond == rhs.nanosecond;
}

bool operator!=(LogTimestamp lhs, LogTimestamp rhs)
{
    return !(lhs == rhs);
}

bool operator<(LogTimestamp lhs, LogTimestamp rhs)
{
    if (lhs.epoch_seconds != rhs.epoch_seconds)
    {
        return lhs.epoch_seconds < rhs.epoch_seconds;
    }

    return lhs.nanosecond < rhs.nanosecond;
}

bool operator<=(LogTimestamp lhs, LogTimestamp rhs)
{
    return !(rhs < lhs);
}

bool operator>(LogTimestamp lhs, LogTimestamp rhs)
{
    return rhs < lhs;
}

bool operator>=(LogTimestamp lhs, LogTimestamp rhs)
{
    return !(lhs < rhs);
}

std::optional<LogTimestamp> make_log_timestamp_utc(int year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second, unsigned nanosecond,
                                                    std::optional<int> utc_offset_minutes)
{
    if (month < 1 || month > 12 || day < 1 || day > max_day_in_month(year, month) || hour > 23 || minute > 59 || second > 60 || nanosecond >= 1000000000)
    {
        return std::nullopt;
    }

    std::int64_t epoch_seconds = days_from_civil(year, month, day) * seconds_per_day + static_cast<std::int64_t>(hour) * 3600 + static_cast<std::int64_t>(minute) * 60 + static_cast<std::int64_t>(second);
    if (utc_offset_minutes.has_value())
    {
        epoch_seconds -= static_cast<std::int64_t>(*utc_offset_minutes) * 60;
    }

    return LogTimestamp {epoch_seconds, nanosecond};
}

LogCivilTime to_utc_civil_time(LogTimestamp timestamp)
{
    const std::int64_t days = floor_div(timestamp.epoch_seconds, seconds_per_day);
    const std::int64_t seconds_in_day = timestamp.epoch_seconds - days * seconds_per_day;

    LogCivilTime civil = civil_from_days(days);
    civil.hour = static_cast<unsigned>(seconds_in_day / 3600);
    civil.minute = static_cast<unsigned>((seconds_in_day % 3600) / 60);
    civil.second = static_cast<unsigned>(seconds_in_day % 60);
    civil.nanosecond = timestamp.nanosecond;
    return civil;
}

std::string format_log_timestamp_utc(LogTimestamp timestamp)
{
    const auto civil = to_utc_civil_time(timestamp);

    std::ostringstream output;
    output << std::setw(4) << std::setfill('0') << civil.year << '-' << format_two_digits(civil.month) << '-' << format_two_digits(civil.day) << ' ' << format_two_digits(civil.hour) << ':' << format_two_digits(civil.minute) << ':' << format_two_digits(civil.second);

    if (civil.nanosecond != 0)
    {
        output << '.' << format_fraction(civil.nanosecond);
    }

    return output.str();
}

std::optional<LogTimestampOffset> parse_log_timestamp_offset(std::string_view text)
{
    text = trim_view(text);
    if (text.empty())
    {
        return std::nullopt;
    }

    int sign = 1;
    if (text.front() == '+' || text.front() == '-')
    {
        sign = text.front() == '-' ? -1 : 1;
        text.remove_prefix(1);
        text = trim_view(text);
    }

    if (text.empty())
    {
        return std::nullopt;
    }

    const std::string input(text);
    for (const auto& entry : offset_parsers())
    {
        eestv::DateAndTime parsed;
        if (!apply_parser(entry.parser, input, parsed))
        {
            continue;
        }

        const std::int64_t days = static_cast<std::int64_t>(parsed.year - 2000);
        if (days < 0)
        {
            return std::nullopt;
        }

        std::int64_t total_seconds = 0;
        if (add_overflows(days * seconds_per_day, static_cast<std::int64_t>(parsed.hour) * 3600, total_seconds) ||
            add_overflows(total_seconds, static_cast<std::int64_t>(parsed.minute) * 60, total_seconds) || add_overflows(total_seconds, static_cast<std::int64_t>(parsed.second), total_seconds))
        {
            return std::nullopt;
        }

        return LogTimestampOffset {total_seconds * sign, static_cast<std::int32_t>(parsed.nanosecond) * sign};
    }

    return std::nullopt;
}

std::string format_log_timestamp_offset(LogTimestampOffset offset)
{
    const bool negative = offset.seconds < 0 || offset.nanosecond < 0;
    const std::int64_t absolute_seconds = offset.seconds < 0 ? -offset.seconds : offset.seconds;
    const unsigned absolute_nanosecond = static_cast<unsigned>(offset.nanosecond < 0 ? -offset.nanosecond : offset.nanosecond);

    const std::int64_t days = absolute_seconds / seconds_per_day;
    const std::int64_t seconds_after_days = absolute_seconds % seconds_per_day;
    const unsigned hours = static_cast<unsigned>(seconds_after_days / 3600);
    const unsigned minutes = static_cast<unsigned>((seconds_after_days % 3600) / 60);
    const unsigned seconds = static_cast<unsigned>(seconds_after_days % 60);

    std::ostringstream output;
    output << (negative ? '-' : '+') << std::setw(2) << std::setfill('0') << days << "d " << format_two_digits(hours) << ':' << format_two_digits(minutes) << ':' << format_two_digits(seconds);
    if (absolute_nanosecond != 0)
    {
        output << '.' << format_fraction(absolute_nanosecond);
    }

    return output.str();
}

std::optional<LogTimestamp> add_offset(LogTimestamp timestamp, LogTimestampOffset offset)
{
    std::int64_t seconds = 0;
    if (add_overflows(timestamp.epoch_seconds, offset.seconds, seconds))
    {
        return std::nullopt;
    }

    std::int64_t nanosecond = static_cast<std::int64_t>(timestamp.nanosecond) + offset.nanosecond;
    if (nanosecond >= nanoseconds_per_second)
    {
        if (add_overflows(seconds, 1, seconds))
        {
            return std::nullopt;
        }
        nanosecond -= nanoseconds_per_second;
    }
    else if (nanosecond < 0)
    {
        if (add_overflows(seconds, -1, seconds))
        {
            return std::nullopt;
        }
        nanosecond += nanoseconds_per_second;
    }

    return LogTimestamp {seconds, static_cast<std::uint32_t>(nanosecond)};
}

} // namespace slayerlog
