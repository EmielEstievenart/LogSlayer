#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "log_batch.hpp"
#include "timestamp/source_timestamp_parser.hpp"

namespace slayerlog
{

namespace
{

LogEntry make_entry(std::size_t source_index, std::string source_label, std::string text)
{
    std::optional<std::chrono::system_clock::time_point> timestamp;

    const auto catalog = default_timestamp_format_catalog();
    if (catalog != nullptr)
    {
        SourceTimestampParser parser;
        LogEntry raw_line(text);
        if (parser.init(raw_line, *catalog) && parser.parse(raw_line))
        {
            timestamp = raw_line.metadata.timestamp;
        }
    }

    return LogEntry {
        source_index, std::move(source_label), std::move(text), timestamp, 0,
    };
}

std::vector<std::shared_ptr<LogEntry>> make_shared_entries(const std::vector<std::string>& lines)
{
    std::vector<std::shared_ptr<LogEntry>> entries;
    entries.reserve(lines.size());
    for (const auto& line : lines)
    {
        entries.push_back(std::make_shared<LogEntry>(make_entry(0, "", line)));
    }

    return entries;
}

} // namespace

TEST(LogBatchTest, PlacesUnsortableLinesBeforeTimestampedLines)
{
    const auto merged = merge_log_batch({
        make_entry(0, "alpha.log", "plain alpha"),
        make_entry(0, "alpha.log", "2026-04-01T10:02:00 alpha timed"),
        make_entry(1, "beta.log", "plain beta"),
        make_entry(1, "beta.log", "2026-04-01T10:01:00 beta timed"),
    });

    ASSERT_EQ(merged.size(), 4U);
    EXPECT_EQ(merged[0].text, "plain alpha");
    EXPECT_EQ(merged[1].text, "plain beta");
    EXPECT_EQ(merged[2].text, "2026-04-01T10:01:00 beta timed");
    EXPECT_EQ(merged[3].text, "2026-04-01T10:02:00 alpha timed");
}

TEST(LogBatchTest, SortsTimestampedLinesAcrossFilesByParsedTime)
{
    const auto merged = merge_log_batch({
        make_entry(0, "alpha.log", "2026-04-01T10:03:00 alpha third"),
        make_entry(0, "alpha.log", "2026-04-01T10:05:00 alpha fifth"),
        make_entry(1, "beta.log", "2026-04-01T10:04:00 beta fourth"),
    });

    ASSERT_EQ(merged.size(), 3U);
    EXPECT_EQ(merged[0].text, "2026-04-01T10:03:00 alpha third");
    EXPECT_EQ(merged[1].text, "2026-04-01T10:04:00 beta fourth");
    EXPECT_EQ(merged[2].text, "2026-04-01T10:05:00 alpha fifth");
}

TEST(LogBatchTest, KeepsOriginalSourceOrderForEqualTimestamps)
{
    const auto merged = merge_log_batch({
        make_entry(0, "alpha.log", "2026-04-01T10:00:00 alpha first"),
        make_entry(1, "beta.log", "2026-04-01T10:00:00 beta second"),
    });

    ASSERT_EQ(merged.size(), 2U);
    EXPECT_EQ(merged[0].text, "2026-04-01T10:00:00 alpha first");
    EXPECT_EQ(merged[1].text, "2026-04-01T10:00:00 beta second");
}

TEST(LogBatchTest, EmitsUntimestampedLinesWhenTheyReachTheFront)
{
    const auto merged = merge_log_batch({
        make_entry(0, "alpha.log", "2026-04-01T10:02:00 alpha timed"),
        make_entry(0, "alpha.log", "plain alpha first follow-up"),
        make_entry(0, "alpha.log", "plain alpha second follow-up"),
        make_entry(0, "alpha.log", "2026-04-01T10:05:00 alpha later"),
        make_entry(1, "beta.log", "2026-04-01T10:03:00 beta timed"),
    });

    ASSERT_EQ(merged.size(), 5U);
    EXPECT_EQ(merged[0].text, "2026-04-01T10:02:00 alpha timed");
    EXPECT_EQ(merged[1].text, "plain alpha first follow-up");
    EXPECT_EQ(merged[2].text, "plain alpha second follow-up");
    EXPECT_EQ(merged[3].text, "2026-04-01T10:03:00 beta timed");
    EXPECT_EQ(merged[4].text, "2026-04-01T10:05:00 alpha later");
}

TEST(LogBatchTest, MergesPerSourceRangesAndOverwritesSourceMetadata)
{
    const auto alpha_entries = make_shared_entries({
        "2026-04-01T10:01:00 alpha old",
        "2026-04-01T10:03:00 alpha third",
        "plain alpha follow-up",
        "2026-04-01T10:05:00 alpha fifth",
    });
    const auto beta_entries = make_shared_entries({
        "2026-04-01T10:04:00 beta fourth",
    });

    const auto merged = merge_log_batch({
        {&alpha_entries, 1, 3, "alpha.log"},
        {&beta_entries, 0, 7, "beta.log"},
    });

    ASSERT_EQ(merged.size(), 4U);
    EXPECT_EQ(merged[0]->text, "2026-04-01T10:03:00 alpha third");
    EXPECT_EQ(merged[1]->text, "plain alpha follow-up");
    EXPECT_EQ(merged[2]->text, "2026-04-01T10:04:00 beta fourth");
    EXPECT_EQ(merged[3]->text, "2026-04-01T10:05:00 alpha fifth");

    EXPECT_EQ(merged[0]->metadata.source_index, 3U);
    EXPECT_EQ(merged[1]->metadata.source_index, 3U);
    EXPECT_EQ(merged[2]->metadata.source_index, 7U);
    EXPECT_EQ(merged[3]->metadata.source_index, 3U);

    EXPECT_EQ(merged[0]->metadata.source_label, "alpha.log");
    EXPECT_EQ(merged[2]->metadata.source_label, "beta.log");
}

} // namespace slayerlog
