#include <gtest/gtest.h>

#include "timestamp/source_timestamp_parser.hpp"

namespace slayerlog
{

namespace
{

std::optional<LogEntryMetadata> parse_timestamp_details(const std::string& line)
{
    const auto catalog = default_timestamp_format_catalog();
    if (catalog == nullptr)
    {
        return std::nullopt;
    }

    SourceTimestampParser parser;
    LogEntry raw_line(line);
    if (!parser.init(raw_line, *catalog))
    {
        return std::nullopt;
    }

    if (!parser.parse(raw_line))
    {
        return std::nullopt;
    }

    return raw_line.metadata;
}

std::optional<LogTimestamp> parse_timestamp(const std::string& line)
{
    const auto parsed = parse_timestamp_details(line);
    if (!parsed.has_value())
    {
        return std::nullopt;
    }

    return parsed->timestamp;
}

} // namespace

TEST(LogTimestampTest, ParsesBracketedIsoTimestamp)
{
    const auto parsed = parse_timestamp("[2026-04-01T12:34:56] hello");
    EXPECT_TRUE(parsed.has_value());
}

TEST(LogTimestampTest, ParsesSpaceSeparatedTimestampWithFraction)
{
    auto formats = std::make_shared<const TimestampFormatCatalog>(std::vector<std::string> {"YYYY-MM-DD hh:mm:ss.fff"});
    SourceTimestampParser parser;
    LogEntry line("2026-04-01 12:34:56.123 details");

    ASSERT_TRUE(parser.init(line, *formats));
    const bool parsed_line = parser.parse(line);

    ASSERT_TRUE(parsed_line);
    ASSERT_TRUE(line.metadata.timestamp.has_value());
    EXPECT_EQ(line.metadata.timestamp->nanosecond, 123000000U);
    EXPECT_EQ(format_log_timestamp_utc(*line.metadata.timestamp), "2026-04-01 12:34:56.123");
}

TEST(LogTimestampTest, TreatsOffsetlessTimestampsAsUtc)
{
    const auto parsed = parse_timestamp("1970-01-01 00:00:00 epoch");
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->epoch_seconds, 0);
    EXPECT_EQ(parsed->nanosecond, 0U);
}

TEST(LogTimestampTest, NormalizesZuluAndOffsetTimestamps)
{
    const auto zulu   = parse_timestamp("2026-04-01T10:34:56Z event");
    const auto offset = parse_timestamp("2026-04-01T12:34:56+0200 event");

    ASSERT_TRUE(zulu.has_value());
    ASSERT_TRUE(offset.has_value());
    EXPECT_EQ(*zulu, *offset);
}

TEST(LogTimestampTest, ParsesColonSeparatedTimezoneOffset)
{
    const auto earlier = parse_timestamp("2026-04-01T12:34:56+02:00 first");
    const auto later   = parse_timestamp("2026-04-01T12:35:56+02:00 second");

    ASSERT_TRUE(earlier.has_value());
    ASSERT_TRUE(later.has_value());
    EXPECT_LT(*earlier, *later);
}

TEST(LogTimestampTest, RejectsUnsupportedStrings)
{
    EXPECT_FALSE(parse_timestamp("INFO no timestamp here").has_value());
    EXPECT_FALSE(parse_timestamp("12:34:56 time only").has_value());
    EXPECT_FALSE(parse_timestamp("[2026-04-01T12:34:56 missing bracket").has_value());
}

TEST(LogTimestampTest, DetectsTimestampAfterPrefixAndKeepsCompiledParser)
{
    auto formats = std::make_shared<const TimestampFormatCatalog>(std::vector<std::string> {"YYYY-MM-DD hh:mm:ss"});
    SourceTimestampParser parser;
    LogEntry first_line("INFO 2026-04-01 12:34:56 first");
    LogEntry second_line("WARN 2026-04-01 12:35:56 second");

    const bool initialized   = parser.init(first_line, *formats);
    const bool first_parsed  = parser.parse(first_line);
    const bool second_parsed = parser.parse(second_line);

    ASSERT_TRUE(initialized);
    ASSERT_TRUE(first_parsed);
    ASSERT_TRUE(second_parsed);
    EXPECT_EQ(first_line.metadata.extracted_time_text, "2026-04-01 12:34:56");
    EXPECT_EQ(second_line.metadata.extracted_time_text, "2026-04-01 12:35:56");
    ASSERT_TRUE(first_line.metadata.timestamp.has_value());
    ASSERT_TRUE(second_line.metadata.timestamp.has_value());
    EXPECT_EQ(format_log_timestamp_utc(*first_line.metadata.timestamp), "2026-04-01 12:34:56");
    EXPECT_EQ(format_log_timestamp_utc(*second_line.metadata.timestamp), "2026-04-01 12:35:56");
    ASSERT_TRUE(first_line.metadata.extracted_time_start.has_value());
    ASSERT_TRUE(first_line.metadata.extracted_time_end.has_value());
    ASSERT_TRUE(second_line.metadata.extracted_time_start.has_value());
    ASSERT_TRUE(second_line.metadata.extracted_time_end.has_value());
    EXPECT_EQ(*first_line.metadata.extracted_time_start, 5U);
    EXPECT_EQ(*first_line.metadata.extracted_time_end, 24U);
    EXPECT_EQ(*second_line.metadata.extracted_time_start, 5U);
    EXPECT_EQ(*second_line.metadata.extracted_time_end, 24U);
}

TEST(LogTimestampTest, InitDoesNotPopulateMetadata)
{
    auto formats = std::make_shared<const TimestampFormatCatalog>(std::vector<std::string> {"YYYY-MM-DD hh:mm:ss"});
    SourceTimestampParser parser;
    LogEntry line("INFO 2026-04-01 12:34:56 first");

    ASSERT_TRUE(parser.init(line, *formats));
    EXPECT_FALSE(line.metadata.timestamp.has_value());
    EXPECT_TRUE(line.metadata.extracted_time_text.empty());
    EXPECT_FALSE(line.metadata.extracted_time_start.has_value());
    EXPECT_FALSE(line.metadata.extracted_time_end.has_value());
}

TEST(LogTimestampTest, FormatsTimezoneAsStoredUtcInstant)
{
    const auto parsed = parse_timestamp_details("2026-04-01T12:34:56+0200 event");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->timestamp.has_value());
    EXPECT_EQ(format_log_timestamp_utc(*parsed->timestamp), "2026-04-01 10:34:56");
}

TEST(LogTimestampTest, OrdersFractionsInsideSameSecond)
{
    const auto earlier = make_log_timestamp_utc(2026, 4, 1, 12, 34, 56, 100000000);
    const auto later   = make_log_timestamp_utc(2026, 4, 1, 12, 34, 56, 200000000);

    ASSERT_TRUE(earlier.has_value());
    ASSERT_TRUE(later.has_value());
    EXPECT_LT(*earlier, *later);
}

TEST(LogTimestampTest, SupportsNegativeEpochSeconds)
{
    const auto timestamp = make_log_timestamp_utc(1969, 12, 31, 23, 59, 59, 500000000);

    ASSERT_TRUE(timestamp.has_value());
    EXPECT_EQ(timestamp->epoch_seconds, -1);
    EXPECT_EQ(timestamp->nanosecond, 500000000U);
    EXPECT_EQ(format_log_timestamp_utc(*timestamp), "1969-12-31 23:59:59.5");
}

TEST(LogTimestampTest, ParsesTimestampOffsetWithFraction)
{
    const auto offset = parse_log_timestamp_offset("20 02:10:10.005");

    ASSERT_TRUE(offset.has_value());
    EXPECT_EQ(offset->seconds, 20 * 86400 + 2 * 3600 + 10 * 60 + 10);
    EXPECT_EQ(offset->nanosecond, 5000000);
    EXPECT_EQ(format_log_timestamp_offset(*offset), "+20d 02:10:10.005");
}

TEST(LogTimestampTest, ParsesNegativeTimestampOffsetWithFraction)
{
    const auto offset = parse_log_timestamp_offset("-00 00:00:10.000500");

    ASSERT_TRUE(offset.has_value());
    EXPECT_EQ(offset->seconds, -10);
    EXPECT_EQ(offset->nanosecond, -500000);
    EXPECT_EQ(format_log_timestamp_offset(*offset), "-00d 00:00:10.0005");
}

TEST(LogTimestampTest, AddsTimestampOffsetAcrossSecondBoundary)
{
    const LogTimestamp timestamp {0, 250000000};
    const LogTimestampOffset offset {-1, -500000000};

    const auto result = add_offset(timestamp, offset);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->epoch_seconds, -2);
    EXPECT_EQ(result->nanosecond, 750000000U);
}

TEST(LogTimestampTest, CalculatesPositiveOffsetBetweenTimestamps)
{
    const LogTimestamp from {10, 750000000};
    const LogTimestamp to {16, 250000000};

    const auto offset = offset_between(from, to);

    ASSERT_TRUE(offset.has_value());
    EXPECT_EQ(offset->seconds, 5);
    EXPECT_EQ(offset->nanosecond, 500000000);
    EXPECT_EQ(add_offset(from, *offset), std::optional<LogTimestamp>(to));
}

TEST(LogTimestampTest, CalculatesNegativeOffsetBetweenTimestamps)
{
    const LogTimestamp from {16, 250000000};
    const LogTimestamp to {10, 750000000};

    const auto offset = offset_between(from, to);

    ASSERT_TRUE(offset.has_value());
    EXPECT_EQ(offset->seconds, -5);
    EXPECT_EQ(offset->nanosecond, -500000000);
    EXPECT_EQ(add_offset(from, *offset), std::optional<LogTimestamp>(to));
}

} // namespace slayerlog
