#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/tracked_source_base.hpp"

namespace slayerlog
{

using LogModel = AllProcessedSources;

namespace
{

std::vector<std::string> rendered_texts(const LogModel& model)
{
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(model.line_count()));
    for (int index = 0; index < model.line_count(); ++index)
    {
        const auto rendered   = model.rendered_line(index);
        const auto prefix_end = rendered.find(' ');
        lines.push_back(rendered.substr(prefix_end + 1));
    }

    return lines;
}

/// Reference implementation for max_rendered_line_width(): render every visible row.
/// The production code tracks the maximum incrementally; the two must always agree.
int brute_force_max_rendered_width(const LogModel& model)
{
    int width = 0;
    for (int index = 0; index < model.line_count(); ++index)
    {
        width = std::max(width, static_cast<int>(model.rendered_line(index).size()));
    }

    return width;
}

std::vector<LogEntry> numbered_lines(int count)
{
    std::vector<LogEntry> lines;
    lines.reserve(static_cast<std::size_t>(count));
    for (int index = 1; index <= count; ++index)
    {
        lines.push_back({"alpha.log", "line " + std::to_string(index)});
    }

    return lines;
}

LogTimestamp test_timestamp(unsigned second = 0)
{
    return *make_log_timestamp_utc(2026, 4, 1, 10, 0, second, 0);
}

LogEntry make_batch_entry(std::size_t source_index, std::string source_label, std::string text, std::uint64_t sequence_number, std::optional<LogTimestamp> timestamp = std::nullopt)
{
    LogEntry entry;
    entry.metadata.source_index    = source_index;
    entry.metadata.source_label    = std::move(source_label);
    entry.text                     = std::move(text);
    entry.metadata.timestamp       = timestamp;
    entry.metadata.sequence_number = sequence_number;
    return entry;
}

LogEntry make_entry_with_extracted_time(std::size_t source_index, std::string source_label, std::string text, std::size_t extracted_time_start, std::size_t extracted_time_end)
{
    LogEntry entry;
    entry.metadata.source_index         = source_index;
    entry.metadata.source_label         = std::move(source_label);
    entry.text                          = std::move(text);
    entry.metadata.extracted_time_start = extracted_time_start;
    entry.metadata.extracted_time_end   = extracted_time_end;
    return entry;
}

struct TestSource : TrackedSourceBase
{
    explicit TestSource(std::string source_label) : TrackedSourceBase(LogSource {}, std::move(source_label)) { }

    void set_timestamp_catalog(std::shared_ptr<const TimestampFormatCatalog>) override { }
    bool poll() override { return false; }
};

LogEntry make_presented_entry(std::size_t source_index, std::string source_label, std::string text, TestSource& source)
{
    LogEntry entry {source_index, source_label, std::move(text)};
    entry.metadata.source = &source;
    return entry;
}

} // namespace

TEST(LogModelTest, AppendsLinesInProvidedOrder)
{
    LogModel model;

    model.append_lines({
        LogEntry {"alpha.log", "plain alpha"},
        LogEntry {"beta.log", "2026-04-01T10:01:00 beta timed"},
        LogEntry {"alpha.log", "2026-04-01T10:02:00 alpha timed"},
    });

    EXPECT_EQ(rendered_texts(model), (std::vector<std::string> {
                                         "plain alpha",
                                         "2026-04-01T10:01:00 beta timed",
                                         "2026-04-01T10:02:00 alpha timed",
                                     }));
}

TEST(LogModelTest, LinesChangedCallbacksAreOwnedByProcessedSources)
{
    LogModel model;
    std::vector<int> first_changed_lines;
    const auto callback_id = model.add_lines_changed_callback([&](VisibleLineIndex first_changed_line) { first_changed_lines.push_back(first_changed_line.value); });

    model.append_lines({
        LogEntry {"alpha.log", "first"},
        LogEntry {"alpha.log", "second"},
    });
    model.append_lines({
        LogEntry {"alpha.log", "third"},
    });
    model.add_include_filter("third");

    model.remove_lines_changed_callback(callback_id);
    model.append_lines({
        LogEntry {"alpha.log", "fourth"},
    });

    EXPECT_EQ(first_changed_lines, (std::vector<int> {0, 2, 0}));
}

TEST(LogModelTest, PausedUpdatesAppendWhenResumed)
{
    LogModel model;
    model.toggle_pause();

    model.append_lines({
        LogEntry {"alpha.log", "first"},
        LogEntry {"beta.log", "second"},
    });

    EXPECT_EQ(model.line_count(), 0);

    model.toggle_pause();

    EXPECT_EQ(rendered_texts(model), (std::vector<std::string> {
                                         "first",
                                         "second",
                                     }));
}

TEST(LogModelTest, ReplaceBatchPreservesSessionState)
{
    LogModel model;
    model.append_lines({
        LogEntry {"alpha.log", "error seed"},
    });
    model.add_include_filter("error");
    model.hide_before_line_number(2);
    model.hide_columns(2, 4);
    model.toggle_pause();

    model.replace_batch({
        make_batch_entry(0, "alpha.log", "info one", 0),
        make_batch_entry(0, "alpha.log", "error two", 1),
    });

    EXPECT_TRUE(model.updates_paused());
    EXPECT_EQ(model.hidden_before_line_number(), 2);
    ASSERT_TRUE(model.hidden_columns().has_value());
    EXPECT_EQ(*model.hidden_columns(), (HiddenColumnRange {2, 4}));
    EXPECT_EQ(model.line_count(), 1);

    model.append_batch({
        make_batch_entry(0, "alpha.log", "error three", 2),
    });
    EXPECT_EQ(model.line_count(), 1);

    model.toggle_pause();
    EXPECT_EQ(model.line_count(), 2);
}

TEST(LogModelTest, ResetClearsAllLoadedAndDerivedState)
{
    LogModel model;

    model.append_lines({
        LogEntry {"alpha.log", "error one"},
        LogEntry {"alpha.log", "info two"},
    });
    model.add_include_filter("error");
    model.add_exclude_filter("ignore");
    model.hide_before_line_number(2);
    model.hide_columns(2, 5);
    model.toggle_pause();
    model.append_lines({
        LogEntry {"alpha.log", "queued while paused"},
    });

    model.reset();

    EXPECT_EQ(model.line_count(), 0);
    EXPECT_EQ(model.total_line_count(), 0);
    EXPECT_TRUE(model.include_filters().empty());
    EXPECT_TRUE(model.exclude_filters().empty());
    EXPECT_FALSE(model.hidden_before_line_number().has_value());
    EXPECT_FALSE(model.hidden_columns().has_value());
    EXPECT_FALSE(model.updates_paused());

    model.append_lines({
        LogEntry {"alpha.log", "post-reset"},
    });
    EXPECT_EQ(model.rendered_line(0), "1 post-reset");
}

TEST(LogModelTest, RendersPresentedMnemonicPrefixVerbatim)
{
    LogModel model;
    TestSource alpha_source("alpha.log");
    TestSource beta_source("beta.log");
    alpha_source.set_source_mnemonic("Everest");
    beta_source.set_source_mnemonic("Denali");
    alpha_source.set_mnemonic_visible(true);
    beta_source.set_mnemonic_visible(true);

    model.append_lines({
        make_presented_entry(0, "alpha.log", "hello", alpha_source),
        make_presented_entry(1, "beta.log", "world", beta_source),
        LogEntry {2, "gamma.log", "plain"},
    });

    EXPECT_EQ(model.rendered_line(0), "1 Everest hello");
    EXPECT_EQ(model.rendered_line(1), "2 Denali world");
    EXPECT_EQ(model.rendered_line(2), "3 plain");
}

TEST(LogModelTest, FiltersMatchAgainstPresentedMnemonic)
{
    LogModel model;
    TestSource alpha_source("alpha.log");
    TestSource beta_source("beta.log");
    alpha_source.set_source_mnemonic("Everest");
    beta_source.set_source_mnemonic("Denali");
    alpha_source.set_mnemonic_visible(true);
    beta_source.set_mnemonic_visible(true);

    model.append_lines({
        make_presented_entry(0, "alpha.log", "hello", alpha_source),
        make_presented_entry(1, "beta.log", "world", beta_source),
    });
    model.add_include_filter("Everest");

    ASSERT_EQ(model.line_count(), 1);
    EXPECT_EQ(model.rendered_line(0), "1 Everest hello");
}

TEST(LogModelTest, HidesDetectedTimestampTextByDefault)
{
    LogModel model;

    LogEntry entry {"alpha.log", "INFO 2026-04-01 10:00:00 hello", test_timestamp()};
    entry.metadata.extracted_time_start = 5;
    entry.metadata.extracted_time_end   = 24;

    model.append_lines({entry});

    EXPECT_EQ(model.rendered_line(0), "1 {2026-04-01 10:00:00} INFO  hello");
}

TEST(LogModelTest, ShowsDetectedTimestampTextWhenEnabled)
{
    LogModel model;

    LogEntry entry {"alpha.log", "INFO 2026-04-01 10:00:00 hello", test_timestamp()};
    entry.metadata.extracted_time_start = 5;
    entry.metadata.extracted_time_end   = 24;

    model.append_lines({entry});
    model.set_show_original_time(true);

    EXPECT_EQ(model.rendered_line(0), "1 {2026-04-01 10:00:00} INFO 2026-04-01 10:00:00 hello");
}

TEST(LogModelTest, RendersOffsetTimestampWhenPresent)
{
    LogModel model;
    LogEntry entry {"alpha.log", "hello", test_timestamp()};
    entry.metadata.offset_timestamp = *add_offset(*entry.metadata.timestamp, *parse_log_timestamp_offset("00 01:00:00"));

    model.append_lines({entry});

    EXPECT_EQ(model.rendered_line(0), "1 {2026-04-01 11:00:00} hello");
}

TEST(LogModelTest, HidesIdenticalMessagesThatOnlyDifferByTimestampByDefault)
{
    LogModel model;
    model.append_lines({
        make_entry_with_extracted_time(0, "alpha.log", "INFO 2026-04-01 10:00:00 hello", 5, 24),
        make_entry_with_extracted_time(0, "alpha.log", "INFO 2026-04-01 10:00:01 hello", 5, 24),
        make_entry_with_extracted_time(0, "alpha.log", "INFO 2026-04-01 10:00:02 hello", 5, 24),
        make_entry_with_extracted_time(0, "alpha.log", "INFO 2026-04-01 10:00:03 world", 5, 24),
    });

    ASSERT_EQ(model.line_count(), 3);
    EXPECT_EQ(model.rendered_line(0), "1 INFO  hello");
    EXPECT_EQ(model.rendered_line(1), "  hiding 2 identical messages above");
    EXPECT_EQ(model.rendered_line(2), "4 INFO  world");
    EXPECT_EQ(model.line_number_for_visible_line(VisibleLineIndex {1}), 3);
    ASSERT_TRUE(model.visible_line_index_for_line_number(2).has_value());
    EXPECT_EQ(model.visible_line_index_for_line_number(2)->value, 1);
    ASSERT_TRUE(model.visible_line_index_for_line_number(3).has_value());
    EXPECT_EQ(model.visible_line_index_for_line_number(3)->value, 1);
}

TEST(LogModelTest, ShowIdenticalLinesRestoresAllMatchingRows)
{
    LogModel model;
    model.append_lines({
        make_entry_with_extracted_time(0, "alpha.log", "INFO 2026-04-01 10:00:00 hello", 5, 24),
        make_entry_with_extracted_time(0, "alpha.log", "INFO 2026-04-01 10:00:01 hello", 5, 24),
    });

    ASSERT_EQ(model.line_count(), 2);
    EXPECT_EQ(model.rendered_line(1), "  hiding 1 identical messages above");

    model.set_hide_identical_lines(false);

    EXPECT_EQ(model.line_count(), 2);
    EXPECT_EQ(model.rendered_line(0), "1 INFO  hello");
    EXPECT_EQ(model.rendered_line(1), "2 INFO  hello");
}

TEST(LogModelTest, ReservesTimestampColumnWidthForRowsWithoutTimestamp)
{
    LogModel model;

    model.append_lines({
        LogEntry {"alpha.log", "with timestamp", test_timestamp()},
        LogEntry {"alpha.log", "without timestamp"},
    });

    const auto first  = model.rendered_line(0);
    const auto second = model.rendered_line(1);

    // The timestamp column is reserved (and padded) for the row that has no timestamp, so the
    // message text starts at the same column on both rows.
    const int message_column = model.line_number_column_width() + 1 + model.timestamp_column_width() + 1;

    EXPECT_GT(model.timestamp_column_width(), 0);
    EXPECT_EQ(first.substr(static_cast<std::size_t>(message_column)), "with timestamp");
    EXPECT_EQ(second.substr(static_cast<std::size_t>(message_column)), "without timestamp");
}

TEST(LogModelTest, ColumnWidthsGrowDynamicallyAndResetWhenModelBecomesEmpty)
{
    LogModel model;

    model.append_lines({
        LogEntry {"alpha.log", "first", test_timestamp()},
        LogEntry {123, "omega.log", "second"},
    });

    EXPECT_EQ(model.timestamp_column_width(), static_cast<int>(std::string("{2026-04-01 10:00:00}").size()));

    model.replace_batch({
        LogEntry {"alpha.log", "after replace"},
    });

    EXPECT_GT(model.timestamp_column_width(), 0);

    model.replace_batch(std::vector<LogEntry> {});

    EXPECT_EQ(model.line_count(), 0);
    EXPECT_EQ(model.timestamp_column_width(), 0);
}

TEST(LogModelTest, RenderedLinesReturnsVisibleSlice)
{
    LogModel model;

    model.append_lines({
        LogEntry {"alpha.log", "first"},
        LogEntry {"alpha.log", "second"},
        LogEntry {"alpha.log", "third"},
    });
    model.add_exclude_filter("second");

    EXPECT_EQ(model.rendered_lines(0, 2), (std::vector<std::string> {
                                              "1 first",
                                              "3 third",
                                          }));
}

TEST(LogModelTest, FiltersApplyRetroactivelyAndToNewLines)
{
    LogModel model;

    model.append_lines({
        LogEntry {"alpha.log", "error critical"},
        LogEntry {"alpha.log", "warn noisy"},
        LogEntry {"alpha.log", "warn useful"},
        LogEntry {"alpha.log", "info ignored"},
    });

    model.add_include_filter("error");
    model.add_include_filter("warn");
    model.add_exclude_filter("noisy");

    EXPECT_EQ(rendered_texts(model), (std::vector<std::string> {
                                         "error critical",
                                         "warn useful",
                                     }));

    model.append_lines({
        LogEntry {"alpha.log", "warn newest"},
        LogEntry {"alpha.log", "debug newest"},
    });
    EXPECT_EQ(rendered_texts(model), (std::vector<std::string> {
                                         "error critical",
                                         "warn useful",
                                         "warn newest",
                                     }));

    model.reset_filters();
    EXPECT_EQ(model.line_count(), 6);
}

TEST(LogModelTest, HideBeforeLineUsesRawLineNumbers)
{
    LogModel model;
    model.append_lines(numbered_lines(5));

    model.hide_before_line_number(3);

    EXPECT_EQ(rendered_texts(model), (std::vector<std::string> {
                                         "line 3",
                                         "line 4",
                                         "line 5",
                                     }));
    EXPECT_EQ(model.hidden_before_line_number(), 3);
    EXPECT_EQ(model.line_number_for_visible_line(VisibleLineIndex {0}), 3);
    EXPECT_EQ(model.line_number_for_visible_line(VisibleLineIndex {2}), 5);
    EXPECT_FALSE(model.line_number_for_visible_line(VisibleLineIndex {3}).has_value());
    ASSERT_TRUE(model.visible_line_index_for_line_number(3).has_value());
    EXPECT_EQ(model.visible_line_index_for_line_number(3)->value, 0);
    EXPECT_FALSE(model.visible_line_index_for_line_number(2).has_value());
}

TEST(LogModelTest, HideColumnsRemovesDisplayedRange)
{
    LogModel model;
    model.append_lines({
        LogEntry {"alpha.log", "abcdef"},
    });

    model.hide_columns(2, 4);

    ASSERT_TRUE(model.hidden_columns().has_value());
    EXPECT_EQ(*model.hidden_columns(), (HiddenColumnRange {2, 4}));
    EXPECT_EQ(model.rendered_line(0), "1 cdef");
}

TEST(LogModelTest, ResetHiddenColumnsRestoresRenderedText)
{
    LogModel model;
    model.append_lines({
        LogEntry {"alpha.log", "abcdef"},
    });
    model.hide_columns(2, 4);

    model.reset_hidden_columns();

    EXPECT_FALSE(model.hidden_columns().has_value());
    EXPECT_EQ(model.rendered_line(0), "1 abcdef");
}

TEST(LogModelTest, ParseHiddenColumnRangeUsesHalfOpenZeroBasedSyntax)
{
    const auto trimmed_range = parse_hidden_column_range(" 4 - 10 ");
    ASSERT_TRUE(trimmed_range.has_value());
    EXPECT_EQ(*trimmed_range, (HiddenColumnRange {4, 10}));
    EXPECT_FALSE(parse_hidden_column_range("4-4").has_value());
    EXPECT_FALSE(parse_hidden_column_range("-1-4").has_value());
    EXPECT_FALSE(parse_hidden_column_range("4-ten").has_value());
}

TEST(LogModelTest, FiltersSupportRegexWithRePrefix)
{
    LogModel model;
    model.append_lines({
        LogEntry {"alpha.log", "status=200 ok"},
        LogEntry {"alpha.log", "status=404 missing"},
        LogEntry {"alpha.log", "status=500 error"},
    });

    model.add_include_filter("re:status=(4|5)\\d\\d");
    model.add_exclude_filter("re:status=404");

    EXPECT_EQ(rendered_texts(model), (std::vector<std::string> {
                                         "status=500 error",
                                     }));
}

TEST(LogModelTest, MaxRenderedLineWidthMatchesRenderedRowsThroughModelChanges)
{
    LogModel model;
    EXPECT_EQ(model.max_rendered_line_width(), 0);

    model.append_lines({
        LogEntry {"alpha.log", "short", test_timestamp()},
        make_entry_with_extracted_time(0, "alpha.log", "INFO 2026-04-01 10:00:00 the widest message of them all", 5, 24),
        LogEntry {"alpha.log", "mid sized text"},
    });
    EXPECT_EQ(model.max_rendered_line_width(), brute_force_max_rendered_width(model));

    model.set_show_original_time(true);
    EXPECT_EQ(model.max_rendered_line_width(), brute_force_max_rendered_width(model));

    model.set_show_original_time(false);
    EXPECT_EQ(model.max_rendered_line_width(), brute_force_max_rendered_width(model));

    model.append_lines({
        LogEntry {"alpha.log", "appended later and even wider than the earlier widest message"},
    });
    EXPECT_EQ(model.max_rendered_line_width(), brute_force_max_rendered_width(model));

    model.add_include_filter("mid");
    EXPECT_EQ(model.max_rendered_line_width(), brute_force_max_rendered_width(model));

    model.reset_filters();
    model.hide_before_line_number(2);
    EXPECT_EQ(model.max_rendered_line_width(), brute_force_max_rendered_width(model));
}

TEST(LogModelTest, MaxRenderedLineWidthAppliesHiddenColumns)
{
    LogModel model;
    model.append_lines({
        LogEntry {"alpha.log", "abcdefghijklmnop"},
        LogEntry {"alpha.log", "abc"},
    });

    model.hide_columns(4, 9);
    EXPECT_EQ(model.max_rendered_line_width(), brute_force_max_rendered_width(model));

    model.hide_columns(10, 200);
    EXPECT_EQ(model.max_rendered_line_width(), brute_force_max_rendered_width(model));

    model.reset_hidden_columns();
    EXPECT_EQ(model.max_rendered_line_width(), brute_force_max_rendered_width(model));
}

TEST(LogModelTest, MaxRenderedLineWidthCoversHiddenIdenticalRunRows)
{
    LogModel model;

    std::vector<LogEntry> lines;
    lines.push_back({"alpha.log", "x"});
    for (int index = 0; index < 12; ++index)
    {
        lines.push_back({"alpha.log", "dup"});
    }

    model.append_lines(lines);

    // The "hiding N identical messages above" row is the widest row here.
    EXPECT_EQ(model.max_rendered_line_width(), brute_force_max_rendered_width(model));
}

TEST(LogModelTest, AppendingContinuesHiddenIdenticalRunsLikeAFullRebuild)
{
    LogModel incremental;
    incremental.append_lines({
        LogEntry {"alpha.log", "start"},
        LogEntry {"alpha.log", "same"},
        LogEntry {"alpha.log", "same"},
    });
    incremental.append_lines({
        LogEntry {"alpha.log", "same"},
        LogEntry {"alpha.log", "same"},
        LogEntry {"alpha.log", "other"},
    });

    LogModel one_shot;
    one_shot.append_lines({
        LogEntry {"alpha.log", "start"},
        LogEntry {"alpha.log", "same"},
        LogEntry {"alpha.log", "same"},
        LogEntry {"alpha.log", "same"},
        LogEntry {"alpha.log", "same"},
        LogEntry {"alpha.log", "other"},
    });

    EXPECT_EQ(rendered_texts(incremental), rendered_texts(one_shot));
    EXPECT_EQ(incremental.max_rendered_line_width(), one_shot.max_rendered_line_width());
}

TEST(LogModelTest, HideBeforeLineAppliesToLinesAppendedLater)
{
    LogModel model;
    model.append_lines(numbered_lines(2));

    model.hide_before_line_number(4);
    EXPECT_EQ(model.line_count(), 0);

    model.append_lines({
        LogEntry {"alpha.log", "line 3"},
        LogEntry {"alpha.log", "line 4"},
        LogEntry {"alpha.log", "line 5"},
    });

    EXPECT_EQ(rendered_texts(model), (std::vector<std::string> {
                                         "line 4",
                                         "line 5",
                                     }));
}

TEST(LogModelTest, InvalidRegexFilterIsRejectedAndKeepsExistingFilters)
{
    LogModel model;
    model.append_lines({
        LogEntry {"alpha.log", "error first"},
        LogEntry {"alpha.log", "warn second"},
    });

    model.add_include_filter("error");
    EXPECT_EQ(rendered_texts(model), (std::vector<std::string> {
                                         "error first",
                                     }));

    EXPECT_THROW(model.add_include_filter("re:["), std::invalid_argument);
    EXPECT_THROW(model.add_exclude_filter("re:"), std::invalid_argument);

    EXPECT_EQ(model.include_filters().size(), 1U);
    EXPECT_EQ(model.exclude_filters().size(), 0U);
    EXPECT_EQ(rendered_texts(model), (std::vector<std::string> {
                                         "error first",
                                     }));
}

} // namespace slayerlog
