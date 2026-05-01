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

std::optional<std::chrono::system_clock::time_point> parse_timestamp(const std::string& line)
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
    const auto parsed = parse_timestamp("2026-04-01 12:34:56.123 details");
    EXPECT_TRUE(parsed.has_value());
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
    EXPECT_EQ(first_line.metadata.parsed_time_text, "2026-04-01 12:34:56");
    EXPECT_EQ(second_line.metadata.parsed_time_text, "2026-04-01 12:35:56");
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
    EXPECT_TRUE(line.metadata.parsed_time_text.empty());
    EXPECT_FALSE(line.metadata.extracted_time_start.has_value());
    EXPECT_FALSE(line.metadata.extracted_time_end.has_value());
}

TEST(LogTimestampTest, FormatsTimezoneInDisplayText)
{
    const auto parsed = parse_timestamp_details("2026-04-01T12:34:56+0200 event");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->parsed_time_text, "2026-04-01 12:34:56+02:00");
}

} // namespace slayerlog
