#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zstd.h>

#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/tracked_source_factory.hpp"
#include "log_source.hpp"

namespace slayerlog
{

namespace
{

std::filesystem::path make_unique_test_path(const std::string& suffix = ".log")
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_tracked_source_manager_" + unique_suffix + suffix);
}

std::vector<unsigned char> compress_zstd_text(std::string_view text)
{
    const std::size_t bound = ZSTD_compressBound(text.size());
    std::vector<unsigned char> compressed(bound);
    const std::size_t written = ZSTD_compress(compressed.data(), compressed.size(), text.data(), text.size(), 1);
    if (ZSTD_isError(written) != 0)
    {
        throw std::runtime_error("Failed to compress zstd test payload");
    }

    compressed.resize(written);
    return compressed;
}

class ScopedTestFile
{
public:
    explicit ScopedTestFile(std::filesystem::path path) : _path(std::move(path))
    {
        std::filesystem::create_directories(_path.parent_path());
        std::ofstream output(_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to create test file");
        }
    }

    ~ScopedTestFile()
    {
        std::error_code error;
        std::filesystem::remove(_path, error);
    }

    const std::filesystem::path& path() const { return _path; }

    void write(const std::string& content) const
    {
        std::ofstream output(_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << content;
        output.close();
        ASSERT_TRUE(output.good());
    }

    void append(const std::string& content) const
    {
        std::ofstream output(_path, std::ios::binary | std::ios::app);
        ASSERT_TRUE(output.is_open());
        output << content;
        output.close();
        ASSERT_TRUE(output.good());
    }

    void write_bytes(const std::vector<unsigned char>& bytes) const
    {
        std::ofstream output(_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        ASSERT_TRUE(output.good());
    }

private:
    std::filesystem::path _path;
};

class RecordingNotificationSink : public NotificationSink
{
public:
    NotificationId show(Notification notification) override
    {
        notifications.push_back(std::move(notification));
        return next_id++;
    }

    void update(NotificationId id, Notification notification) override
    {
        updated_ids.push_back(id);
        notifications.push_back(std::move(notification));
    }

    void dismiss(NotificationId id) override { dismissed_ids.push_back(id); }

    NotificationId next_id = 1;
    std::vector<Notification> notifications;
    std::vector<NotificationId> updated_ids;
    std::vector<NotificationId> dismissed_ids;
};

// Each tracked source embeds its mnemonic as a "<mnemonic> " prefix in every line, so these
// content-focused helpers peel it off to compare against the raw log text.
std::string strip_mnemonic_prefix(std::string text, const std::vector<std::string>& mnemonics)
{
    for (const auto& mnemonic : mnemonics)
    {
        if (mnemonic.empty())
        {
            continue;
        }

        const std::string prefix = mnemonic + " ";
        if (text.compare(0, prefix.size(), prefix) == 0)
        {
            return text.substr(prefix.size());
        }
    }

    return text;
}

std::vector<std::string> all_texts(const AllTrackedSources& tracked_sources)
{
    const auto mnemonics = tracked_sources.source_mnemonics();
    std::vector<std::string> texts;
    texts.reserve(tracked_sources.all_lines().size());
    for (const auto& line : tracked_sources.all_lines())
    {
        texts.push_back(strip_mnemonic_prefix(line->text, mnemonics));
    }

    return texts;
}

std::vector<std::string> delta_texts(const AllTrackedSources& tracked_sources, AllLineIndex first_new_line_index)
{
    const auto mnemonics = tracked_sources.source_mnemonics();
    std::vector<std::string> texts;
    for (int index = first_new_line_index.value; index < tracked_sources.line_count(); ++index)
    {
        texts.push_back(strip_mnemonic_prefix(tracked_sources.all_lines()[AllLineIndex {index}]->text, mnemonics));
    }

    return texts;
}

std::vector<std::string> processed_texts(const AllProcessedSources& processed_sources, const std::vector<std::string>& mnemonics)
{
    std::vector<std::string> texts;
    texts.reserve(static_cast<std::size_t>(processed_sources.total_line_count()));
    for (int index = 0; index < processed_sources.total_line_count(); ++index)
    {
        texts.push_back(strip_mnemonic_prefix(processed_sources.entry_at(AllLineIndex {index}).text, mnemonics));
    }

    return texts;
}

} // namespace

TEST(AllTrackedSourcesTest, OpenSourceLoadsInitialContentsAndPollReturnsOnlyNewLines)
{
    const auto path = make_unique_test_path();
    ScopedTestFile file(path);
    file.write("first\n");

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(path.string())).has_value());

    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {"first"}));

    file.append("second\nthird\n");
    const auto first_new_line_index = tracked_sources.poll();
    ASSERT_TRUE(first_new_line_index.has_value());
    EXPECT_EQ(delta_texts(tracked_sources, *first_new_line_index), (std::vector<std::string> {
                                                                       "second",
                                                                       "third",
                                                                   }));
    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "first",
                                              "second",
                                              "third",
                                          }));
}

TEST(AllTrackedSourcesTest, PollRewritesTailWhenNewTimestampWouldSortBeforeCurrentEnd)
{
    const auto root      = make_unique_test_path("");
    const auto alpha_log = root / "alpha.log";
    const auto beta_log  = root / "beta.log";
    ScopedTestFile alpha_file(alpha_log);
    ScopedTestFile beta_file(beta_log);
    alpha_file.write("2026-04-01T10:02:00 alpha second\n");
    beta_file.write("2026-04-01T10:05:00 beta fifth\n");

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(alpha_log.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(beta_log.string())).has_value());

    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "2026-04-01T10:02:00 alpha second",
                                              "2026-04-01T10:05:00 beta fifth",
                                          }));

    beta_file.append("2026-04-01T10:01:00 beta first late\n");
    const auto first_changed_line_index = tracked_sources.poll();
    ASSERT_TRUE(first_changed_line_index.has_value());
    EXPECT_EQ(first_changed_line_index->value, 0);

    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "2026-04-01T10:01:00 beta first late",
                                              "2026-04-01T10:02:00 alpha second",
                                              "2026-04-01T10:05:00 beta fifth",
                                          }));
}

TEST(AllTrackedSourcesTest, SetSourceTimestampFormatReparsesAndResortsAllLines)
{
    const auto root      = make_unique_test_path("");
    const auto alpha_log = root / "alpha.log";
    const auto beta_log  = root / "beta.log";
    ScopedTestFile alpha_file(alpha_log);
    ScopedTestFile beta_file(beta_log);
    alpha_file.write("2026-04-01T10:00:00 alpha first\n");
    beta_file.write("2026/04/01 10:01:00 beta second\n");

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(alpha_log.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(beta_log.string())).has_value());

    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "2026/04/01 10:01:00 beta second",
                                              "2026-04-01T10:00:00 alpha first",
                                          }));

    ASSERT_FALSE(tracked_sources.set_source_timestamp_format(1, "YYYY/MM/DD hh:mm:ss").has_value());

    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "2026-04-01T10:00:00 alpha first",
                                              "2026/04/01 10:01:00 beta second",
                                          }));
}

TEST(AllTrackedSourcesTest, SetSourceTimestampOffsetResortsByOffsetTimestamp)
{
    const auto root      = make_unique_test_path("");
    const auto alpha_log = root / "alpha.log";
    const auto beta_log  = root / "beta.log";
    ScopedTestFile alpha_file(alpha_log);
    ScopedTestFile beta_file(beta_log);
    alpha_file.write("2026-04-01T10:00:00 alpha first\n");
    beta_file.write("2026-04-01T10:05:00 beta second\n");

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(alpha_log.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(beta_log.string())).has_value());

    ASSERT_FALSE(tracked_sources.set_source_timestamp_offset(1, *parse_log_timestamp_offset("-00 00:10:00")).has_value());

    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "2026-04-01T10:05:00 beta second",
                                              "2026-04-01T10:00:00 alpha first",
                                          }));

    ASSERT_FALSE(tracked_sources.clear_source_timestamp_offset(1).has_value());
    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "2026-04-01T10:00:00 alpha first",
                                              "2026-04-01T10:05:00 beta second",
                                          }));
}

TEST(AllTrackedSourcesTest, RebuildAllLinesReportsProgressThroughNotifier)
{
    const auto path = make_unique_test_path();
    ScopedTestFile file(path);
    file.write("first\nsecond\n");
    auto sink = std::make_shared<RecordingNotificationSink>();

    AllTrackedSources tracked_sources;
    tracked_sources.set_notifier(Notifier(sink));

    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(path.string())).has_value());

    ASSERT_EQ(sink->notifications.size(), 3U);
    EXPECT_EQ(sink->notifications[0].title, "Rebuilding log lines");
    EXPECT_EQ(sink->notifications[0].message, "0% rebuilt");
    ASSERT_TRUE(sink->notifications[0].progress.has_value());
    EXPECT_FLOAT_EQ(*sink->notifications[0].progress, 0.0F);
    EXPECT_EQ(sink->notifications[1].message, "50% rebuilt");
    ASSERT_TRUE(sink->notifications[1].progress.has_value());
    EXPECT_FLOAT_EQ(*sink->notifications[1].progress, 0.5F);
    EXPECT_EQ(sink->notifications[2].message, "100% rebuilt (2 log lines)");
    ASSERT_TRUE(sink->notifications[2].progress.has_value());
    EXPECT_FLOAT_EQ(*sink->notifications[2].progress, 1.0F);
    EXPECT_EQ(sink->updated_ids, std::vector<NotificationId>({1, 1}));
}

TEST(AllTrackedSourcesTest, SetFolderSourceTimestampFormatRebuildsMergedFolderLines)
{
    const auto root      = make_unique_test_path("");
    const auto alpha_log = root / "alpha.log";
    const auto beta_log  = root / "beta.log";
    ScopedTestFile alpha_file(alpha_log);
    ScopedTestFile beta_file(beta_log);
    alpha_file.write("2026-04-01T10:00:00 alpha first\n");
    beta_file.write("2026/04/01 10:01:00 beta second\n");

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, make_local_folder_source(root.string())).has_value());

    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "2026/04/01 10:01:00 beta second",
                                              "2026-04-01T10:00:00 alpha first",
                                          }));

    ASSERT_FALSE(tracked_sources.set_source_timestamp_format(0, "YYYY/MM/DD hh:mm:ss").has_value());

    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "2026-04-01T10:00:00 alpha first",
                                              "2026/04/01 10:01:00 beta second",
                                          }));
}

TEST(AllProcessedSourcesTest, ReplaceFromSourcesUpdatesOnlyChangedSuffix)
{
    const auto root      = make_unique_test_path("");
    const auto alpha_log = root / "alpha.log";
    const auto beta_log  = root / "beta.log";
    ScopedTestFile alpha_file(alpha_log);
    ScopedTestFile beta_file(beta_log);
    alpha_file.write("2026-04-01T10:00:00 alpha first\n");
    beta_file.write("2026-04-01T10:10:00 beta second\n");

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(alpha_log.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(beta_log.string())).has_value());

    AllProcessedSources processed_sources;
    processed_sources.rebuild_from_sources(tracked_sources);

    alpha_file.append("2026-04-01T10:20:00 alpha third\n");
    const auto alpha_append_index = tracked_sources.poll();
    ASSERT_TRUE(alpha_append_index.has_value());
    processed_sources.append_from_sources(tracked_sources, *alpha_append_index);

    beta_file.append("2026-04-01T10:15:00 beta late\n");
    const auto first_changed_index = tracked_sources.poll();
    ASSERT_TRUE(first_changed_index.has_value());
    EXPECT_EQ(first_changed_index->value, 2);

    processed_sources.replace_from_sources(tracked_sources, *first_changed_index);

    EXPECT_EQ(processed_texts(processed_sources, tracked_sources.source_mnemonics()), (std::vector<std::string> {
                                                      "2026-04-01T10:00:00 alpha first",
                                                      "2026-04-01T10:10:00 beta second",
                                                      "2026-04-01T10:15:00 beta late",
                                                      "2026-04-01T10:20:00 alpha third",
                                                  }));
}

TEST(AllProcessedSourcesTest, QueuesReplaceFromSourcesWhilePausedAndAppliesOnResume)
{
    const auto root      = make_unique_test_path("");
    const auto alpha_log = root / "alpha.log";
    const auto beta_log  = root / "beta.log";
    ScopedTestFile alpha_file(alpha_log);
    ScopedTestFile beta_file(beta_log);
    alpha_file.write("2026-04-01T10:00:00 alpha first\n");
    beta_file.write("2026-04-01T10:10:00 beta second\n");

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(alpha_log.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(beta_log.string())).has_value());

    AllProcessedSources processed_sources;
    processed_sources.rebuild_from_sources(tracked_sources);

    alpha_file.append("2026-04-01T10:20:00 alpha third\n");
    const auto alpha_append_index = tracked_sources.poll();
    ASSERT_TRUE(alpha_append_index.has_value());
    processed_sources.append_from_sources(tracked_sources, *alpha_append_index);

    processed_sources.toggle_pause();

    beta_file.append("2026-04-01T10:15:00 beta late\n");
    const auto first_changed_index = tracked_sources.poll();
    ASSERT_TRUE(first_changed_index.has_value());
    EXPECT_EQ(first_changed_index->value, 2);
    processed_sources.replace_from_sources(tracked_sources, *first_changed_index);

    EXPECT_EQ(processed_sources.total_line_count(), 3);
    EXPECT_EQ(processed_texts(processed_sources, tracked_sources.source_mnemonics()), (std::vector<std::string> {
                                                      "2026-04-01T10:00:00 alpha first",
                                                      "2026-04-01T10:10:00 beta second",
                                                      "2026-04-01T10:20:00 alpha third",
                                                  }));

    alpha_file.append("2026-04-01T10:25:00 alpha fourth\n");
    const auto alpha_fourth_index = tracked_sources.poll();
    ASSERT_TRUE(alpha_fourth_index.has_value());
    processed_sources.append_from_sources(tracked_sources, *alpha_fourth_index);

    EXPECT_EQ(processed_sources.total_line_count(), 3);

    processed_sources.toggle_pause();

    EXPECT_EQ(processed_texts(processed_sources, tracked_sources.source_mnemonics()), (std::vector<std::string> {
                                                      "2026-04-01T10:00:00 alpha first",
                                                      "2026-04-01T10:10:00 beta second",
                                                      "2026-04-01T10:15:00 beta late",
                                                      "2026-04-01T10:20:00 alpha third",
                                                      "2026-04-01T10:25:00 alpha fourth",
                                                  }));
}

TEST(AllTrackedSourcesTest, RebuildsSourceLabelsWhenBasenameCollisionsChange)
{
    const auto root        = make_unique_test_path("");
    const auto first_path  = root / "first" / "app.log";
    const auto second_path = root / "second" / "app.log";
    ScopedTestFile first_file(first_path);
    ScopedTestFile second_file(second_path);

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(first_path.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(second_path.string())).has_value());

    const auto labels_with_collision = tracked_sources.source_labels();
    ASSERT_EQ(labels_with_collision.size(), 2U);
    EXPECT_EQ(labels_with_collision[0], first_path.string());
    EXPECT_EQ(labels_with_collision[1], second_path.string());

    std::string closed_label;
    ASSERT_FALSE(tracked_sources.close_source(1, &closed_label).has_value());
    EXPECT_EQ(closed_label, second_path.string());

    const auto labels_without_collision = tracked_sources.source_labels();
    ASSERT_EQ(labels_without_collision.size(), 1U);
    EXPECT_EQ(labels_without_collision[0], "app.log");
}

TEST(AllTrackedSourcesTest, EmbedsMnemonicPrefixWhileKeepingTimestampExtraction)
{
    const auto root      = make_unique_test_path("");
    const auto alpha_log = root / "alpha.log";
    const auto beta_log  = root / "beta.log";
    ScopedTestFile alpha_file(alpha_log);
    ScopedTestFile beta_file(beta_log);
    alpha_file.write("2026-04-01T10:00:00 hello world\n");
    beta_file.write("2026-04-01T10:05:00 beta line\n");

    // Two sources, so mnemonics are visible.
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(alpha_log.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(beta_log.string())).has_value());

    const auto mnemonics = tracked_sources.source_mnemonics();
    ASSERT_EQ(mnemonics.size(), 2U);
    const std::string alpha_prefix = mnemonics[0] + " ";

    // alpha sorts first (earlier timestamp); its line carries alpha's mnemonic prefix.
    ASSERT_GE(tracked_sources.line_count(), 1);
    const auto& entry = *tracked_sources.all_lines()[AllLineIndex {0}];
    EXPECT_EQ(entry.text, alpha_prefix + "2026-04-01T10:00:00 hello world");

    // The extracted-timestamp offsets were shifted past the prefix, so they still bracket the
    // timestamp inside the prefixed text.
    ASSERT_TRUE(entry.metadata.extracted_time_start.has_value());
    ASSERT_TRUE(entry.metadata.extracted_time_end.has_value());
    EXPECT_EQ(entry.text.substr(*entry.metadata.extracted_time_start, *entry.metadata.extracted_time_end - *entry.metadata.extracted_time_start), "2026-04-01T10:00:00");

    // Rendering still lifts the timestamp into its own column and drops it from the message,
    // leaving the mnemonic prefix in place.
    AllProcessedSources processed_sources;
    processed_sources.rebuild_from_sources(tracked_sources);
    const std::string rendered = processed_sources.rendered_line(0);
    EXPECT_NE(rendered.find("{2026-04-01 10:00:00}"), std::string::npos);
    EXPECT_NE(rendered.find(alpha_prefix), std::string::npos);
    EXPECT_EQ(rendered.find("2026-04-01T10:00:00"), std::string::npos);
}

TEST(AllTrackedSourcesTest, HidesMnemonicForLoneSourceAndShowsItWhenSecondOpens)
{
    const auto root      = make_unique_test_path("");
    const auto alpha_log = root / "alpha.log";
    const auto beta_log  = root / "beta.log";
    ScopedTestFile alpha_file(alpha_log);
    ScopedTestFile beta_file(beta_log);
    alpha_file.write("alpha one\n");
    beta_file.write("beta one\n");

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(alpha_log.string())).has_value());

    // A lone source shows no mnemonic prefix, even though it has a mnemonic assigned.
    ASSERT_FALSE(tracked_sources.source_mnemonics()[0].empty());
    ASSERT_EQ(tracked_sources.line_count(), 1);
    EXPECT_EQ(tracked_sources.all_lines()[AllLineIndex {0}]->text, "alpha one");

    // Opening a second source reveals the prefix on every source's lines.
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(beta_log.string())).has_value());
    const auto mnemonics = tracked_sources.source_mnemonics();
    ASSERT_EQ(mnemonics.size(), 2U);

    bool any_prefixed = false;
    for (const auto& line : tracked_sources.all_lines())
    {
        if (line->text.rfind(mnemonics[0] + " ", 0) == 0 || line->text.rfind(mnemonics[1] + " ", 0) == 0)
        {
            any_prefixed = true;
        }
    }
    EXPECT_TRUE(any_prefixed);

    // Closing back down to a single source hides the prefix again.
    ASSERT_FALSE(tracked_sources.close_source(1).has_value());
    ASSERT_EQ(tracked_sources.line_count(), 1);
    EXPECT_EQ(tracked_sources.all_lines()[AllLineIndex {0}]->text, "alpha one");
}

TEST(AllTrackedSourcesTest, ClosingSourceKeepsRemainingSourceMnemonicsStable)
{
    const auto root      = make_unique_test_path("");
    const auto alpha_log = root / "alpha.log";
    const auto beta_log  = root / "beta.log";
    const auto gamma_log = root / "gamma.log";
    ScopedTestFile alpha_file(alpha_log);
    ScopedTestFile beta_file(beta_log);
    ScopedTestFile gamma_file(gamma_log);

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(alpha_log.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(beta_log.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(gamma_log.string())).has_value());

    const auto mnemonics_before = tracked_sources.source_mnemonics();
    ASSERT_EQ(mnemonics_before.size(), 3U);

    // Closing the middle source must not reshuffle the mnemonics of the survivors.
    ASSERT_FALSE(tracked_sources.close_source(1).has_value());

    const auto mnemonics_after = tracked_sources.source_mnemonics();
    ASSERT_EQ(mnemonics_after.size(), 2U);
    EXPECT_EQ(mnemonics_after[0], mnemonics_before[0]);
    EXPECT_EQ(mnemonics_after[1], mnemonics_before[2]);
}

TEST(AllTrackedSourcesTest, OpenFolderLoadsInitialContentsAsSingleTrackedSource)
{
    const auto root   = make_unique_test_path("");
    const auto folder = root / "archive";
    const auto first  = folder / "alpha.log";
    const auto second = folder / "beta.log";
    ScopedTestFile first_file(first);
    ScopedTestFile second_file(second);
    first_file.write("2026-04-01T10:02:00 alpha second\n");
    second_file.write("2026-04-01T10:01:00 beta first\n");

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, make_local_folder_source(folder.string())).has_value());

    EXPECT_EQ(tracked_sources.source_count(), 1U);
    EXPECT_EQ(tracked_sources.source_labels(), (std::vector<std::string> {"archive"}));
    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "2026-04-01T10:01:00 beta first",
                                              "2026-04-01T10:02:00 alpha second",
                                          }));
    EXPECT_FALSE(tracked_sources.poll().has_value());
}

TEST(AllTrackedSourcesTest, FolderSourceContinuesProducingIncrementalUpdatesAfterOpen)
{
    const auto root       = make_unique_test_path("");
    const auto folder     = root / "archive";
    const auto plain_file = folder / "alpha.log";
    const auto zstd_file  = folder / "beta.log.zst";
    ScopedTestFile first_file(plain_file);
    ScopedTestFile compressed_file(zstd_file);
    first_file.write("2026-04-01T10:02:00 alpha second\n");
    compressed_file.write_bytes(compress_zstd_text("2026-04-01T10:01:00 beta first\n"));

    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, make_local_folder_source(folder.string())).has_value());

    EXPECT_EQ(all_texts(tracked_sources), (std::vector<std::string> {
                                              "2026-04-01T10:01:00 beta first",
                                              "2026-04-01T10:02:00 alpha second",
                                          }));

    first_file.append("2026-04-01T10:03:00 alpha third\n");
    const auto first_new_line_index = tracked_sources.poll();
    ASSERT_TRUE(first_new_line_index.has_value());
    EXPECT_EQ(delta_texts(tracked_sources, *first_new_line_index), (std::vector<std::string> {
                                                                       "2026-04-01T10:03:00 alpha third",
                                                                   }));

    EXPECT_FALSE(tracked_sources.poll().has_value());
}

} // namespace slayerlog
