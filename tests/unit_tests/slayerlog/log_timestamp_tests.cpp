#include <gtest/gtest.h>

#include <limits>

#include "eestv/timestamp/timestamp_parser.hpp"
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

std::optional<eestv::DateAndTime> parse_with_format(const std::string& format, std::string_view input)
{
    const auto parser = eestv::TimestampParser::CompileFormat(format);
    if (!parser.valid())
    {
        return std::nullopt;
    }

    eestv::DateAndTime output;
    int index = 0;
    for (const auto& step : parser.steps)
    {
        int index_jump = 0;
        if (!step(input, index, index_jump, output))
        {
            return std::nullopt;
        }

        index += index_jump;
    }

    if (index != static_cast<int>(input.size()))
    {
        return std::nullopt;
    }

    return output;
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

TEST(LogTimestampTest, InitChoosesLongestMatchingTimestampFormat)
{
    auto formats = std::make_shared<const TimestampFormatCatalog>(std::vector<std::string> {
        "YYYY-MM-DDThh:mm:ss.fff",
        "YYYY-MM-DDThh:mm:ss.ffffff",
    });
    SourceTimestampParser parser;
    LogEntry line("2026-04-01T12:34:56.123456 details");

    ASSERT_TRUE(parser.init(line, *formats));
    ASSERT_TRUE(parser.parse(line));

    ASSERT_TRUE(line.metadata.timestamp.has_value());
    EXPECT_EQ(line.metadata.timestamp->nanosecond, 123456000U);
    EXPECT_EQ(extracted_time_view(line), "2026-04-01T12:34:56.123456");
    ASSERT_TRUE(line.metadata.extracted_time_end.has_value());
    EXPECT_EQ(*line.metadata.extracted_time_end, 26U);
}

TEST(LogTimestampTest, ParsesVariableSecondAndFractionDigits)
{
    const auto one_digit_fraction = parse_with_format("s*.f*", "60.5");
    const auto padded_seconds     = parse_with_format("s*.f*", "060.50");
    const auto comma_fraction     = parse_with_format("s*,f*", "6001,15550005");

    ASSERT_TRUE(one_digit_fraction.has_value());
    EXPECT_EQ(one_digit_fraction->second, 60U);
    EXPECT_EQ(one_digit_fraction->nanosecond, 500000000U);

    ASSERT_TRUE(padded_seconds.has_value());
    EXPECT_EQ(padded_seconds->second, 60U);
    EXPECT_EQ(padded_seconds->nanosecond, 500000000U);

    ASSERT_TRUE(comma_fraction.has_value());
    EXPECT_EQ(comma_fraction->second, 6001U);
    EXPECT_EQ(comma_fraction->nanosecond, 155500050U);
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

TEST(LogTimestampTest, ParsesFractionWithColonOffset)
{
    const auto with_offset = parse_timestamp("2026-07-03T12:34:56.789+02:00 event");
    const auto utc         = parse_timestamp("2026-07-03T10:34:56.789Z event");

    ASSERT_TRUE(with_offset.has_value());
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(*with_offset, *utc);
    EXPECT_EQ(with_offset->nanosecond, 789000000U);
}

TEST(LogTimestampTest, ParsesFractionWithCompactOffset)
{
    const auto with_offset = parse_timestamp("2026-07-03T12:34:56.789+0200 event");
    const auto utc         = parse_timestamp("2026-07-03T10:34:56.789Z event");

    ASSERT_TRUE(with_offset.has_value());
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(*with_offset, *utc);
}

TEST(LogTimestampTest, ExtractedTextIncludesTrailingZuluDesignator)
{
    const auto details = parse_timestamp_details("2026-07-03T12:34:56.789Z event");

    ASSERT_TRUE(details.has_value());
    ASSERT_TRUE(details->extracted_time_start.has_value());
    ASSERT_TRUE(details->extracted_time_end.has_value());
    EXPECT_EQ(*details->extracted_time_start, 0U);
    EXPECT_EQ(*details->extracted_time_end, 24U);
}

TEST(LogTimestampTest, ParsesNineFractionDigits)
{
    const auto parsed = parse_timestamp("2026-07-03T12:34:56.123456789Z event");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->nanosecond, 123456789U);
}

TEST(LogTimestampTest, ParsesLowercaseZuluDesignator)
{
    const auto lowercase = parse_timestamp("2026-07-03T12:34:56z event");
    const auto uppercase = parse_timestamp("2026-07-03T12:34:56Z event");

    ASSERT_TRUE(lowercase.has_value());
    ASSERT_TRUE(uppercase.has_value());
    EXPECT_EQ(*lowercase, *uppercase);
}

TEST(LogTimestampTest, ParsesSyslogDayPadding)
{
    const auto space_padded = parse_timestamp("Jul  3 12:00:00 host daemon: up");
    const auto zero_padded  = parse_timestamp("Jul 03 12:00:00 host daemon: up");
    const auto bare         = parse_timestamp("Jul 3 12:00:00 host daemon: up");
    const auto two_digit    = parse_timestamp("Jul 13 12:00:00 host daemon: up");

    ASSERT_TRUE(space_padded.has_value());
    ASSERT_TRUE(zero_padded.has_value());
    ASSERT_TRUE(bare.has_value());
    ASSERT_TRUE(two_digit.has_value());
    EXPECT_EQ(*space_padded, *zero_padded);
    EXPECT_EQ(*space_padded, *bare);
    EXPECT_EQ(to_utc_civil_time(*space_padded).day, 3U);
    EXPECT_EQ(to_utc_civil_time(*two_digit).day, 13U);
}

TEST(LogTimestampTest, ParsesYearlessLeapDay)
{
    const auto parsed = parse_timestamp("Feb 29 12:00:00 host daemon: tick");

    ASSERT_TRUE(parsed.has_value());
    const auto civil = to_utc_civil_time(*parsed);
    EXPECT_EQ(civil.month, 2U);
    EXPECT_EQ(civil.day, 29U);
}

TEST(LogTimestampTest, RejectsUnsupportedStrings)
{
    EXPECT_FALSE(parse_timestamp("INFO no timestamp here").has_value());
    EXPECT_FALSE(parse_timestamp("12:34:56 time only").has_value());
    EXPECT_FALSE(parse_timestamp("[2026-04-01T12:34:56 missing bracket").has_value());
}

TEST(LogTimestampTest, CompileFormatReportsInvalidFormats)
{
    EXPECT_FALSE(eestv::TimestampParser::CompileFormat("").valid());
    EXPECT_FALSE(eestv::TimestampParser::CompileFormat("YYY-MM-DD hh:mm:ss").valid());
    EXPECT_FALSE(eestv::TimestampParser::CompileFormat("YYYY-M-DD hh:mm:ss").valid());
    EXPECT_FALSE(eestv::TimestampParser::CompileFormat("hh:mm:ss.ffffffffff").valid());
    EXPECT_FALSE(eestv::TimestampParser::CompileFormat("").error.empty());

    EXPECT_TRUE(eestv::TimestampParser::CompileFormat("YYYY-MM-DD hh:mm:ss").valid());
    EXPECT_TRUE(eestv::TimestampParser::CompileFormat("MMM D* hh:mm:ss.f*ZZZ").valid());
}

TEST(LogTimestampTest, CatalogRejectsInvalidFormatsAndKeepsValidOnes)
{
    const TimestampFormatCatalog catalog(std::vector<std::string> {"YYY oops", "YYYY-MM-DD hh:mm:ss"});

    ASSERT_EQ(catalog.formats().size(), 1U);
    EXPECT_EQ(catalog.formats().front(), "YYYY-MM-DD hh:mm:ss");
    ASSERT_EQ(catalog.rejected_formats().size(), 1U);
    EXPECT_EQ(catalog.rejected_formats().front().format, "YYY oops");
    EXPECT_FALSE(catalog.rejected_formats().front().error.empty());
}

TEST(LogTimestampTest, CatalogFallsBackToDefaultsWhenAllFormatsAreInvalid)
{
    const TimestampFormatCatalog catalog(std::vector<std::string> {"YYY oops"});

    EXPECT_EQ(catalog.formats(), default_timestamp_formats());
    ASSERT_EQ(catalog.rejected_formats().size(), 1U);
}

TEST(LogTimestampTest, InitVotesAcrossLinesSoBannersCannotPoisonDetection)
{
    const auto catalog = default_timestamp_format_catalog();
    ASSERT_NE(catalog, nullptr);

    const std::vector<std::string> lines {
        "Log started 2026-07-03 10:00:00 by tooling",
        "2026-07-03 10:00:01 INFO alpha",
        "2026-07-03 10:00:02 INFO beta",
        "2026-07-03 10:00:03 INFO gamma",
    };

    SourceTimestampParser parser;
    ASSERT_TRUE(parser.init(lines, *catalog));

    LogEntry real_line("2026-07-03 10:00:04 INFO delta");
    ASSERT_TRUE(parser.parse(real_line));
    ASSERT_TRUE(real_line.metadata.extracted_time_start.has_value());
    EXPECT_EQ(*real_line.metadata.extracted_time_start, 0U);

    LogEntry banner_line("Log started 2026-07-03 11:00:00 by tooling");
    EXPECT_FALSE(parser.parse(banner_line));
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
    EXPECT_EQ(extracted_time_view(first_line), "2026-04-01 12:34:56");
    EXPECT_EQ(extracted_time_view(second_line), "2026-04-01 12:35:56");
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
    EXPECT_TRUE(extracted_time_view(line).empty());
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

TEST(LogTimestampTest, AddOffsetsCombinesComponents)
{
    const auto combined = add_offsets(LogTimestampOffset {10, 250000000}, LogTimestampOffset {5, 100000000});

    ASSERT_TRUE(combined.has_value());
    EXPECT_EQ(combined->seconds, 15);
    EXPECT_EQ(combined->nanosecond, 350000000);

    const auto identity = add_offsets(LogTimestampOffset {10, 250000000}, LogTimestampOffset {});
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(identity->seconds, 10);
    EXPECT_EQ(identity->nanosecond, 250000000);
}

TEST(LogTimestampTest, AddOffsetsCarriesNanosecondsIntoSeconds)
{
    const auto combined = add_offsets(LogTimestampOffset {0, 600000000}, LogTimestampOffset {0, 600000000});

    ASSERT_TRUE(combined.has_value());
    EXPECT_EQ(combined->seconds, 1);
    EXPECT_EQ(combined->nanosecond, 200000000);

    const auto negative = add_offsets(LogTimestampOffset {0, -600000000}, LogTimestampOffset {0, -600000000});
    ASSERT_TRUE(negative.has_value());
    EXPECT_EQ(negative->seconds, -1);
    EXPECT_EQ(negative->nanosecond, -200000000);
}

TEST(LogTimestampTest, AddOffsetsNormalizesMixedSigns)
{
    const auto positive = add_offsets(LogTimestampOffset {1, 0}, LogTimestampOffset {0, -1});

    ASSERT_TRUE(positive.has_value());
    EXPECT_EQ(positive->seconds, 0);
    EXPECT_EQ(positive->nanosecond, 999999999);

    const auto negative = add_offsets(LogTimestampOffset {-1, 0}, LogTimestampOffset {0, 1});
    ASSERT_TRUE(negative.has_value());
    EXPECT_EQ(negative->seconds, 0);
    EXPECT_EQ(negative->nanosecond, -999999999);
}

TEST(LogTimestampTest, AddOffsetsReturnsNulloptOnOverflow)
{
    const auto overflowed = add_offsets(LogTimestampOffset {(std::numeric_limits<std::int64_t>::max)(), 0}, LogTimestampOffset {1, 0});

    EXPECT_FALSE(overflowed.has_value());

    const auto carry_overflowed = add_offsets(LogTimestampOffset {(std::numeric_limits<std::int64_t>::max)(), 600000000}, LogTimestampOffset {0, 600000000});
    EXPECT_FALSE(carry_overflowed.has_value());
}

} // namespace slayerlog
