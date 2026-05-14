#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "commands/command_history.hpp"
#include "timestamp/source_timestamp_parser.hpp"

namespace slayerlog
{

namespace
{

std::filesystem::path make_temp_settings_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_command_history_tests_" + unique_suffix + ".ini");
}

void remove_temp_settings_file(const std::filesystem::path& settings_path)
{
    std::error_code error_code;
    std::filesystem::remove(settings_path, error_code);
    std::filesystem::remove(settings_path.string() + ".tmp", error_code);

    const auto parent_path = settings_path.parent_path();
    if (parent_path.empty() || !std::filesystem::exists(parent_path, error_code))
    {
        return;
    }

    const std::string backup_prefix = settings_path.filename().string() + ".";
    for (const auto& entry : std::filesystem::directory_iterator(parent_path, error_code))
    {
        if (error_code)
        {
            return;
        }

        const auto filename = entry.path().filename().string();
        if (filename.rfind(backup_prefix, 0) == 0 && entry.path().extension() == ".bak")
        {
            std::filesystem::remove(entry.path(), error_code);
        }
    }
}

} // namespace

TEST(CommandHistoryTest, LoadsAsEmptyWhenSettingsFileDoesNotExist)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;

    ASSERT_TRUE(history.load(error_message));
    EXPECT_TRUE(history.entries().empty());

    remove_temp_settings_file(settings_path);
}

TEST(CommandHistoryTest, StoresUniqueEntriesWithMostRecentFirst)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;

    ASSERT_TRUE(history.load(error_message));
    ASSERT_TRUE(history.record_command("find error", error_message));
    ASSERT_TRUE(history.record_command("filter-in auth", error_message));
    ASSERT_TRUE(history.record_command("find error", error_message));

    ASSERT_EQ(history.entries().size(), 2U);
    EXPECT_EQ(history.entries()[0], "find error");
    EXPECT_EQ(history.entries()[1], "filter-in auth");

    SettingsStore reloaded_store(settings_path);
    CommandHistory reloaded_history(reloaded_store);
    ASSERT_TRUE(reloaded_history.load(error_message));
    ASSERT_EQ(reloaded_history.entries().size(), 2U);
    EXPECT_EQ(reloaded_history.entries()[0], "find error");
    EXPECT_EQ(reloaded_history.entries()[1], "filter-in auth");

    remove_temp_settings_file(settings_path);
}

TEST(CommandHistoryTest, MatchesEntriesUsingCaseInsensitiveSubstring)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;

    ASSERT_TRUE(history.load(error_message));
    ASSERT_TRUE(history.record_command("find Error", error_message));
    ASSERT_TRUE(history.record_command("filter-in auth", error_message));

    const auto matches = history.matching_entries("erR");
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0], "find Error");

    remove_temp_settings_file(settings_path);
}

TEST(CommandHistoryTest, SeedsDefaultTimestampFormatsWhenMissing)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    std::string error_message;

    ASSERT_TRUE(settings_store.load(error_message));
    ASSERT_TRUE(settings_store.ensure_default_values("timestamp_formats", "format", default_timestamp_formats(), error_message));

    const auto formats = settings_store.ini().values("timestamp_formats", "format");
    EXPECT_EQ(formats, default_timestamp_formats());

    remove_temp_settings_file(settings_path);
}

TEST(CommandHistoryTest, AddsMissingDefaultTimestampFormatsToExistingSettings)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    std::string error_message;
    std::vector<std::string> existing_formats = default_timestamp_formats();
    existing_formats.erase(std::remove(existing_formats.begin(), existing_formats.end(), "YYYY-MM-DDThh:mm:ss.ffffff"), existing_formats.end());
    existing_formats.push_back("custom-format");

    ASSERT_TRUE(settings_store.load(error_message));
    settings_store.ini().set_values("timestamp_formats", "format", existing_formats);
    ASSERT_TRUE(settings_store.save(error_message));

    ASSERT_TRUE(settings_store.ensure_default_values("timestamp_formats", "format", default_timestamp_formats(), error_message));

    auto expected_formats = default_timestamp_formats();
    expected_formats.push_back("custom-format");
    const auto formats = settings_store.ini().values("timestamp_formats", "format");
    EXPECT_EQ(formats, expected_formats);

    remove_temp_settings_file(settings_path);
}

} // namespace slayerlog
