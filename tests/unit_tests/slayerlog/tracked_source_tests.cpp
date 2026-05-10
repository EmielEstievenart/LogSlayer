#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <zstd.h>

#include "tracked_source_base.hpp"
#include "tracked_source_file.hpp"
#include "tracked_source_folder.hpp"

namespace slayerlog
{

namespace
{

std::filesystem::path make_unique_test_folder()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_tracked_source_" + unique_suffix);
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

class ScopedTestFolder
{
public:
    ScopedTestFolder()
    {
        _path = make_unique_test_folder();
        std::filesystem::create_directories(_path);
    }

    ~ScopedTestFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(_path, error);
    }

    const std::filesystem::path& path() const { return _path; }

    void write_file(const std::string& file_name, const std::string& content) const
    {
        const auto file_path = _path / file_name;
        std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to create test file");
        }

        output << content;
    }

    void append_file(const std::string& file_name, const std::string& content) const
    {
        const auto file_path = _path / file_name;
        std::ofstream output(file_path, std::ios::binary | std::ios::app);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to append test file");
        }

        output << content;
    }

    void write_zstd_file(const std::string& file_name, std::string_view content) const
    {
        const auto bytes     = compress_zstd_text(content);
        const auto file_path = _path / file_name;
        std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to create test zstd file");
        }

        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void remove_file(const std::string& file_name) const
    {
        std::error_code error;
        std::filesystem::remove(_path / file_name, error);
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

std::vector<std::string> delta_texts(const TrackedSourceBase& tracked_source, std::size_t first_new_entry_index)
{
    std::vector<std::string> texts;
    const auto& entries = tracked_source.entries();
    texts.reserve(entries.size() - first_new_entry_index);
    for (std::size_t entry_index = first_new_entry_index; entry_index < entries.size(); ++entry_index)
    {
        texts.push_back(entries[entry_index]->text);
    }

    return texts;
}

void expect_poll_lines(TrackedSourceBase& tracked_source, const std::vector<std::string>& expected_lines)
{
    const std::size_t first_new_entry_index = tracked_source.entries().size();
    ASSERT_TRUE(tracked_source.poll());
    EXPECT_EQ(delta_texts(tracked_source, first_new_entry_index), expected_lines);
}

void expect_no_poll_lines(TrackedSourceBase& tracked_source)
{
    const std::size_t entry_count = tracked_source.entries().size();
    EXPECT_FALSE(tracked_source.poll());
    EXPECT_EQ(tracked_source.entries().size(), entry_count);
}

} // namespace

TEST(TrackedSourceTest, StoresParsedEntriesAndSequenceNumbers)
{
    TrackedSourceFile tracked_source(parse_log_source("alpha.log"), "alpha.log");

    tracked_source.add_entries_from_raw_strings({
        "2026-04-01T10:00:00 first",
        "plain second",
    });

    const auto& entries = tracked_source.entries();
    ASSERT_EQ(entries.size(), 2U);

    EXPECT_EQ(entries[0]->text, "2026-04-01T10:00:00 first");
    EXPECT_TRUE(entries[0]->metadata.timestamp.has_value());
    EXPECT_EQ(entries[0]->metadata.extracted_time_text, "2026-04-01T10:00:00");
    EXPECT_EQ(format_log_timestamp_utc(*entries[0]->metadata.timestamp), "2026-04-01 10:00:00");
    ASSERT_TRUE(entries[0]->metadata.extracted_time_start.has_value());
    ASSERT_TRUE(entries[0]->metadata.extracted_time_end.has_value());
    EXPECT_EQ(*entries[0]->metadata.extracted_time_start, 0U);
    EXPECT_EQ(*entries[0]->metadata.extracted_time_end, 19U);
    EXPECT_EQ(entries[0]->metadata.sequence_number, 0U);

    EXPECT_EQ(entries[1]->text, "plain second");
    EXPECT_FALSE(entries[1]->metadata.timestamp.has_value());
    EXPECT_TRUE(entries[1]->metadata.extracted_time_text.empty());
    EXPECT_FALSE(entries[1]->metadata.extracted_time_start.has_value());
    EXPECT_FALSE(entries[1]->metadata.extracted_time_end.has_value());
    EXPECT_EQ(entries[1]->metadata.sequence_number, 1U);
}

TEST(TrackedSourceTest, UpdatesSourceLabelWithoutTouchingStoredEntries)
{
    TrackedSourceFile tracked_source(parse_log_source("alpha.log"), "alpha.log");
    tracked_source.add_entries_from_raw_strings({"plain line"});

    tracked_source.set_source_label("renamed.log");

    EXPECT_EQ(tracked_source.source_label(), "renamed.log");
    ASSERT_EQ(tracked_source.entries().size(), 1U);
    EXPECT_EQ(tracked_source.entries()[0]->text, "plain line");
}

TEST(TrackedSourceTest, SetTimestampFormatReparsesExistingFileEntries)
{
    TrackedSourceFile tracked_source(parse_log_source("alpha.log"), "alpha.log");

    tracked_source.add_entries_from_raw_strings({
        "2026/04/01 10:00:00 slash timestamp",
        "plain follow-up",
    });

    ASSERT_EQ(tracked_source.entries().size(), 2U);
    EXPECT_FALSE(tracked_source.entries()[0]->metadata.timestamp.has_value());

    tracked_source.set_timestamp_format("YYYY/MM/DD hh:mm:ss");

    EXPECT_TRUE(tracked_source.entries()[0]->metadata.timestamp.has_value());
    EXPECT_EQ(tracked_source.entries()[0]->metadata.extracted_time_text, "2026/04/01 10:00:00");
    EXPECT_EQ(format_log_timestamp_utc(*tracked_source.entries()[0]->metadata.timestamp), "2026-04-01 10:00:00");
    EXPECT_FALSE(tracked_source.entries()[1]->metadata.timestamp.has_value());
    EXPECT_EQ(tracked_source.entries()[0]->metadata.sequence_number, 0U);
    EXPECT_EQ(tracked_source.entries()[1]->metadata.sequence_number, 1U);
}

TEST(TrackedSourceTest, TimestampOffsetReappliesFromOriginalTimestamp)
{
    auto formats = std::make_shared<const TimestampFormatCatalog>(std::vector<std::string> {"YYYY-MM-DDThh:mm:ss.fff"});
    TrackedSourceFile tracked_source(parse_log_source("alpha.log"), "alpha.log", formats);
    tracked_source.add_entries_from_raw_strings({"2026-04-01T10:00:00.250 first"});
    ASSERT_EQ(tracked_source.entries().size(), 1U);
    ASSERT_TRUE(tracked_source.entries()[0]->metadata.timestamp.has_value());

    ASSERT_FALSE(tracked_source.set_timestamp_offset(*parse_log_timestamp_offset("00 00:00:10.500")).has_value());
    ASSERT_TRUE(tracked_source.entries()[0]->metadata.offset_timestamp.has_value());
    EXPECT_EQ(format_log_timestamp_utc(*tracked_source.entries()[0]->metadata.offset_timestamp), "2026-04-01 10:00:10.75");

    ASSERT_FALSE(tracked_source.set_timestamp_offset(*parse_log_timestamp_offset("00 00:00:20.000")).has_value());
    ASSERT_TRUE(tracked_source.entries()[0]->metadata.offset_timestamp.has_value());
    EXPECT_EQ(format_log_timestamp_utc(*tracked_source.entries()[0]->metadata.offset_timestamp), "2026-04-01 10:00:20.25");

    tracked_source.clear_timestamp_offset();
    EXPECT_FALSE(tracked_source.entries()[0]->metadata.offset_timestamp.has_value());
}

TEST(TrackedSourceTest, FilePollReadsZstdFileOnce)
{
    ScopedTestFolder folder;
    folder.write_zstd_file("single.log.zst", "2026-04-01T10:01:00 from zst\nplain zst follow-up\n");

    TrackedSourceFile tracked_source(parse_log_source((folder.path() / "single.log.zst").string()), "single.log.zst");
    expect_poll_lines(tracked_source, {
                                        "2026-04-01T10:01:00 from zst",
                                        "plain zst follow-up",
                                    });
    expect_no_poll_lines(tracked_source);
}

TEST(TrackedSourceTest, FilePollUsesBlfWatcherForCaseInsensitiveBlfExtension)
{
    ScopedTestFolder folder;
    const std::vector<std::string> file_names {"lower.blf", "upper.BLF", "mixed.BlF"};

    for (const auto& file_name : file_names)
    {
        folder.write_file(file_name, "raw blf bytes should not be tailed as text\n");

        TrackedSourceFile tracked_source(parse_log_source((folder.path() / file_name).string()), file_name);

        ASSERT_TRUE(tracked_source.poll()) << file_name;
        ASSERT_FALSE(tracked_source.entries().empty()) << file_name;
        EXPECT_NE(tracked_source.entries().front()->text.find("\"kind\":\"import_error\""), std::string::npos) << file_name;
        EXPECT_NE(tracked_source.entries().front()->text.find("\"schema\":\"logslayer.blf.v1\""), std::string::npos) << file_name;
        EXPECT_NE(tracked_source.entries().front()->text.find("raw blf bytes should not be tailed as text"), 0U) << file_name;
    }
}

TEST(TrackedSourceTest, FilePollDoesNotTreatBlffooAsBlf)
{
    ScopedTestFolder folder;
    folder.write_file("plain.blffoo", "plain text\n");

    TrackedSourceFile tracked_source(parse_log_source((folder.path() / "plain.blffoo").string()), "plain.blffoo");

    expect_poll_lines(tracked_source, {"plain text"});
}

TEST(TrackedSourceTest, FolderPollKeepsTailingNormalFilesAfterFirstPoll)
{
    ScopedTestFolder folder;
    folder.write_file("alpha.log", "first\nsecond\n");

    TrackedSourceFolder tracked_source(make_local_folder_source(folder.path().string()), "archive");
    expect_poll_lines(tracked_source, {"first", "second"});

    folder.append_file("alpha.log", "third\nfourth\n");
    expect_poll_lines(tracked_source, {"third", "fourth"});
    expect_no_poll_lines(tracked_source);
}

TEST(TrackedSourceTest, FolderPollReportsOpenProgressThroughNotifier)
{
    ScopedTestFolder folder;
    folder.write_file("alpha.log", "first\n");
    folder.write_file("beta.log", "second\n");
    auto sink = std::make_shared<RecordingNotificationSink>();

    TrackedSourceFolder tracked_source(make_local_folder_source(folder.path().string()), "archive", default_timestamp_format_catalog(), Notifier(sink));

    expect_poll_lines(tracked_source, {"first", "second"});

    ASSERT_EQ(sink->notifications.size(), 5U);
    EXPECT_EQ(sink->notifications[0].title, "Sorting folder files");
    EXPECT_EQ(sink->notifications[0].message, "0 / 2 files sorted");
    EXPECT_EQ(sink->notifications[0].level, NotificationLevel::Info);
    ASSERT_TRUE(sink->notifications[0].progress.has_value());
    EXPECT_FLOAT_EQ(*sink->notifications[0].progress, 0.0F);
    EXPECT_EQ(sink->notifications[1].message, "2 / 2 files sorted");
    ASSERT_TRUE(sink->notifications[1].progress.has_value());
    EXPECT_FLOAT_EQ(*sink->notifications[1].progress, 1.0F);
    EXPECT_EQ(sink->notifications[2].title, "Opening folder");
    EXPECT_EQ(sink->notifications[2].message, "0 / 2 files opened");
    EXPECT_EQ(sink->notifications[3].message, "1 / 2 files opened");
    EXPECT_EQ(sink->notifications[4].message, "2 / 2 files opened");
    ASSERT_TRUE(sink->notifications[2].progress.has_value());
    EXPECT_FLOAT_EQ(*sink->notifications[2].progress, 0.0F);
    ASSERT_TRUE(sink->notifications[4].progress.has_value());
    EXPECT_FLOAT_EQ(*sink->notifications[4].progress, 1.0F);
    EXPECT_EQ(sink->updated_ids, std::vector<NotificationId>({1, 2, 2}));
}

TEST(TrackedSourceTest, FolderPollDiscoversNewlyCreatedNormalFiles)
{
    ScopedTestFolder folder;
    folder.write_file("alpha.log", "alpha first\n");

    TrackedSourceFolder tracked_source(make_local_folder_source(folder.path().string()), "archive");
    expect_poll_lines(tracked_source, {"alpha first"});

    folder.write_file("beta.log", "beta first\n");
    expect_poll_lines(tracked_source, {"beta first"});

    folder.append_file("alpha.log", "alpha second\n");
    expect_poll_lines(tracked_source, {"alpha second"});
}

TEST(TrackedSourceTest, FolderPollDiscoversNewlyCreatedZstdFilesOnce)
{
    ScopedTestFolder folder;
    folder.write_file("alpha.log", "2026-04-01T10:02:00 alpha second\n");

    TrackedSourceFolder tracked_source(make_local_folder_source(folder.path().string()), "archive");
    expect_poll_lines(tracked_source, {"2026-04-01T10:02:00 alpha second"});

    folder.write_zstd_file("beta.log.zst", "2026-04-01T10:01:00 from zst\nplain zst follow-up\n");
    expect_poll_lines(tracked_source, {
                                        "2026-04-01T10:01:00 from zst",
                                        "plain zst follow-up",
                                    });
    expect_no_poll_lines(tracked_source);
}

TEST(TrackedSourceTest, FolderPollDeletedAndRecreatedZstdFileIsReread)
{
    ScopedTestFolder folder;
    folder.write_zstd_file("archive.log.zst", "first\n");

    TrackedSourceFolder tracked_source(make_local_folder_source(folder.path().string()), "archive");
    expect_poll_lines(tracked_source, {"first"});

    folder.remove_file("archive.log.zst");
    expect_no_poll_lines(tracked_source);

    folder.write_zstd_file("archive.log.zst", "second\n");
    expect_poll_lines(tracked_source, {"second"});
    expect_no_poll_lines(tracked_source);
}

TEST(TrackedSourceTest, FolderPollMergesPlainAndZstdChildResultsByTimestamp)
{
    ScopedTestFolder folder;
    folder.write_file("alpha.log", "2026-04-01T10:02:00 alpha second\n");
    folder.write_zstd_file("beta.log.zst", "2026-04-01T10:01:00 beta first\n");

    TrackedSourceFolder tracked_source(make_local_folder_source(folder.path().string()), "archive");
    expect_poll_lines(tracked_source, {
                                        "2026-04-01T10:01:00 beta first",
                                        "2026-04-01T10:02:00 alpha second",
                                    });

    const auto& entries = tracked_source.entries();
    ASSERT_EQ(entries.size(), 2U);
    EXPECT_EQ(entries[0]->metadata.extracted_time_text, "2026-04-01T10:01:00");
    EXPECT_EQ(entries[1]->metadata.extracted_time_text, "2026-04-01T10:02:00");
    ASSERT_TRUE(entries[0]->metadata.extracted_time_start.has_value());
    ASSERT_TRUE(entries[0]->metadata.extracted_time_end.has_value());
    ASSERT_TRUE(entries[1]->metadata.extracted_time_start.has_value());
    ASSERT_TRUE(entries[1]->metadata.extracted_time_end.has_value());
    EXPECT_EQ(*entries[0]->metadata.extracted_time_start, 0U);
    EXPECT_EQ(*entries[0]->metadata.extracted_time_end, 19U);
    EXPECT_EQ(*entries[1]->metadata.extracted_time_start, 0U);
    EXPECT_EQ(*entries[1]->metadata.extracted_time_end, 19U);
}

TEST(TrackedSourceTest, FolderPollMissingFolderThrows)
{
    const auto missing_path = make_unique_test_folder();
    TrackedSourceFolder tracked_source(make_local_folder_source(missing_path.string()), "missing");

    EXPECT_THROW(tracked_source.poll(), std::runtime_error);
}

} // namespace slayerlog
